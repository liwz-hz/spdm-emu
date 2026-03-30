# TF-RMM 设备直通 SPDM/TDISP 流程分析

## 1. 概述

本文档分析 TF-RMM (Trusted Firmware RMM) 项目中设备直通 (Device Assignment, DA) 流程的 SPDM 和 TDISP 协议实现。

TF-RMM 是 ARM CCA (Confidential Computing Architecture) 架构中的 Realm Management Monitor 实现，负责管理 Realm (可信执行环境) 与设备之间的安全交互。

### 1.1 项目路径

- 项目根目录：`/home/lmm/code/tf-rmm`
- 设备直通应用：`app/device_assignment/el0_app/`

### 1.2 核心文件

| 文件 | 功能 |
|------|------|
| `dev_assign_el0_app.c` | 主入口和传输层实现 |
| `dev_assign_cmds.c` | SPDM 连接和会话管理 |
| `dev_tdisp_cmds.c` | TDISP 流程实现 |
| `dev_assign_ide_cmds.c` | IDE KM (Key Management) 流程 |
| `dev_assign_private.h` | 核心数据结构定义 |

## 2. 架构设计

### 2.1 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                    NS Host (Normal World)                   │
│  ┌─────────────────┐    ┌─────────────────────────────┐   │
│  │   Device Driver │◄──►│        SPDM Responder       │   │
│  │   (PCIe Device) │    │    (设备端 SPDM 实现)        │   │
│  └─────────────────┘    └─────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                    ▲
                    │ PCIe / TDISP
                    ▼
┌─────────────────────────────────────────────────────────────┐
│                    RMM (Realm World)                        │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              el0_app (SPDM Requester)               │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐ │   │
│  │  │ SPDM Client │  │ TDISP Client│  │  IDE KM     │ │   │
│  │  │  (会话管理) │  │ (设备锁定)  │  │  (密钥)     │ │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘ │   │
│  │                      │                              │   │
│  │                      ▼                              │   │
│  │            ┌─────────────────┐                     │   │
│  │            │  传输层抽象     │                     │   │
│  │            │ (无MCTP头封装)  │                     │   │
│  │            └─────────────────┘                     │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 核心数据结构

```c
struct dev_assign_info {
    struct granule *dev_granule;           // 设备粒度
    void *spdm_context;                     // SPDM 上下文
    uint32_t session_id;                    // SPDM 会话 ID
    uint8_t slot_id;                        // 证书槽 ID
    uint8_t spdm_version;                   // SPDM 版本
    uint8_t measurement_hash_algo;          // 测量哈希算法
    uint8_t base_asym_algo;                  // 非对称算法
    uint8_t base_hash_algo;                  // 哈希算法
    struct dev_assign_ide_stream ide_streams[DEV_ASSIGN_IDE_STREAMS_NUM];
    bool ide_enabled;                       // IDE 是否启用
};
```

## 3. SPDM API 调用流程

### 3.1 SPDM 初始化与连接

```
libspdm_init_connection()
├── libspdm_get_version()           # 获取版本协商
├── libspdm_get_capabilities()     # 获取能力协商
└── libspdm_negotiate_algorithms() # 算法协商
```

**代码位置：** `dev_assign_cmds.c:93`

```c
int dev_assign_connect(struct dev_assign_info *dev_assign)
{
    libspdm_return_t status;
    
    status = libspdm_init_connection(
        dev_assign->spdm_context,    // SPDM 上下文
        true,                         // 获取证书
        true                          // 执行测量 (可选)
    );
    
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        return -1;
    }
    
    return 0;
}
```

### 3.2 证书获取

```
libspdm_get_certificate()
├── 获取证书链 (逐段获取)
├── 验证证书链完整性
└── 提取公钥信息
```

**代码位置：** `dev_assign_cmds.c:111`

```c
int dev_assign_get_certificate(struct dev_assign_info *dev_assign)
{
    libspdm_return_t status;
    void *cert_chain;
    size_t cert_chain_size;
    
    cert_chain_size = LIBSPDM_MAX_CERT_CHAIN_SIZE;
    
    status = libspdm_get_certificate(
        dev_assign->spdm_context,
        dev_assign->slot_id,           // 证书槽 ID
        &cert_chain_size,
        &cert_chain,
        NULL,                          // 不存储哈希
        NULL,
        NULL
    );
    
    return (LIBSPDM_STATUS_IS_ERROR(status)) ? -1 : 0;
}
```

### 3.3 安全会话建立

```
libspdm_start_session()
├── KEY_EXCHANGE 请求
├── 测量 (可选)
├── FINISH 握手
└── 返回会话 ID
```

**代码位置：** `dev_assign_cmds.c:170`

