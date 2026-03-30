/**
 *  Copyright Notice:
 *  Copyright 2021-2025 DMTF. All rights reserved.
 *  License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/spdm-emu/blob/main/LICENSE.md
 **/

#ifndef __INTERNAL_TRANSPORT_H__
#define __INTERNAL_TRANSPORT_H__

#include "hal/base.h"
#include "library/spdm_common_lib.h"

#define INPROC_TRANSPORT_BUFFER_SIZE 0x2000

typedef struct {
    uint8_t buffer[INPROC_TRANSPORT_BUFFER_SIZE];
    size_t buffer_size;
    bool has_message;
} inproc_message_buffer_t;

typedef struct {
    inproc_message_buffer_t req_to_rsp_buffer;
    inproc_message_buffer_t rsp_to_req_buffer;
    void *requester_context;
    void *responder_context;
} inproc_transport_context_t;

libspdm_return_t inproc_requester_send_message(void *spdm_context,
                                               size_t message_size,
                                               const void *message,
                                               uint64_t timeout);

libspdm_return_t inproc_requester_receive_message(void *spdm_context,
                                                  size_t *message_size,
                                                  void **message,
                                                  uint64_t timeout);

libspdm_return_t inproc_responder_send_message(void *spdm_context,
                                               size_t message_size,
                                               const void *message,
                                               uint64_t timeout);

libspdm_return_t inproc_responder_receive_message(void *spdm_context,
                                                  size_t *message_size,
                                                  void **message,
                                                  uint64_t timeout);

libspdm_return_t inproc_device_acquire_sender_buffer(void *spdm_context, void **msg_buf_ptr);

void inproc_device_release_sender_buffer(void *spdm_context, const void *msg_buf_ptr);

libspdm_return_t inproc_device_acquire_receiver_buffer(void *spdm_context, void **msg_buf_ptr);

void inproc_device_release_receiver_buffer(void *spdm_context, const void *msg_buf_ptr);

void inproc_transport_init(inproc_transport_context_t *transport_context);

void inproc_transport_cleanup(inproc_transport_context_t *transport_context);

void inproc_log_message(const char *direction, size_t message_size, const void *message);

#endif