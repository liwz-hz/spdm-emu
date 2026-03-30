# SPDM 进程内演示程序 (spdm_inproc_demo)

## 概述

`spdm_inproc_demo` 是一个单进程 SPDM 演示应用程序，它在同一个进程内同时运行 SPDM 请求者 (Requester) 和响应者 (Responder)，通过内存缓冲区进行消息传递，完成完整的 SPDM 安全会话创建流程，并在安全会话上承载 TDISP 设备锁定流程。

## 一、架构设计

### 1.1 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                     spdm_inproc_demo (单进程)                    │
│                                                                  │
│  ┌──────────────────┐         ┌──────────────────┐              │
│  │   Requester      │         │   Responder      │              │
│  │   Context        │         │   Context        │              │
│  │                  │         │                  │              │
│  │  - SPDM Context  │         │  - SPDM Context  │              │
│  │  - Sender Buffer │         │  - Sender Buffer │              │
│  │  - Receiver Buf  │         │  - Receiver Buf  │              │
│  └─────────┬────────┘         └─────────┬────────┘              │
│            │                            │                        │
│            │   ┌────────────────────┐   │                        │
│            │   │ Transport Context  │   │                        │
│            └───┤                    │───┤                        │
│                │ req_to_rsp_buffer  │   │                        │
│                │ rsp_to_req_buffer  │   │                        │
│                └────────────────────┘                            │
│                                                                  │
│  消息流向:                                                       │
│  Requester发送 → req_to_rsp_buffer → Responder接收              │
│  Responder发送 → rsp_to_req_buffer → Requester接收              │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 核心组件

| 组件 | 文件 | 功能 |
|------|------|------|
| `inproc_transport_context_t` | `internal_transport.h` | 传输层上下文，管理双向消息缓冲区 |
| `inproc_spdm_context_t` | `spdm_context.h` | SPDM上下文封装，包含发送/接收缓冲区 |
| `internal_transport.c` | `internal_transport.c` | 实现消息传递的I/O回调函数 |
| `spdm_context.c` | `spdm_context.c` | SPDM上下文初始化和TDISP响应者集成 |
| `main.c` | `main.c` | 主程序流程控制 |

### 1.3 消息传递机制

进程内传输使用双缓冲区机制：

```
┌─────────────────────────────────────────────────────────────┐
│                  inproc_transport_context_t                  │
│                                                              │
│   req_to_rsp_buffer                rsp_to_req_buffer        │
│  ┌──────────────────┐             ┌──────────────────┐      │
│  │ buffer[0x2000]   │             │ buffer[0x2000]   │      │
│  │ buffer_size      │             │ buffer_size      │      │
│  │ has_message      │             │ has_message      │      │
│  └──────────────────┘             └──────────────────┐      │
│                                                              │
│  Requester发送 → 写入req_to_rsp → 触发Responder处理          │
│  Responder发送 → 写入rsp_to_req → Requester读取              │
└─────────────────────────────────────────────────────────────┘
```

关键函数调用链：

1. Requester调用 `libspdm_get_version()` 等API
2. API内部调用 `inproc_requester_send_message()` 将消息写入 `req_to_rsp_buffer`
3. `inproc_requester_send_message()` 调用 `inproc_trigger_responder_processing()`
4. `inproc_trigger_responder_processing()` 调用 `libspdm_responder_dispatch_message()`
5. Responder处理消息后调用 `inproc_responder_send_message()` 将响应写入 `rsp_to_req_buffer`
6. Requester调用 `inproc_requester_receive_message()` 读取响应

## 二、设计思路

### 2.1 为什么需要进程内演示？

传统的 SPDM 演示程序（如 `spdm_requester_emu` 和 `spdm_responder_emu`）需要：
- 启动两个独立进程
- 通过网络socket或PCI_DOE传输层进行通信
- 需要先启动Responder，再启动Requester

进程内演示的优势：
- 无需网络配置，简化测试环境
- 单进程执行，便于调试和问题定位
- 消息传递在内存中完成，无I/O延迟
- 可作为单元测试的基础框架

### 2.2 传输层选择

**关键决策：使用 MCTP 传输层，而非 NONE 传输层**

原因：
- NONE 传输层 (`spdm_transport_none_lib`) **不支持安全消息**
- KEY_EXCHANGE 和 FINISH 之后的消息必须通过加密的安全会话发送
- MCTP 传输层调用 `libspdm_encode_secured_message` 进行消息加密

```
MCTP传输层调用链:
libspdm_transport_mctp_encode_message()
  → libspdm_encode_secured_message()  // 加密消息
  → 添加MCTP传输头
```

### 2.3 Session ID 计算

进程内演示使用固定的 Session ID：

```
req_session_id = 0xFFFF  (Requester分配，索引0)
rsp_session_id = 0xFFFF  (Responder分配，索引0)
最终 session_id = (0xFFFF << 16) | 0xFFFF = 0xFFFFFFFF
```

这是符合 SPDM 规范的合法 Session ID。

### 2.4 缓冲区布局设计

缓冲区采用连续内存布局，预留头部和尾部空间：