```c
int dev_assign_start_session(struct dev_assign_info *dev_assign)
{
    libspdm_return_t status;
    uint32_t session_id;
    uint8_t heartbeat_period;
    uint8_t measurement_count;
    void *measurement_hash;
    
    status = libspdm_start_session(
        dev_assign->spdm_context,
        false,                         // 不使用测量签名
        dev_assign->slot_id,
        0,                             // 测量哈希类型
        NULL,                          // 无随机数
        &session_id,
        &heartbeat_period,
        &measurement_count,
        &measurement_hash
    );
    
    if (LIBSPDM_STATUS_IS_SUCCESS(status)) {
        dev_assign->session_id = session_id;
    }
    
    return (LIBSPDM_STATUS_IS_ERROR(status)) ? -1 : 0;
}
```

### 3.4 会话停止

```
libspdm_stop_session()
├── END_SESSION 请求
├── 清理会话状态
└── 释放会话资源
```

**代码位置：** `dev_assign_cmds.c:283`

```c
int dev_assign_stop_session(struct dev_assign_info *dev_assign)
{
    libspdm_return_t status;
    
    status = libspdm_stop_session(
        dev_assign->spdm_context,
        dev_assign->session_id,
        0                              // 结束会话原因
    );
    
    dev_assign->session_id = 0;
    
    return (LIBSPDM_STATUS_IS_ERROR(status)) ? -1 : 0;
}
```

## 4. TDISP API 调用流程

### 4.1 TDISP 版本查询

```
pci_tdisp_get_version()
├── GET_VERSION 请求
└── 返回支持的版本列表
```

**代码位置：** `dev_tdisp_cmds.c:29`

```c
int dev_tdisp_get_version(struct dev_assign_info *dev_assign)
{
    libspdm_return_t status;
    pci_tdisp_version_response_mine_t response;
    size_t response_size;
    
    response_size = sizeof(response);
    
    status = pci_tdisp_get_version(
        dev_assign->spdm_context,
        dev_assign->session_id,
        &response_size,
        &response
    );
    
    return (LIBSPDM_STATUS_IS_ERROR(status)) ? -1 : 0;
}
```

### 4.2 TDISP 接口锁定

```
pci_tdisp_lock_interface()
├── GET_TDISP_CAPABILITIES  # 获取 TDISP 能力
├── SET_TDISP_CONFIGURATION # 设置 TDISP 配置
├── LOCK_INTERFACE          # 锁定接口
└── 返回锁定状态
```

**代码位置：** `dev_tdisp_cmds.c:78`

```c
int dev_tdisp_lock_interface(struct dev_assign_info *dev_assign)
{
    libspdm_return_t status;
    pci_tdisp_capabilities_response_t capabilities;
    pci_tdisp_lock_interface_response_t response;
    
    status = pci_tdisp_get_capabilities(
        dev_assign->spdm_context,
        dev_assign->session_id,
        &capabilities
    );
    
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        return -1;
    }
    
    status = pci_tdisp_set_configuration(
        dev_assign->spdm_context,
        dev_assign->session_id,
        &config
    );
    
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        return -1;
    }
    
    status = pci_tdisp_lock_interface(
        dev_assign->spdm_context,
        dev_assign->session_id,
        dev_assign->dev_granule,
        &response
    );
    
    return (LIBSPDM_STATUS_IS_ERROR(status)) ? -1 : 0;
}
```

### 4.3 TDISP 接口启动

```
pci_tdisp_start_interface()
├── START_INTERFACE 请求
└── 设备进入 RUN 状态
```

**代码位置：** `dev_tdisp_cmds.c:193`

```c
int dev_tdisp_start_interface(struct dev_assign_info *dev_assign)
{
    libspdm_return_t status;
    pci_tdisp_start_interface_response_t response;
    
    status = pci_tdisp_start_interface(
        dev_assign->spdm_context,
        dev_assign->session_id,
        &response
    );
    
    return (LIBSPDM_STATUS_IS_ERROR(status)) ? -1 : 0;
}
```

## 5. 完整流程序列图

### 5.1 设备直通完整流程

