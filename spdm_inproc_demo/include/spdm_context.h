/**
 *  Copyright Notice:
 *  Copyright 2021-2025 DMTF. All rights reserved.
 *  License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/spdm-emu/blob/main/LICENSE.md
 **/

#ifndef __SPDM_CONTEXT_H__
#define __SPDM_CONTEXT_H__

#include "hal/base.h"
#include "library/spdm_common_lib.h"
#include "internal_transport.h"

#define INPROC_TRANSPORT_HEADER_SIZE 64
#define INPROC_TRANSPORT_TAIL_SIZE 64
#define INPROC_MAX_SPDM_MSG_SIZE 0x1200
#define INPROC_SENDER_RECEIVER_BUFFER_SIZE (INPROC_MAX_SPDM_MSG_SIZE + \
                                            INPROC_TRANSPORT_HEADER_SIZE + \
                                            INPROC_TRANSPORT_TAIL_SIZE)

typedef struct {
    void *spdm_context;
    void *scratch_buffer;
    size_t scratch_buffer_size;
    uint8_t sender_buffer[INPROC_SENDER_RECEIVER_BUFFER_SIZE];
    uint8_t receiver_buffer[INPROC_SENDER_RECEIVER_BUFFER_SIZE];
    bool sender_buffer_acquired;
    bool receiver_buffer_acquired;
    bool is_requester;
    inproc_transport_context_t *transport_context;
} inproc_spdm_context_t;

bool inproc_spdm_context_init(inproc_spdm_context_t *context,
                              inproc_transport_context_t *transport_context,
                              bool is_requester);

void inproc_spdm_context_cleanup(inproc_spdm_context_t *context);

void *inproc_get_requester_context(inproc_spdm_context_t *context);

void *inproc_get_responder_context(inproc_spdm_context_t *context);

#endif