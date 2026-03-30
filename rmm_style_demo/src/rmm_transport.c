/**
 *  Copyright Notice:
 *  Copyright 2021-2025 DMTF. All rights reserved.
 *  License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/spdm-emu/blob/main/LICENSE.md
 **/

#include "rmm_transport.h"
#include "rmm_demo_context.h"
#include "hal/library/debuglib.h"
#include "hal/library/memlib.h"
#include "library/spdm_responder_lib.h"
#include "internal/libspdm_responder_lib.h"
#include "stdio.h"

static rmm_transport_context_t *m_transport_context;

void rmm_transport_init(rmm_transport_context_t *transport_context)
{
    libspdm_zero_mem(transport_context, sizeof(rmm_transport_context_t));
    m_transport_context = transport_context;
}

void rmm_transport_cleanup(rmm_transport_context_t *transport_context)
{
    libspdm_zero_mem(transport_context, sizeof(rmm_transport_context_t));
    m_transport_context = NULL;
}

static void rmm_trigger_responder_processing(void)
{
    rmm_demo_context_t *responder_context;

    if (m_transport_context == NULL || m_transport_context->responder_context == NULL) {
        return;
    }

    responder_context = (rmm_demo_context_t *)m_transport_context->responder_context;

    libspdm_responder_dispatch_message(responder_context->spdm_context);
}

void rmm_log_message(const char *direction, size_t message_size, const void *message)
{
    printf("RMM %s: size=%zu\n", direction, message_size);
}

libspdm_return_t rmm_requester_send_message(void *spdm_context,
                                            size_t message_size,
                                            const void *message,
                                            uint64_t timeout)
{
    rmm_message_buffer_t *buffer;
    uint8_t *msg_ptr;
    spdm_message_header_t *header;

    if (m_transport_context == NULL || m_transport_context->requester_context == NULL) {
        return LIBSPDM_STATUS_INVALID_PARAMETER;
    }

    buffer = &m_transport_context->req_to_rsp_buffer;

    if (message_size > RMM_TRANSPORT_BUFFER_SIZE) {
        return LIBSPDM_STATUS_BUFFER_TOO_SMALL;
    }

    libspdm_copy_mem(buffer->buffer, RMM_TRANSPORT_BUFFER_SIZE, message, message_size);
    buffer->buffer_size = message_size;
    buffer->has_message = true;

    msg_ptr = (uint8_t *)message;
    header = (spdm_message_header_t *)msg_ptr;
    printf("RMM REQ->RSP: Request code=0x%x, version=0x%x, size=%zu\n",
           header->request_response_code, header->spdm_version, message_size);

    rmm_trigger_responder_processing();

    return LIBSPDM_STATUS_SUCCESS;
}

libspdm_return_t rmm_requester_receive_message(void *spdm_context,
                                               size_t *message_size,
                                               void **message,
                                               uint64_t timeout)
{
    rmm_message_buffer_t *buffer;
    uint8_t *msg_ptr;
    spdm_message_header_t *header;

    if (m_transport_context == NULL || m_transport_context->requester_context == NULL) {
        return LIBSPDM_STATUS_INVALID_PARAMETER;
    }

    buffer = &m_transport_context->rsp_to_req_buffer;

    if (!buffer->has_message) {
        return LIBSPDM_STATUS_RECEIVE_FAIL;
    }

    if (*message_size < buffer->buffer_size) {
        *message_size = buffer->buffer_size;
        return LIBSPDM_STATUS_BUFFER_TOO_SMALL;
    }

    libspdm_copy_mem(*message, *message_size, buffer->buffer, buffer->buffer_size);
    *message_size = buffer->buffer_size;
    buffer->has_message = false;

    msg_ptr = (uint8_t *)*message;
    header = (spdm_message_header_t *)msg_ptr;
    printf("RMM REQ<-RSP: Response code=0x%x, version=0x%x, size=%zu\n",
           header->request_response_code, header->spdm_version, *message_size);

    return LIBSPDM_STATUS_SUCCESS;
}

libspdm_return_t rmm_responder_send_message(void *spdm_context,
                                            size_t message_size,
                                            const void *message,
                                            uint64_t timeout)
{
    rmm_message_buffer_t *buffer;

    if (m_transport_context == NULL || m_transport_context->responder_context == NULL) {
        return LIBSPDM_STATUS_INVALID_PARAMETER;
    }

    buffer = &m_transport_context->rsp_to_req_buffer;

    if (message_size > RMM_TRANSPORT_BUFFER_SIZE) {
        return LIBSPDM_STATUS_BUFFER_TOO_SMALL;
    }

    libspdm_copy_mem(buffer->buffer, RMM_TRANSPORT_BUFFER_SIZE, message, message_size);
    buffer->buffer_size = message_size;
    buffer->has_message = true;

    rmm_log_message("RSP->REQ", message_size, message);

    return LIBSPDM_STATUS_SUCCESS;
}

