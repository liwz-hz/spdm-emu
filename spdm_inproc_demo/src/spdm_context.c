/**
 *  Copyright Notice:
 *  Copyright 2021-2025 DMTF. All rights reserved.
 *  License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/spdm-emu/blob/main/LICENSE.md
 **/

#include "library/spdm_transport_mctp_lib.h"
#include "library/spdm_common_lib.h"
#include "library/spdm_responder_lib.h"
#include "spdm_device_secret_lib_internal.h"
#include "spdm_context.h"
#include "internal_transport.h"
#include "hal/library/memlib.h"
#include "hal/library/cryptlib/cryptlib_cert.h"
#include "industry_standard/spdm.h"
#include "library/pci_doe_responder_lib.h"
#include "library/pci_tdisp_responder_lib.h"
#include "stdlib.h"
#include "stdio.h"

static void *m_inproc_pci_doe_context = NULL;

static const spdm_version_number_t m_spdm_version_table[] = {
    SPDM_MESSAGE_VERSION_11 << SPDM_VERSION_NUMBER_SHIFT_BIT,
    SPDM_MESSAGE_VERSION_12 << SPDM_VERSION_NUMBER_SHIFT_BIT
};

static bool inproc_configure_spdm_version(void *spdm_context, bool is_requester)
{
    libspdm_data_parameter_t parameter;
    uint8_t data8;
    uint16_t data16;
    uint32_t data32;

    libspdm_zero_mem(&parameter, sizeof(parameter));
    parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;

    libspdm_set_data(spdm_context, LIBSPDM_DATA_SPDM_VERSION, &parameter,
                     m_spdm_version_table, sizeof(m_spdm_version_table));

    data8 = 0;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_CT_EXPONENT,
                     &parameter, &data8, sizeof(data8));

    if (is_requester) {
        data32 = SPDM_GET_CAPABILITIES_REQUEST_FLAGS_CERT_CAP |
                 SPDM_GET_CAPABILITIES_REQUEST_FLAGS_CHAL_CAP |
                 SPDM_GET_CAPABILITIES_REQUEST_FLAGS_KEY_EX_CAP |
                 SPDM_GET_CAPABILITIES_REQUEST_FLAGS_ENCRYPT_CAP |
                 SPDM_GET_CAPABILITIES_REQUEST_FLAGS_MAC_CAP;
    } else {
        data32 = SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CERT_CAP |
                 SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CHAL_CAP |
                 SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_KEY_EX_CAP |
                 SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_ENCRYPT_CAP |
                 SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MAC_CAP;
    }
    libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS, &parameter,
                     &data32, sizeof(data32));

    data8 = SPDM_MEASUREMENT_SPECIFICATION_DMTF;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_MEASUREMENT_SPEC, &parameter,
                     &data8, sizeof(data8));

    data32 = SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_384 |
             SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_256;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_BASE_HASH_ALGO, &parameter,
                     &data32, sizeof(data32));

    data32 = SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P384 |
             SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P256;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_BASE_ASYM_ALGO, &parameter,
                     &data32, sizeof(data32));

    data16 = SPDM_ALGORITHMS_DHE_NAMED_GROUP_SECP_384_R1 |
             SPDM_ALGORITHMS_DHE_NAMED_GROUP_SECP_256_R1;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_DHE_NAME_GROUP, &parameter,
                     &data16, sizeof(data16));

    data16 = SPDM_ALGORITHMS_AEAD_CIPHER_SUITE_AES_256_GCM |
             SPDM_ALGORITHMS_AEAD_CIPHER_SUITE_CHACHA20_POLY1305;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_AEAD_CIPHER_SUITE, &parameter,
                     &data16, sizeof(data16));

    data16 = SPDM_ALGORITHMS_KEY_SCHEDULE_SPDM;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_KEY_SCHEDULE, &parameter,
                     &data16, sizeof(data16));

    data8 = SPDM_ALGORITHMS_OPAQUE_DATA_FORMAT_1;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_OTHER_PARAMS_SUPPORT, &parameter,
                     &data8, sizeof(data8));

    return true;
}

