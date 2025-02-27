/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include <linux/netlink.h>
#include <linux/types.h>
#include <linux/version.h>
#include <net/genetlink.h>
#include <linux/if.h>
#include <linux/kernel.h>
#include <linux/module.h>

#define CFGMGR_K2U_SEND_INFO_RESPONSE	0x00000001
#define CFGMGR_K2U_SEND_INFO_MULTICAST	0x00000002

/*
 * cfgmgr_send_info
 *	Information needed while sending the packet out.
 */
struct cfgmgr_send_info {
	uint32_t flags;				/* If this message is a response */
	dpdk_ptr_t resp_sock_data;		/* sock data is valid only if response */
	uint32_t pid;				/* From which PID you want to send. 0 for Kernel */
	uint32_t ifnum;				/* Interface on which the message is sent */
};

extern void cfgmgr_cmn_msg_init(struct cfgmgr_cmn_msg *ccm, uint16_t msg_len, uint32_t msg_type, void *cb, void *cb_data);
extern int cfgmgr_k2u_msg_send(struct cfgmgr_send_info *info, struct cfgmgr_cmn_msg *cmn, uint32_t m_size);
// extern int cfgmgr_k2u_send_ack(struct cfgmgr_cmn_msg *nl_cm, char *ackmsg, int ack);

extern struct cfgmgr_ctx cmc_ctx;