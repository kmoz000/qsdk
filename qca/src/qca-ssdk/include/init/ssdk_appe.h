/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SSDK_APPE_H_
#define _SSDK_APPE_H_

#ifdef __cplusplus
extern "C" {
#endif                          /* __cplusplus */

#include "ssdk_init.h"

sw_error_t qca_appe_hw_init(a_uint32_t dev_id);

#define APPE_MAX_C_TOKEN_NUM                0x3fffffff
#define APPE_MAX_E_TOKEN_NUM                0x3fffffff
#define APPE_PORT_SHAPER_TIMESLOT_DFT       8
#define APPE_FLOW_SHAPER_TIMESLOT_DFT       128
#define APPE_QUEUE_SHAPER_TIMESLOT_DFT      353*2
#define APPE_SHAPER_IPG_PREAMBLE_LEN_DFT    20
#define APPE_QUEUE_SHAPER_HEAD              0
#define APPE_QUEUE_SHAPER_TAIL              299
#define APPE_FLOW_SHAPER_HEAD               0
#define APPE_FLOW_SHAPER_TAIL               63
#define APPE_POLICER_TIMESLOT_DFT           353*4
#define APPE_POLICER_HEAD                   0
#define APPE_POLICER_TAIL                   511
#define MPPE_POLICER_TIMESLOT_DFT           200*2
#define MPPE_POLICER_TAIL                   127
#define APPE_ACL_POLICER_CFG_MAX            512
#define MPPE_ACL_POLICER_CFG_MAX            128

#ifdef __cplusplus
}
#endif                          /* __cplusplus */
#endif                          /* _SSDK_APPE_H */

