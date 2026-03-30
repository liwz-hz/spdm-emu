/**
 *  Copyright Notice:
 *  Copyright 2021-2025 DMTF. All rights reserved.
 *  License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/spdm-emu/blob/main/LICENSE.md
 **/

#include "rmm_demo_context.h"
#include "rmm_transport.h"
#include "internal/libspdm_requester_lib.h"
#include "library/spdm_responder_lib.h"
#include "hal/library/debuglib.h"
#include "hal/library/memlib.h"
#include "library/pci_tdisp_requester_lib.h"
#include "library/pci_tdisp_common_lib.h"
#include "industry_standard/pci_tdisp.h"
#include "industry_standard/spdm.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

static rmm_transport_context_t m_transport_context;
static rmm_demo_context_t m_requester_context;
static rmm_demo_context_t m_responder_context;

static void rmm_demo_log(const char *phase, const char *message)
{
    printf("RMM_DEMO [%s]: %s\n", phase, message);
}

static void rmm_demo_log_success(const char *phase, const char *message)
{
    printf("RMM_DEMO [%s]: SUCCESS - %s\n", phase, message);
}

static void rmm_demo_log_error(const char *phase, const char *message, libspdm_return_t status)
{
    printf("RMM_DEMO [%s]: ERROR - %s (status=0x%x)\n", phase, message, status);
}