```
┌─────────────────────────────────────────────────────────────┐
│                  Sender/Receiver Buffer                      │
│                                                              │
│  ┌──────────┐───────────────────────────┐──────────┐        │
│  │ Header   │    SPDM Message Area      │   Tail   │        │
│  │ 64 bytes │    0x1200 bytes (max)     │ 64 bytes │        │
│  └──────────┘───────────────────────────┘──────────┘        │
│                                                              │
│  总大小 = 64 + 0x1200 + 64 = 0x12C8 bytes                    │
└─────────────────────────────────────────────────────────────┘
```

### 2.5 TDISP 集成

TDISP (Trusted Device Interface Security Protocol) 通过 Vendor Defined SPDM 消息承载：

```
TDISP消息流程:
1. pci_tdisp_get_version()      → 获取TDISP版本
2. pci_tdisp_get_capabilities() → 交换能力
3. pci_tdisp_get_interface_state() → 检查TDI状态 (CONFIG_UNLOCKED=0x0)
4. pci_tdisp_lock_interface()   → 锁定接口，获取nonce
5. pci_tdisp_get_interface_state() → 验证状态 (CONFIG_LOCKED=0x1)
```

TDISP响应者通过 `pci_doe_register_vendor_response_func()` 注册回调处理。

## 三、编译方法

### 3.1 依赖关系

**重要：本demo无法脱离spdm-emu工程独立编译**

依赖项：
```
spdm_inproc_demo
├── spdm-emu 工程 (父工程)
│   ├── libspdm/ (核心SPDM库，git submodule)
│   │   ├── library/spdm_*_lib
│   │   ├── os_stub/*_lib
│   │   └── include/
│   ├── library/pci_*_lib (PCI DOE/TDISP/IDE KM库)
│   └── include/
└── 加密库 (mbedtls 或 openssl)
```

### 3.2 编译步骤

#### Linux (GCC)

```bash
# 1. 进入spdm-emu根目录
cd /path/to/spdm-emu

# 2. 初始化子模块
git submodule update --init --recursive

# 3. 创建构建目录
mkdir build && cd build

# 4. 配置CMake
cmake -DARCH=x64 -DTOOLCHAIN=GCC -DTARGET=Release -DCRYPTO=mbedtls ..

# 5. 复制测试证书
make copy_sample_key

# 6. 编译整个工程（包括spdm_inproc_demo）
make -j$(nproc)

# 或者只编译spdm_inproc_demo
make spdm_inproc_demo -j$(nproc)
```

#### 编译选项说明

| 选项 | 可选值 | 说明 |
|------|--------|------|
| `ARCH` | x64, ia32, arm, aarch64, riscv32, riscv64 | 目标架构 |
| `TOOLCHAIN` | GCC, CLANG | 编译工具链 |
| `TARGET` | Debug, Release | 构建类型 |
| `CRYPTO` | mbedtls, openssl | 加密库 |
| `DEVICE` | sample, tpm | 设备类型 |

#### 输出位置

编译完成后，可执行文件位于：
```
build/bin/spdm_inproc_demo      # 主程序
build/bin/test_spdm_inproc      # 测试程序
```

### 3.3 为什么无法独立编译？

1. **库依赖**：依赖 libspdm 的多个静态库：
   - `spdm_requester_lib`, `spdm_responder_lib`, `spdm_common_lib`
   - `spdm_secured_message_lib`, `spdm_transport_mctp_lib`
   - `pci_tdisp_*_lib`, `pci_doe_*_lib`

2. **头文件依赖**：依赖 libspdm 和 spdm-emu 的头文件路径

3. **加密库依赖**：需要 mbedtls 或 openssl 的完整编译

如果需要独立编译，必须：
- 提供预编译的所有静态库
- 正确配置所有头文件路径
- 链接所有必要的库

## 四、运行与测试方法

### 4.1 运行主程序

```bash
cd build/bin
./spdm_inproc_demo
```

### 4.2 预期输出

```
INPROC_DEMO: Initializing SPDM In-Process Demo
INPROC_DEMO: SPDM contexts initialized successfully
INPROC_DEMO: Memory message bridge ready
INPROC_DEMO: Starting GET_VERSION exchange
INPROC REQ->RSP: Request code=0x84, version=0x10, size=4
INPROC REQ<-RSP: Response code=0x84, version=0x10, size=12
INPROC_DEMO: GET_VERSION successful, received 4 versions:
  Version 0: 0x12 (major=1, minor=2)
  Version 1: 0x11 (major=1, minor=1)
  ...
INPROC_DEMO: Starting GET_CAPABILITIES exchange
...
INPROC_DEMO: KEY_EXCHANGE successful, session_id=0xffffffff
INPROC_DEMO: FINISH successful, secure session created
INPROC_DEMO: Starting TDISP LOCK_INTERFACE flow
INPROC_DEMO: TDI state before lock: 0x0
INPROC_DEMO: TDISP LOCK_INTERFACE successful
INPROC_DEMO: TDI state after lock: 0x1
INPROC_DEMO: TDISP LOCK_INTERFACE flow completed successfully
INPROC_DEMO: SPDM In-Process Demo completed
```