libspdm_return_t rmm_responder_receive_message(void *spdm_context,
                                               size_t *message_size,
                                               void **message,
                                               uint64_t timeout)
{
    rmm_message_buffer_t *buffer;

    if (m_transport_context == NULL || m_transport_context->responder_context == NULL) {
        return LIBSPDM_STATUS_INVALID_PARAMETER;
    }

    buffer = &m_transport_context->req_to_rsp_buffer;

    if (!buffer->has_message) {
        return LIBSPDM_STATUS_RECEIVE_FAIL;
    }

    if (*message_size < buffer->buffer_size) {
        *message_size = buffer->buffer_size;
        return LIBSPDM_STATUS_BUFFER_TOO_SMALL;
    }

    libspdm_copy_mem(*message, *message_size, buffer->buffer, buffer->buffer_size);
    *message_size = buffer->buffer_size;
    buffer->has_message = false;

    rmm_log_message("RSP<-REQ", *message_size, *message);

    return LIBSPDM_STATUS_SUCCESS;
}

libspdm_return_t rmm_device_acquire_sender_buffer(void *spdm_context, void **msg_buf_ptr)
{
    rmm_demo_context_t *context;

    if (m_transport_context == NULL) {
        return LIBSPDM_STATUS_INVALID_PARAMETER;
    }

    if (m_transport_context->requester_context != NULL &&
        ((rmm_demo_context_t *)m_transport_context->requester_context)->spdm_context == spdm_context) {
        context = (rmm_demo_context_t *)m_transport_context->requester_context;
    } else if (m_transport_context->responder_context != NULL &&
               ((rmm_demo_context_t *)m_transport_context->responder_context)->spdm_context == spdm_context) {
        context = (rmm_demo_context_t *)m_transport_context->responder_context;
    } else {
        return LIBSPDM_STATUS_INVALID_PARAMETER;
    }

    if (context->sender_buffer_acquired) {
        return LIBSPDM_STATUS_ACQUIRE_FAIL;
    }

    *msg_buf_ptr = context->sender_buffer;
    libspdm_zero_mem(context->sender_buffer, sizeof(context->sender_buffer));
    context->sender_buffer_acquired = true;

    return LIBSPDM_STATUS_SUCCESS;
}

void rmm_device_release_sender_buffer(void *spdm_context, const void *msg_buf_ptr)
{
    rmm_demo_context_t *context;

    if (m_transport_context == NULL) {
        return;
    }

    if (m_transport_context->requester_context != NULL &&
        ((rmm_demo_context_t *)m_transport_context->requester_context)->spdm_context == spdm_context) {
        context = (rmm_demo_context_t *)m_transport_context->requester_context;
    } else if (m_transport_context->responder_context != NULL &&
               ((rmm_demo_context_t *)m_transport_context->responder_context)->spdm_context == spdm_context) {
        context = (rmm_demo_context_t *)m_transport_context->responder_context;
    } else {
        return;
    }

    context->sender_buffer_acquired = false;
}

libspdm_return_t rmm_device_acquire_receiver_buffer(void *spdm_context, void **msg_buf_ptr)
{
    rmm_demo_context_t *context;

    if (m_transport_context == NULL) {
        return LIBSPDM_STATUS_INVALID_PARAMETER;
    }

    if (m_transport_context->requester_context != NULL &&
        ((rmm_demo_context_t *)m_transport_context->requester_context)->spdm_context == spdm_context) {
        context = (rmm_demo_context_t *)m_transport_context->requester_context;
    } else if (m_transport_context->responder_context != NULL &&
               ((rmm_demo_context_t *)m_transport_context->responder_context)->spdm_context == spdm_context) {
        context = (rmm_demo_context_t *)m_transport_context->responder_context;
    } else {
        return LIBSPDM_STATUS_INVALID_PARAMETER;
    }

    if (context->receiver_buffer_acquired) {
        return LIBSPDM_STATUS_ACQUIRE_FAIL;
    }

    *msg_buf_ptr = context->receiver_buffer;
    libspdm_zero_mem(context->receiver_buffer, sizeof(context->receiver_buffer));
    context->receiver_buffer_acquired = true;

    return LIBSPDM_STATUS_SUCCESS;
}

void rmm_device_release_receiver_buffer(void *spdm_context, const void *msg_buf_ptr)
{
    rmm_demo_context_t *context;

    if (m_transport_context == NULL) {
        return;
    }

    if (m_transport_context->requester_context != NULL &&
        ((rmm_demo_context_t *)m_transport_context->requester_context)->spdm_context == spdm_context) {
        context = (rmm_demo_context_t *)m_transport_context->requester_context;
    } else if (m_transport_context->responder_context != NULL &&
               ((rmm_demo_context_t *)m_transport_context->responder_context)->spdm_context == spdm_context) {
        context = (rmm_demo_context_t *)m_transport_context->responder_context;
    } else {
        return;
    }

    context->receiver_buffer_acquired = false;
}