static bool inproc_provision_responder_certificate(void *spdm_context)
{
    libspdm_data_parameter_t parameter;
    void *data;
    size_t data_size;
    bool res;
    uint8_t data8;
    uint16_t data16;
    uint32_t base_hash_algo;
    uint32_t base_asym_algo;

    base_hash_algo = SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_384;
    base_asym_algo = SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P384;

    res = libspdm_read_responder_public_certificate_chain(
        base_hash_algo, base_asym_algo, &data, &data_size, NULL, NULL);
    if (!res) {
        printf("INPROC_DEMO: Failed to read responder certificate chain (hash=0x%x, asym=0x%x)\n",
               base_hash_algo, base_asym_algo);
        return false;
    }
    printf("INPROC_DEMO: Successfully read responder certificate chain, size=%zu\n", data_size);

    libspdm_zero_mem(&parameter, sizeof(parameter));
    parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
    parameter.additional_data[0] = 0;

    libspdm_set_data(spdm_context, LIBSPDM_DATA_LOCAL_PUBLIC_CERT_CHAIN,
                     &parameter, data, data_size);

    data8 = 0xA0;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_LOCAL_KEY_PAIR_ID,
                     &parameter, &data8, sizeof(data8));

    data8 = SPDM_CERTIFICATE_INFO_CERT_MODEL_DEVICE_CERT;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_LOCAL_CERT_INFO,
                     &parameter, &data8, sizeof(data8));

    data16 = SPDM_KEY_USAGE_BIT_MASK_KEY_EX_USE |
             SPDM_KEY_USAGE_BIT_MASK_CHALLENGE_USE |
             SPDM_KEY_USAGE_BIT_MASK_MEASUREMENT_USE |
             SPDM_KEY_USAGE_BIT_MASK_ENDPOINT_INFO_USE;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_LOCAL_KEY_USAGE_BIT_MASK,
                     &parameter, &data16, sizeof(data16));

    return true;
}

static bool inproc_provision_requester_peer_root_cert(void *spdm_context)
{
    libspdm_data_parameter_t parameter;
    void *data;
    size_t data_size;
    uint8_t *hash;
    size_t hash_size;
    const uint8_t *root_cert;
    size_t root_cert_size;
    bool res;
    uint32_t base_hash_algo;
    uint32_t base_asym_algo;

    base_hash_algo = SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_384;
    base_asym_algo = SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P384;

    res = libspdm_read_responder_root_public_certificate(
        base_hash_algo, base_asym_algo, &data, &data_size, (void **)&hash, &hash_size);
    if (!res) {
        printf("INPROC_DEMO: Failed to read responder root certificate (hash=0x%x, asym=0x%x)\n",
               base_hash_algo, base_asym_algo);
        return false;
    }
    printf("INPROC_DEMO: Successfully read responder root certificate, size=%zu\n", data_size);

    libspdm_x509_get_cert_from_cert_chain(
        (uint8_t *)data + sizeof(spdm_cert_chain_t) + hash_size,
        data_size - sizeof(spdm_cert_chain_t) - hash_size, 0,
        &root_cert, &root_cert_size);

    libspdm_zero_mem(&parameter, sizeof(parameter));
    parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_PEER_PUBLIC_ROOT_CERT,
                     &parameter, (void *)root_cert, root_cert_size);

    free(data);

    return true;
}

static libspdm_return_t inproc_get_response_vendor_defined_request(
    void *spdm_context, const uint32_t *session_id, bool is_app_message,
    size_t request_size, const void *request, size_t *response_size, void *response)
{
    return pci_doe_get_response_spdm_vendor_defined_request(
        m_inproc_pci_doe_context, spdm_context, session_id,
        request, request_size, response, response_size);
}

bool inproc_init_tdisp_responder(void *spdm_context)
{
    libspdm_return_t status;

    libspdm_register_get_response_func(spdm_context,
        inproc_get_response_vendor_defined_request);

    status = pci_doe_register_vendor_response_func(
        m_inproc_pci_doe_context,
        SPDM_REGISTRY_ID_PCISIG, SPDM_VENDOR_ID_PCISIG,
        PCI_PROTOCOL_ID_TDISP, pci_tdisp_get_response);
    if (LIBSPDM_STATUS_IS_ERROR(status)) {
        return false;
    }

    return true;
}