### 4.3 运行测试程序

```bash
cd build/bin
./test_spdm_inproc
```

测试程序验证：
- SPDM 安全会话创建成功
- TDISP LOCK_INTERFACE 流程完成
- 程序正常退出

### 4.4 协议流程完整序列

```
SPDM协商阶段:
1. GET_VERSION        → 获取版本列表
2. GET_CAPABILITIES   → 交换能力
3. NEGOTIATE_ALGORITHMS → 协商算法
4. GET_DIGEST         → 获取证书摘要
5. GET_CERTIFICATE    → 获取证书链
6. CHALLENGE          → 身份认证
7. KEY_EXCHANGE       → 密钥交换，创建session_id
8. FINISH             → 完成会话建立

TDISP锁定阶段 (在安全会话上):
9. TDISP GET_VERSION
10. TDISP GET_CAPABILITIES
11. TDISP GET_INTERFACE_STATE → 状态=0x0 (CONFIG_UNLOCKED)
12. TDISP LOCK_INTERFACE
13. TDISP GET_INTERFACE_STATE → 状态=0x1 (CONFIG_LOCKED)
```

## 五、代码结构

### 5.1 目录结构

```
spdm_inproc_demo/
├── CMakeLists.txt           # 构建配置
├── README_zh.md             # 本文档
├── include/
│   ├── spdm_context.h       # SPDM上下文定义
│   └── internal_transport.h # 传输层接口定义
├── src/
│   ├── main.c               # 主程序入口
│   ├── spdm_context.c       # SPDM上下文初始化
│   ├── internal_transport.c # 传输层实现
│   └── support.c            # 辅助函数
└── test/
    ├── CMakeLists.txt       # 测试构建配置
    └── test_spdm_inproc.c   # 测试代码
```

### 5.2 关键数据结构

#### inproc_transport_context_t

```c
typedef struct {
    inproc_message_buffer_t req_to_rsp_buffer;  // Requester→Responder消息
    inproc_message_buffer_t rsp_to_req_buffer;  // Responder→Requester消息
    void *requester_context;                    // Requester上下文指针
    void *responder_context;                    // Responder上下文指针
} inproc_transport_context_t;
```

#### inproc_spdm_context_t

```c
typedef struct {
    void *spdm_context;                         // libspdm上下文
    void *scratch_buffer;                       // 临时缓冲区
    size_t scratch_buffer_size;
    uint8_t sender_buffer[0x12C8];              // 发送缓冲区
    uint8_t receiver_buffer[0x12C8];            // 接收缓冲区
    bool sender_buffer_acquired;
    bool receiver_buffer_acquired;
    bool is_requester;                          // 是否为Requester
    inproc_transport_context_t *transport_context;
} inproc_spdm_context_t;
```

### 5.3 关键函数

| 函数 | 作用 |
|------|------|
| `inproc_transport_init()` | 初始化传输层上下文 |
| `inproc_spdm_context_init()` | 初始化SPDM上下文 |
| `inproc_requester_send_message()` | Requester发送消息回调 |
| `inproc_requester_receive_message()` | Requester接收消息回调 |
| `inproc_responder_send_message()` | Responder发送消息回调 |
| `inproc_responder_receive_message()` | Responder接收消息回调 |
| `inproc_trigger_responder_processing()` | 触发Responder处理消息 |

## 六、扩展与定制

### 6.1 添加新的SPDM消息类型

在 `main.c` 中添加对应的调用：

```c
// 例如添加GET_MEASUREMENT
status = libspdm_get_measurement(requester_spdm_ctx, ...);
```

### 6.2 修改TDISP参数

修改 `interface_id` 和 `lock_interface_param`：

```c
interface_id.function_id = 0x1234;  // 修改功能ID
lock_interface_param.mmio_reporting_offset = 0xE0000000;  // 修改MMIO偏移
```

### 6.3 启用详细日志

在编译时使用 Debug 目标：

```bash
cmake -DTARGET=Debug ...
```

## 七、常见问题

### Q1: 编译时找不到头文件？

确保已初始化 git submodule：
```bash
git submodule update --init --recursive
```

### Q2: 运行时证书验证失败？

确保已复制测试证书：
```bash
make copy_sample_key
```

### Q3: KEY_EXCHANGE 失败？

检查 Responder 能力配置，必须包含 `ENCRYPT_CAP` 和 `MAC_CAP`。

### Q4: 如何在其他项目中使用？

本demo作为spdm-emu的子项目编译。若需独立使用，需要：
1. 提取源代码文件
2. 配置所有依赖库和头文件路径
3. 手动编写CMakeLists.txt或Makefile

## 八、参考资料

- [libspdm GitHub](https://github.com/DMTF/libspdm)
- [SPDM Specification (DMTF)](https://www.dmtf.org/sites/default/files/standards/documents/DSP0274_1.2.1.pdf)
- [PCI TDISP Specification](https://www.dmtf.org/sites/default/files/standards/documents/DSP0275_1.0.0.pdf)
- [spdm-emu GitHub](https://github.com/DMTF/spdm-emu)