```
┌──────────┐     ┌──────────┐     ┌──────────┐
│   RMM    │     │ NS Host  │     │  Device  │
│(Requester)│     │(Forwarder)│     │(Responder)│
└────┬─────┘     └────┬─────┘     └────┬─────┘
     │                │                │
     │ ════════════ SPDM 连接阶段 ════════════
     │                │                │
     │ VERSION        │                │
     │───────────────►│───────────────►│
     │                │                │
     │◄───────────────│◄───────────────│ VERSION_RESPONSE
     │                │                │
     │ CAPABILITIES   │                │
     │───────────────►│───────────────►│
     │                │                │
     │◄───────────────│◄───────────────│ CAPABILITIES_RESPONSE
     │                │                │
     │ ALGORITHMS     │                │
     │───────────────►│───────────────►│
     │                │                │
     │◄───────────────│◄───────────────│ ALGORITHMS_RESPONSE
     │                │                │
     │ ════════════ 证书获取阶段 ════════════
     │                │                │
     │ GET_DIGESTS    │                │
     │───────────────►│───────────────►│
     │                │                │
     │◄───────────────│◄───────────────│ DIGESTS_RESPONSE
     │                │                │
     │ GET_CERTIFICATE│                │
     │───────────────►│───────────────►│
     │                │                │
     │◄───────────────│◄───────────────│ CERTIFICATE_RESPONSE
     │                │                │
     │ ════════════ 会话建立阶段 ════════════
     │                │                │
     │ KEY_EXCHANGE   │                │
     │───────────────►│───────────────►│
     │                │                │
     │◄───────────────│◄───────────────│ KEY_EXCHANGE_RSP
     │                │                │
     │ FINISH         │                │
     │───────────────►│───────────────►│
     │                │                │
     │◄───────────────│◄───────────────│ FINISH_RSP
     │                │                │
     │ [会话 ID: 0xXXXX]               │
     │                │                │
     │ ════════════ TDISP 锁定阶段 ════════════
     │                │                │
     │ GET_TDISP_VERSION              │
     │───────────────►│───────────────►│
     │                │                │
     │◄───────────────│◄───────────────│ TDISP_VERSION_RSP
     │                │                │
     │ GET_TDISP_CAPABILITIES         │
     │───────────────►│───────────────►│
     │                │                │
     │◄───────────────│◄───────────────│ TDISP_CAPABILITIES_RSP
     │                │                │
     │ SET_TDISP_CONFIGURATION        │
     │───────────────►│───────────────►│
     │                │                │
     │◄───────────────│◄───────────────│ TDISP_CONFIG_RSP
     │                │                │
     │ LOCK_INTERFACE │                │
     │───────────────►│───────────────►│
     │                │                │
     │◄───────────────│◄───────────────│ LOCK_INTERFACE_RSP
     │                │                │
     │ [设备状态: CONFIG_LOCKED]        │
     │                │                │
     │ ════════════ TDISP 启动阶段 ════════════
     │                │                │
     │ START_INTERFACE│                │
     │───────────────►│───────────────►│
     │                │                │
     │◄───────────────│◄───────────────│ START_INTERFACE_RSP
     │                │                │
     │ [设备状态: RUN]                  │
     │                │                │
     │ ════════════ 设备使用阶段 ════════════
     │                │                │
     │ [设备 DMA / MMIO 正常工作]       │
     │                │                │
     │ ════════════ 清理阶段 ════════════
     │                │                │
     │ STOP_INTERFACE │                │
     │───────────────►│───────────────►│
     │                │                │
     │◄───────────────│◄───────────────│ STOP_INTERFACE_RSP
     │                │                │
     │ END_SESSION    │                │
     │───────────────►│───────────────►│
     │                │                │
     │◄───────────────│◄───────────────│ END_SESSION_RSP
     │                │                │
```

### 5.2 TDISP 状态机

```
                    ┌─────────────────┐
                    │ CONFIG_UNLOCKED │
                    │     (0x0)       │
                    └────────┬────────┘
                             │
                    LOCK_INTERFACE
                             │
                             ▼
                    ┌─────────────────┐
                    │  CONFIG_LOCKED  │
                    │     (0x1)       │
                    └────────┬────────┘
                             │
                   START_INTERFACE
                             │
                             ▼
                    ┌─────────────────┐
                    │       RUN        │
                    │     (0x2)        │
                    └────────┬────────┘
                             │
                  STOP_INTERFACE /
                    END_SESSION
                             │
                             ▼
                    ┌─────────────────┐
                    │ CONFIG_UNLOCKED │
                    │     (0x0)       │
                    └─────────────────┘
```

## 6. 传输层设计

### 6.1 无传输头封装

TF-RMM 的一个关键设计特点是**不使用标准的 MCTP 或 PCI_DOE 传输头**，而是直接传递 SPDM 消息。

**标准方式 (带传输头)：**
```
┌───────────────┬───────────────┬─────────────┐
│  MCTP Header  │  SPDM Header  │   Payload   │
│   (4 bytes)   │   (4 bytes)   │  (variable) │
└───────────────┴───────────────┴─────────────┘
```

**TF-RMM 方式 (无传输头)：**
```
┌───────────────┬─────────────┐
│  SPDM Header  │   Payload   │
│   (4 bytes)   │  (variable) │
└───────────────┴─────────────┘
```