int main(void)
{
    libspdm_return_t status;
    void *requester_spdm_ctx;
    uint32_t session_id;
    uint8_t heartbeat_period;
    uint8_t slot_id;
    pci_tdisp_interface_id_t interface_id;
    pci_tdisp_requester_capabilities_t req_caps;
    pci_tdisp_responder_capabilities_t rsp_caps;
    pci_tdisp_lock_interface_param_t lock_interface_param;
    uint8_t start_interface_nonce[PCI_TDISP_START_INTERFACE_NONCE_SIZE];
    uint8_t tdi_state;

    printf("========================================\n");
    printf("RMM Style SPDM/TDISP Demo\n");
    printf("========================================\n\n");

    rmm_demo_log("INIT", "Initializing transport layer");
    rmm_transport_init(&m_transport_context);

    rmm_demo_log("INIT", "Initializing requester context");
    if (!rmm_demo_context_init(&m_requester_context, &m_transport_context, true)) {
        rmm_demo_log_error("INIT", "Failed to initialize requester context", 0);
        return -1;
    }

    rmm_demo_log("INIT", "Initializing responder context");
    if (!rmm_demo_context_init(&m_responder_context, &m_transport_context, false)) {
        rmm_demo_log_error("INIT", "Failed to initialize responder context", 0);
        rmm_demo_context_cleanup(&m_requester_context);
        return -1;
    }

    rmm_demo_log_success("INIT", "All contexts initialized");

    requester_spdm_ctx = m_requester_context.spdm_context;

    printf("\n----------------------------------------\n");
    printf("Phase 1: SPDM Connection (libspdm_init_connection)\n");
    printf("----------------------------------------\n\n");

    rmm_demo_log("SPDM_CONNECT", "Calling libspdm_init_connection(get_version_only=false)");

    status = libspdm_init_connection(requester_spdm_ctx, false);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        rmm_demo_log_error("SPDM_CONNECT", "libspdm_init_connection failed", status);
        rmm_demo_context_cleanup(&m_requester_context);
        rmm_demo_context_cleanup(&m_responder_context);
        rmm_transport_cleanup(&m_transport_context);
        return -1;
    }

    rmm_demo_log_success("SPDM_CONNECT", "GET_VERSION -> GET_CAPABILITIES -> NEGOTIATE_ALGORITHMS completed");

    printf("\n----------------------------------------\n");
    printf("Phase 2: Certificate Retrieval\n");
    printf("----------------------------------------\n\n");

    rmm_demo_log("CERT", "Calling libspdm_get_digest()");

    slot_id = 0;
    uint8_t slot_mask;
    uint8_t total_digest_buffer[LIBSPDM_MAX_HASH_SIZE * SPDM_MAX_SLOT_COUNT];
    libspdm_zero_mem(total_digest_buffer, sizeof(total_digest_buffer));

    status = libspdm_get_digest(requester_spdm_ctx, NULL, &slot_mask, total_digest_buffer);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        rmm_demo_log_error("CERT", "libspdm_get_digest failed", status);
        rmm_demo_context_cleanup(&m_requester_context);
        rmm_demo_context_cleanup(&m_responder_context);
        rmm_transport_cleanup(&m_transport_context);
        return -1;
    }

    printf("RMM_DEMO [CERT]: GET_DIGEST successful, slot_mask=0x%x\n", slot_mask);
    rmm_demo_log("CERT", "Calling libspdm_get_certificate()");

    #define RMM_MAX_CERT_CHAIN_SIZE 0x1000
    uint8_t cert_chain[RMM_MAX_CERT_CHAIN_SIZE];
    size_t cert_chain_size = sizeof(cert_chain);
    libspdm_zero_mem(cert_chain, sizeof(cert_chain));

    status = libspdm_get_certificate(requester_spdm_ctx, NULL, slot_id,
                                     &cert_chain_size, cert_chain);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        rmm_demo_log_error("CERT", "libspdm_get_certificate failed", status);
        rmm_demo_context_cleanup(&m_requester_context);
        rmm_demo_context_cleanup(&m_responder_context);
        rmm_transport_cleanup(&m_transport_context);
        return -1;
    }

    printf("RMM_DEMO [CERT]: GET_CERTIFICATE successful, cert_chain_size=%zu bytes\n", cert_chain_size);
    rmm_demo_log_success("CERT", "DIGEST -> CERTIFICATE completed");

    printf("\n----------------------------------------\n");
    printf("Phase 3: Secure Session (libspdm_start_session)\n");
    printf("----------------------------------------\n\n");

    rmm_demo_log("SESSION", "Calling libspdm_start_session()");

    heartbeat_period = 0;
    session_id = 0;

    status = libspdm_start_session(requester_spdm_ctx, false,
                                   NULL, 0,
                                   SPDM_CHALLENGE_REQUEST_NO_MEASUREMENT_SUMMARY_HASH,
                                   slot_id, 0,
                                   &session_id, &heartbeat_period, NULL);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        rmm_demo_log_error("SESSION", "libspdm_start_session failed", status);
        rmm_demo_context_cleanup(&m_requester_context);
        rmm_demo_context_cleanup(&m_responder_context);
        rmm_transport_cleanup(&m_transport_context);
        return -1;
    }

    m_requester_context.session_id = session_id;
    printf("RMM_DEMO [SESSION]: Session ID = 0x%x\n", session_id);
    rmm_demo_log_success("SESSION", "KEY_EXCHANGE -> FINISH completed");

    printf("\n----------------------------------------\n");
    printf("Phase 3: TDISP GET_VERSION\n");
    printf("----------------------------------------\n\n");

    rmm_demo_log("TDISP", "Calling pci_tdisp_get_version()");

    interface_id.function_id = 0xbeef;
    interface_id.reserved = 0;

    status = pci_tdisp_get_version(NULL, requester_spdm_ctx, &session_id, &interface_id);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        rmm_demo_log_error("TDISP", "TDISP GET_VERSION failed", status);
        goto cleanup_session;
    }

    rmm_demo_log_success("TDISP", "GET_VERSION completed");

    printf("\n----------------------------------------\n");
    printf("Phase 4: TDISP GET_CAPABILITIES\n");
    printf("----------------------------------------\n\n");

    rmm_demo_log("TDISP", "Calling pci_tdisp_get_capabilities()");

    req_caps.tsm_caps = 0;
    libspdm_zero_mem(&rsp_caps, sizeof(rsp_caps));

    status = pci_tdisp_get_capabilities(NULL, requester_spdm_ctx, &session_id,
                                        &interface_id, &req_caps, &rsp_caps);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        rmm_demo_log_error("TDISP", "TDISP GET_CAPABILITIES failed", status);
        goto cleanup_session;
    }

    rmm_demo_log_success("TDISP", "GET_CAPABILITIES completed");

    printf("\n----------------------------------------\n");
    printf("Phase 5: TDISP GET_INTERFACE_STATE\n");
    printf("----------------------------------------\n\n");

    rmm_demo_log("TDISP", "Calling pci_tdisp_get_interface_state()");

    status = pci_tdisp_get_interface_state(NULL, requester_spdm_ctx, &session_id,
                                           &interface_id, &tdi_state);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        rmm_demo_log_error("TDISP", "TDISP GET_INTERFACE_STATE failed", status);
        goto cleanup_session;
    }

    printf("RMM_DEMO [TDISP]: TDI state = 0x%x (CONFIG_UNLOCKED=0, CONFIG_LOCKED=1, RUN=2)\n",
           tdi_state);
    rmm_demo_log_success("TDISP", "GET_INTERFACE_STATE completed");

    printf("\n----------------------------------------\n");
    printf("Phase 6: TDISP LOCK_INTERFACE\n");
    printf("----------------------------------------\n\n");

    rmm_demo_log("TDISP", "Calling pci_tdisp_lock_interface()");

    libspdm_zero_mem(&lock_interface_param, sizeof(lock_interface_param));
    lock_interface_param.flags = rsp_caps.lock_interface_flags_supported;
    lock_interface_param.default_stream_id = 0;
    lock_interface_param.mmio_reporting_offset = 0xD0000000;
    lock_interface_param.bind_p2p_address_mask = 0;
    libspdm_zero_mem(start_interface_nonce, sizeof(start_interface_nonce));

    status = pci_tdisp_lock_interface(NULL, requester_spdm_ctx, &session_id, &interface_id,
                                       &lock_interface_param, start_interface_nonce);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        rmm_demo_log_error("TDISP", "TDISP LOCK_INTERFACE failed", status);
        goto cleanup_session;
    }

    rmm_demo_log_success("TDISP", "LOCK_INTERFACE completed");

    status = pci_tdisp_get_interface_state(NULL, requester_spdm_ctx, &session_id,
                                           &interface_id, &tdi_state);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        rmm_demo_log_error("TDISP", "GET_INTERFACE_STATE after lock failed", status);
        goto cleanup_session;
    }
    printf("RMM_DEMO [TDISP]: TDI state after lock = 0x%x\n", tdi_state);

    printf("\n----------------------------------------\n");
    printf("Phase 7: TDISP START_INTERFACE\n");
    printf("----------------------------------------\n\n");

    rmm_demo_log("TDISP", "Calling pci_tdisp_start_interface()");

    status = pci_tdisp_start_interface(NULL, requester_spdm_ctx, &session_id,
                                       &interface_id, start_interface_nonce);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        rmm_demo_log_error("TDISP", "TDISP START_INTERFACE failed", status);
        goto cleanup_session;
    }

    rmm_demo_log_success("TDISP", "START_INTERFACE completed");

    status = pci_tdisp_get_interface_state(NULL, requester_spdm_ctx, &session_id,
                                           &interface_id, &tdi_state);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        rmm_demo_log_error("TDISP", "GET_INTERFACE_STATE after start failed", status);
        goto cleanup_session;
    }
    printf("RMM_DEMO [TDISP]: TDI state after start = 0x%x (RUN state)\n", tdi_state);

    printf("\n----------------------------------------\n");
    printf("Phase 8: TDISP STOP_INTERFACE\n");
    printf("----------------------------------------\n\n");

    rmm_demo_log("TDISP", "Calling pci_tdisp_stop_interface()");

    status = pci_tdisp_stop_interface(NULL, requester_spdm_ctx, &session_id, &interface_id);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        rmm_demo_log_error("TDISP", "TDISP STOP_INTERFACE failed", status);
        goto cleanup_session;
    }

    rmm_demo_log_success("TDISP", "STOP_INTERFACE completed");

    status = pci_tdisp_get_interface_state(NULL, requester_spdm_ctx, &session_id,
                                           &interface_id, &tdi_state);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        rmm_demo_log_error("TDISP", "GET_INTERFACE_STATE after stop failed", status);
        goto cleanup_session;
    }
    printf("RMM_DEMO [TDISP]: TDI state after stop = 0x%x\n", tdi_state);

cleanup_session:
    printf("\n----------------------------------------\n");
    printf("Phase 9: Session Cleanup (libspdm_stop_session)\n");
    printf("----------------------------------------\n\n");

    rmm_demo_log("CLEANUP", "Calling libspdm_stop_session()");

    status = libspdm_stop_session(requester_spdm_ctx, session_id, 0);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        rmm_demo_log_error("CLEANUP", "libspdm_stop_session failed", status);
    } else {
        rmm_demo_log_success("CLEANUP", "END_SESSION completed");
    }

    rmm_demo_context_cleanup(&m_requester_context);
    rmm_demo_context_cleanup(&m_responder_context);
    rmm_transport_cleanup(&m_transport_context);

    printf("\n========================================\n");
    printf("RMM Style Demo Complete\n");
    printf("========================================\n\n");

    return 0;
}