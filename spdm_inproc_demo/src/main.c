/**
 *  Copyright Notice:
 *  Copyright 2021-2025 DMTF. All rights reserved.
 *  License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/spdm-emu/blob/main/LICENSE.md
 **/

#include "spdm_context.h"
#include "internal_transport.h"
#include "internal/libspdm_requester_lib.h"
#include "library/spdm_responder_lib.h"
#include "hal/library/debuglib.h"
#include "hal/library/memlib.h"
#include "library/pci_tdisp_requester_lib.h"
#include "library/pci_tdisp_common_lib.h"
#include "industry_standard/pci_tdisp.h"
#include "stdio.h"
#include "stdlib.h"

static inproc_transport_context_t m_transport_context;
static inproc_spdm_context_t m_requester_context;
static inproc_spdm_context_t m_responder_context;

void inproc_demo_log(const char *message)
{
    printf("INPROC_DEMO: %s\n", message);
}

int main(void)
{
    bool result;
    libspdm_return_t status;
    void *requester_spdm_ctx;
    uint8_t version_count;
    spdm_version_number_t versions[8];
    uint8_t slot_mask;
    uint8_t slot_id;
    uint8_t total_digest_buffer[LIBSPDM_MAX_HASH_SIZE * SPDM_MAX_SLOT_COUNT];
    size_t cert_chain_size;
    #define INPROC_MAX_CERT_CHAIN_SIZE 0x1000

    uint8_t cert_chain[INPROC_MAX_CERT_CHAIN_SIZE];
    uint8_t measurement_hash[LIBSPDM_MAX_HASH_SIZE];
    uint8_t heartbeat_period;
    uint8_t req_slot_id_param;
    uint32_t session_id;
    pci_tdisp_interface_id_t interface_id;
    pci_tdisp_requester_capabilities_t req_caps;
    pci_tdisp_responder_capabilities_t rsp_caps;
    pci_tdisp_lock_interface_param_t lock_interface_param;
    uint8_t start_interface_nonce[PCI_TDISP_START_INTERFACE_NONCE_SIZE];
    uint8_t tdi_state;

    inproc_demo_log("Initializing SPDM In-Process Demo");

    inproc_transport_init(&m_transport_context);

    result = inproc_spdm_context_init(&m_requester_context, &m_transport_context, true);
    if (!result) {
        inproc_demo_log("Failed to initialize requester context");
        return -1;
    }

    result = inproc_spdm_context_init(&m_responder_context, &m_transport_context, false);
    if (!result) {
        inproc_demo_log("Failed to initialize responder context");
        inproc_spdm_context_cleanup(&m_requester_context);
        return -1;
    }

    inproc_demo_log("SPDM contexts initialized successfully");
    inproc_demo_log("Memory message bridge ready");
    inproc_demo_log("Requester -> Responder buffer initialized");
    inproc_demo_log("Responder -> Requester buffer initialized");

    requester_spdm_ctx = m_requester_context.spdm_context;
    inproc_demo_log("Starting GET_VERSION exchange");

    version_count = 8;
    libspdm_zero_mem(versions, sizeof(versions));

    status = libspdm_get_version(requester_spdm_ctx, &version_count, versions);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        printf("INPROC_DEMO: GET_VERSION failed with status 0x%x\n", status);
        inproc_spdm_context_cleanup(&m_requester_context);
        inproc_spdm_context_cleanup(&m_responder_context);
        inproc_transport_cleanup(&m_transport_context);
        return -1;
    }

    printf("INPROC_DEMO: GET_VERSION successful, received %u versions:\n", version_count);
    for (size_t i = 0; i < version_count && i < 8; i++) {
        printf("  Version %zu: 0x%x (major=%u, minor=%u)\n",
               i, versions[i],
               (versions[i] >> SPDM_VERSION_NUMBER_SHIFT_BIT) & 0xF,
               (versions[i] >> (SPDM_VERSION_NUMBER_SHIFT_BIT - 4)) & 0xF);
    }

    inproc_demo_log("GET_VERSION exchange completed successfully");

    inproc_demo_log("Starting GET_CAPABILITIES exchange");

    status = libspdm_get_capabilities(requester_spdm_ctx);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        printf("INPROC_DEMO: GET_CAPABILITIES failed with status 0x%x\n", status);
        inproc_spdm_context_cleanup(&m_requester_context);
        inproc_spdm_context_cleanup(&m_responder_context);
        inproc_transport_cleanup(&m_transport_context);
        return -1;
    }

    inproc_demo_log("GET_CAPABILITIES exchange completed successfully");

    inproc_demo_log("Starting NEGOTIATE_ALGORITHMS exchange");

    status = libspdm_negotiate_algorithms(requester_spdm_ctx);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        printf("INPROC_DEMO: NEGOTIATE_ALGORITHMS failed with status 0x%x\n", status);
        inproc_spdm_context_cleanup(&m_requester_context);
        inproc_spdm_context_cleanup(&m_responder_context);
        inproc_transport_cleanup(&m_transport_context);
        return -1;
    }

    inproc_demo_log("NEGOTIATE_ALGORITHMS exchange completed successfully");

    inproc_demo_log("Starting GET_DIGEST exchange");

    slot_id = 0;
    libspdm_zero_mem(total_digest_buffer, sizeof(total_digest_buffer));

    status = libspdm_get_digest(requester_spdm_ctx, NULL, &slot_mask, total_digest_buffer);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        printf("INPROC_DEMO: GET_DIGEST failed with status 0x%x\n", status);
        inproc_spdm_context_cleanup(&m_requester_context);
        inproc_spdm_context_cleanup(&m_responder_context);
        inproc_transport_cleanup(&m_transport_context);
        return -1;
    }

    printf("INPROC_DEMO: GET_DIGEST successful, slot_mask=0x%x\n", slot_mask);

    inproc_demo_log("GET_DIGEST exchange completed successfully");

    inproc_demo_log("Starting GET_CERTIFICATE exchange");

    cert_chain_size = sizeof(cert_chain);
    libspdm_zero_mem(cert_chain, sizeof(cert_chain));

    status = libspdm_get_certificate(requester_spdm_ctx, NULL, slot_id,
                                     &cert_chain_size, cert_chain);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        printf("INPROC_DEMO: GET_CERTIFICATE failed with status 0x%x\n", status);
        inproc_spdm_context_cleanup(&m_requester_context);
        inproc_spdm_context_cleanup(&m_responder_context);
        inproc_transport_cleanup(&m_transport_context);
        return -1;
    }

    printf("INPROC_DEMO: GET_CERTIFICATE successful, cert_chain_size=%zu bytes\n",
           cert_chain_size);

    inproc_demo_log("GET_CERTIFICATE exchange completed successfully");

    inproc_demo_log("Starting CHALLENGE exchange");

    libspdm_zero_mem(measurement_hash, sizeof(measurement_hash));

    status = libspdm_challenge(requester_spdm_ctx, NULL, slot_id,
                               SPDM_CHALLENGE_REQUEST_NO_MEASUREMENT_SUMMARY_HASH,
                               measurement_hash, &slot_mask);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        printf("INPROC_DEMO: CHALLENGE failed with status 0x%x\n", status);
        inproc_spdm_context_cleanup(&m_requester_context);
        inproc_spdm_context_cleanup(&m_responder_context);
        inproc_transport_cleanup(&m_transport_context);
        return -1;
    }

    inproc_demo_log("CHALLENGE exchange completed successfully");

    inproc_demo_log("Starting KEY_EXCHANGE");

    heartbeat_period = 0;
    req_slot_id_param = 0;
    libspdm_zero_mem(measurement_hash, sizeof(measurement_hash));

    status = libspdm_send_receive_key_exchange(requester_spdm_ctx,
                                               SPDM_CHALLENGE_REQUEST_NO_MEASUREMENT_SUMMARY_HASH,
                                               slot_id, 0, &session_id, &heartbeat_period,
                                               &req_slot_id_param, measurement_hash);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        printf("INPROC_DEMO: KEY_EXCHANGE failed with status 0x%x\n", status);
        inproc_spdm_context_cleanup(&m_requester_context);
        inproc_spdm_context_cleanup(&m_responder_context);
        inproc_transport_cleanup(&m_transport_context);
        return -1;
    }

    printf("INPROC_DEMO: KEY_EXCHANGE successful, session_id=0x%x, heartbeat_period=%u\n",
           session_id, heartbeat_period);

    inproc_demo_log("KEY_EXCHANGE completed successfully");

    inproc_demo_log("Starting FINISH");

    status = libspdm_send_receive_finish(requester_spdm_ctx, session_id, req_slot_id_param);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        printf("INPROC_DEMO: FINISH failed with status 0x%x\n", status);
        inproc_spdm_context_cleanup(&m_requester_context);
        inproc_spdm_context_cleanup(&m_responder_context);
        inproc_transport_cleanup(&m_transport_context);
        return -1;
    }

    printf("INPROC_DEMO: FINISH successful, secure session created with session_id=0x%x\n",
           session_id);

    inproc_demo_log("FINISH completed successfully");
    inproc_demo_log("Secure session created successfully");

    inproc_demo_log("Starting TDISP LOCK_INTERFACE flow");

    interface_id.function_id = 0xbeef;
    interface_id.reserved = 0;

    status = pci_tdisp_get_version(NULL, requester_spdm_ctx, &session_id, &interface_id);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        printf("INPROC_DEMO: TDISP GET_VERSION failed with status 0x%x\n", status);
        inproc_spdm_context_cleanup(&m_requester_context);
        inproc_spdm_context_cleanup(&m_responder_context);
        inproc_transport_cleanup(&m_transport_context);
        return -1;
    }
    inproc_demo_log("TDISP GET_VERSION successful");

    req_caps.tsm_caps = 0;
    libspdm_zero_mem(&rsp_caps, sizeof(rsp_caps));
    status = pci_tdisp_get_capabilities(NULL, requester_spdm_ctx, &session_id,
        &interface_id, &req_caps, &rsp_caps);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        printf("INPROC_DEMO: TDISP GET_CAPABILITIES failed with status 0x%x\n", status);
        inproc_spdm_context_cleanup(&m_requester_context);
        inproc_spdm_context_cleanup(&m_responder_context);
        inproc_transport_cleanup(&m_transport_context);
        return -1;
    }
    inproc_demo_log("TDISP GET_CAPABILITIES successful");

    status = pci_tdisp_get_interface_state(NULL, requester_spdm_ctx, &session_id,
        &interface_id, &tdi_state);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        printf("INPROC_DEMO: TDISP GET_INTERFACE_STATE failed with status 0x%x\n", status);
        inproc_spdm_context_cleanup(&m_requester_context);
        inproc_spdm_context_cleanup(&m_responder_context);
        inproc_transport_cleanup(&m_transport_context);
        return -1;
    }
    printf("INPROC_DEMO: TDI state before lock: 0x%x\n", tdi_state);

    libspdm_zero_mem(&lock_interface_param, sizeof(lock_interface_param));
    lock_interface_param.flags = rsp_caps.lock_interface_flags_supported;
    lock_interface_param.default_stream_id = 0;
    lock_interface_param.mmio_reporting_offset = 0xD0000000;
    lock_interface_param.bind_p2p_address_mask = 0;
    libspdm_zero_mem(start_interface_nonce, sizeof(start_interface_nonce));

    status = pci_tdisp_lock_interface(NULL, requester_spdm_ctx, &session_id, &interface_id,
        &lock_interface_param, start_interface_nonce);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        printf("INPROC_DEMO: TDISP LOCK_INTERFACE failed with status 0x%x\n", status);
        inproc_spdm_context_cleanup(&m_requester_context);
        inproc_spdm_context_cleanup(&m_responder_context);
        inproc_transport_cleanup(&m_transport_context);
        return -1;
    }
    inproc_demo_log("TDISP LOCK_INTERFACE successful");

    status = pci_tdisp_get_interface_state(NULL, requester_spdm_ctx, &session_id,
        &interface_id, &tdi_state);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        printf("INPROC_DEMO: TDISP GET_INTERFACE_STATE after lock failed\n");
        inproc_spdm_context_cleanup(&m_requester_context);
        inproc_spdm_context_cleanup(&m_responder_context);
        inproc_transport_cleanup(&m_transport_context);
        return -1;
    }
    printf("INPROC_DEMO: TDI state after lock: 0x%x\n", tdi_state);

    inproc_demo_log("TDISP LOCK_INTERFACE flow completed successfully");

    inproc_spdm_context_cleanup(&m_requester_context);
    inproc_spdm_context_cleanup(&m_responder_context);
    inproc_transport_cleanup(&m_transport_context);

    inproc_demo_log("SPDM In-Process Demo completed");

    return 0;
}