bool inproc_spdm_context_init(inproc_spdm_context_t *context,
                                inproc_transport_context_t *transport_context,
                                bool is_requester)
{
    context->spdm_context = malloc(libspdm_get_context_size());
    if (context->spdm_context == NULL) {
        return false;
    }

    libspdm_init_context(context->spdm_context);

    context->transport_context = transport_context;
    context->is_requester = is_requester;
    context->sender_buffer_acquired = false;
    context->receiver_buffer_acquired = false;

    if (is_requester) {
        transport_context->requester_context = context;
    } else {
        transport_context->responder_context = context;
    }

    if (is_requester) {
        libspdm_register_device_io_func(context->spdm_context,
                                        inproc_requester_send_message,
                                        inproc_requester_receive_message);
    } else {
        libspdm_register_device_io_func(context->spdm_context,
                                        inproc_responder_send_message,
                                        inproc_responder_receive_message);
    }

    libspdm_register_transport_layer_func(context->spdm_context,
                                          INPROC_MAX_SPDM_MSG_SIZE,
                                          INPROC_TRANSPORT_HEADER_SIZE,
                                          INPROC_TRANSPORT_TAIL_SIZE,
                                          libspdm_transport_mctp_encode_message,
                                          libspdm_transport_mctp_decode_message);

    libspdm_register_device_buffer_func(context->spdm_context,
                                          INPROC_SENDER_RECEIVER_BUFFER_SIZE,
                                          INPROC_SENDER_RECEIVER_BUFFER_SIZE,
                                          inproc_device_acquire_sender_buffer,
                                          inproc_device_release_sender_buffer,
                                          inproc_device_acquire_receiver_buffer,
                                          inproc_device_release_receiver_buffer);

    context->scratch_buffer_size = libspdm_get_sizeof_required_scratch_buffer(
        context->spdm_context);
    context->scratch_buffer = malloc(context->scratch_buffer_size);
    if (context->scratch_buffer == NULL) {
        free(context->spdm_context);
        context->spdm_context = NULL;
        return false;
    }

    libspdm_set_scratch_buffer(context->spdm_context,
                               context->scratch_buffer,
                               context->scratch_buffer_size);

    if (!inproc_configure_spdm_version(context->spdm_context, is_requester)) {
        free(context->scratch_buffer);
        free(context->spdm_context);
        context->spdm_context = NULL;
        context->scratch_buffer = NULL;
        return false;
    }

    if (!is_requester) {
        if (!inproc_provision_responder_certificate(context->spdm_context)) {
            free(context->scratch_buffer);
            free(context->spdm_context);
            context->spdm_context = NULL;
            context->scratch_buffer = NULL;
            return false;
        }
        if (!inproc_init_tdisp_responder(context->spdm_context)) {
            free(context->scratch_buffer);
            free(context->spdm_context);
            context->spdm_context = NULL;
            context->scratch_buffer = NULL;
            return false;
        }
    } else {
        if (!inproc_provision_requester_peer_root_cert(context->spdm_context)) {
            free(context->scratch_buffer);
            free(context->spdm_context);
            context->spdm_context = NULL;
            context->scratch_buffer = NULL;
            return false;
        }
    }

    return true;
}

void inproc_spdm_context_cleanup(inproc_spdm_context_t *context)
{
    if (context->spdm_context != NULL) {
        free(context->spdm_context);
        context->spdm_context = NULL;
    }
    if (context->scratch_buffer != NULL) {
        free(context->scratch_buffer);
        context->scratch_buffer = NULL;
    }
    context->sender_buffer_acquired = false;
    context->receiver_buffer_acquired = false;
}

void *inproc_get_requester_context(inproc_spdm_context_t *context)
{
    return context->spdm_context;
}

void *inproc_get_responder_context(inproc_spdm_context_t *context)
{
    return context->spdm_context;
}