### 6.2 传输层实现

**代码位置：** `dev_assign_el0_app.c`

```c
libspdm_return_t spdm_transport_encode_message(
    void *context,
    const uint32_t *session_id,
    bool is_app_message,
    size_t message_size,
    void *message,
    size_t *transport_message_size,
    void **transport_message)
{
    *transport_message = message;
    *transport_message_size = message_size;
    return LIBSPDM_STATUS_SUCCESS;
}

libspdm_return_t spdm_transport_decode_message(
    void *context,
    uint32_t **session_id,
    bool is_app_message,
    size_t transport_message_size,
    void *transport_message,
    size_t *message_size,
    void **message)
{
    *message = transport_message;
    *message_size = transport_message_size;
    return LIBSPDM_STATUS_SUCCESS;
}
```

### 6.3 设计原因

1. **简化实现**：RMM 作为中间层，不需要处理复杂的传输协议
2. **性能优化**：减少封装/解封装开销
3. **安全隔离**：RMM 直接与硬件交互，不需要额外的传输层安全
4. **架构限制**：Realm 环境下可能无法访问完整的 PCIe 配置空间

## 7. 证书管理

### 7.1 证书获取策略

TF-RMM 采用**不存储证书链**的策略：

```c
status = libspdm_get_certificate(
    dev_assign->spdm_context,
    dev_assign->slot_id,
    &cert_chain_size,
    &cert_chain,
    NULL,                          // 不存储哈希
    NULL,
    NULL
);
```

### 7.2 设计原因

1. **内存限制**：RMM 运行在受限的内存环境中
2. **安全考虑**：证书链由 NS Host 缓存和验证，RMM 只存储哈希
3. **信任链**：证书验证在 NS Host 完成，RMM 信任已验证的结果

## 8. IDE 密钥管理 (可选)

TF-RMM 支持 IDE (Integrity and Data Encryption) 密钥编程，用于 PCIe 链路加密：

```
IDE KM 流程:
├── pci_ide_km_query()
├── pci_ide_km_set_key()
├── pci_ide_km_set_key()
└── pci_ide_km_kp_ack()
```

**代码位置：** `dev_assign_ide_cmds.c`

IDE KM 是可选功能，在基本设备直通流程中不是必需的。

## 9. 与标准 SPDM/TDISP 流程的差异

| 方面 | 标准流程 | TF-RMM 实现 |
|------|----------|-------------|
| 传输层 | MCTP/PCI_DOE | 无传输头，直接传递 |
| 证书存储 | 完整证书链 | 不存储，仅用哈希 |
| 会话管理 | Requester 管理 | RMM 代理管理 |
| 消息转发 | 直接通信 | NS Host 转发 |
| SPDM 版本 | 1.2+ | 1.2 (CMA_SPDM_VERSION) |
| IDE KM | 可选 | 支持，用于链路加密 |

## 10. API 调用汇总

### 10.1 SPDM API 列表

| API | 文件位置 | 行号 | 功能 |
|-----|----------|------|------|
| `libspdm_init_connection()` | dev_assign_cmds.c | 93 | 初始化 SPDM 连接 |
| `libspdm_get_certificate()` | dev_assign_cmds.c | 111 | 获取设备证书 |
| `libspdm_start_session()` | dev_assign_cmds.c | 170 | 建立安全会话 |
| `libspdm_stop_session()` | dev_assign_cmds.c | 283 | 停止安全会话 |

### 10.2 TDISP API 列表

| API | 文件位置 | 行号 | 功能 |
|-----|----------|------|------|
| `pci_tdisp_get_version()` | dev_tdisp_cmds.c | 29 | 获取 TDISP 版本 |
| `pci_tdisp_lock_interface()` | dev_tdisp_cmds.c | 78 | 锁定设备接口 |
| `pci_tdisp_start_interface()` | dev_tdisp_cmds.c | 193 | 启动设备接口 |

### 10.3 IDE KM API 列表

| API | 文件位置 | 功能 |
|-----|----------|------|
| `pci_ide_km_query()` | dev_assign_ide_cmds.c | 查询 IDE 能力 |
| `pci_ide_km_set_key()` | dev_assign_ide_cmds.c | 设置 IDE 密钥 |
| `pci_ide_km_kp_ack()` | dev_assign_ide_cmds.c | 确认密钥编程 |

## 11. 参考链接

- TF-RMM 项目：https://git.trustedfirmware.org/TF-RMM/tf-rmm.git/
- libspdm 库：https://github.com/DMTF/libspdm
- SPDM 规范：DMTF DSP0274
- TDISP 规范：PCIe TDISP ECN