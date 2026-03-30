/**
 *  Copyright Notice:
 *  Copyright 2021-2025 DMTF. All rights reserved.
 *  License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/spdm-emu/blob/main/LICENSE.md
 **/

#ifndef __RMM_DEMO_CONTEXT_H__
#define __RMM_DEMO_CONTEXT_H__

#include "hal/base.h"
#include "library/spdm_common_lib.h"
#include "rmm_transport.h"

#define RMM_TRANSPORT_HEADER_SIZE 64
#define RMM_TRANSPORT_TAIL_SIZE 64
#define RMM_MAX_SPDM_MSG_SIZE 0x1200
#define RMM_SENDER_RECEIVER_BUFFER_SIZE (RMM_MAX_SPDM_MSG_SIZE + \
                                         RMM_TRANSPORT_HEADER_SIZE + \
                                         RMM_TRANSPORT_TAIL_SIZE)

typedef struct {
    void *spdm_context;
    void *scratch_buffer;
    size_t scratch_buffer_size;
    uint8_t sender_buffer[RMM_SENDER_RECEIVER_BUFFER_SIZE];
    uint8_t receiver_buffer[RMM_SENDER_RECEIVER_BUFFER_SIZE];
    bool sender_buffer_acquired;
    bool receiver_buffer_acquired;
    bool is_requester;
    rmm_transport_context_t *transport_context;
    uint32_t session_id;
    uint8_t slot_id;
} rmm_demo_context_t;

bool rmm_demo_context_init(rmm_demo_context_t *context,
                           rmm_transport_context_t *transport_context,
                           bool is_requester);

void rmm_demo_context_cleanup(rmm_demo_context_t *context);

void *rmm_demo_get_requester_context(rmm_demo_context_t *context);

void *rmm_demo_get_responder_context(rmm_demo_context_t *context);

#endif