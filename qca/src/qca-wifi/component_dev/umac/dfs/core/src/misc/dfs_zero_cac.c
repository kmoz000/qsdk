/*
 * Copyright (c) 2016-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2007-2008 Sam Leffler, Errno Consulting
 * All rights reserved.
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * DOC: This file has ZERO CAC DFS functions.
 * Abstract:- Operation in a DFS channel requires CAC that adds additional
 * delay as well as loss of connection even when CSA is used. ETSI allows
 * pre-CAC, i.e. performing CAC at a convenient time and using that channel
 * later. Once Pre-CAC is done in a channel, it is no longer required to
 * perform a CAC in the channel before TX/RX as long as radar is not found in
 * it or we reset or restart the device.
 *
 * Design:-
 * When Zero-CAC is enabled and the current regulatory domain is ETSI,
 * a Binary Search Forest (BSForest) is initialized and maintained, indexed by
 * DFS IEEE channels of different bandwidths (20/40/80 MHz).
 *
 * The structure of precac BSForest is:
 *   1). A preCAC list of 80MHz channels which contains the Binary Search Tree
 *       (BSTree) root pointer.
 *   2). The BSTree consists of nodes of different IEEEs of different
 *       bandwidths (80/40/20 MHz) of that 80MHz channel in the list.
 *
 * Each Binary Search Tree (BSTree) node has a unique IEEE channel and
 * three values that indicate three statuses (Channel valid / CAC Done /
 * Channel in NOL) of the subchannels of the node.
 *
 * A sample Precac BSForest:
 *
 * List HEAD --------> 58 -------------> 106 --------------> 122
 *                      |                 |                   |
 *                     58                106                 122
 *                     /\                / \                 / \
 *                    /  \              /   \               /   \
 *                   /    \            /     \             /     \
 *                  /      \          /       \           /       \
 *                 54      62       102       110       118       126
 *                 /\      /\       / \       / \       / \       / \
 *                /  \    /  \     /   \     /   \     /   \     /   \
 *               52  56  60  64  100  104  108  112  116  120  124   128
 *
 * Consider the BSTree 106, where all subchannels of 106HT80 are available
 * in the regulatory (100, 104, 108, 112 are valid channels) and 100HT20 is
 * preCAC done and 104HT20 is in NOL. The BSTree would look like:
 *
 *                               _________
 *                              |   | | | |
 *                              |106|4|1|1|
 *                              |___|_|_|_|
 *                                 _/ \_
 *                               _/     \_
 *                             _/         \_
 *                           _/             \_
 *                         _/                 \_
 *                 _______/_                   _\_______
 *                |   | | | |                 |   | | | |
 *                |102|2|1|1|                 |110|2|0|0|
 *                |___|_|_|_|                 |___|_|_|_|
 *                    / \                           / \
 *                   /   \                         /   \
 *                  /     \                       /     \
 *                 /       \                     /       \
 *          ______/__     __\______       ______/__     __\______
 *         |   | | | |   |   | | | |     |   | | | |   |   | | | |
 *         |100|1|1|0|   |104|1|0|1|     |108|1|0|0|   |112|1|0|0|
 *         |___|_|_|_|   |___|_|_|_|     |___|_|_|_|   |___|_|_|_|
 *
 *
 *  Syntax of each node:
 *      _______________________________
 *     |      |       |          |     |
 *     | IEEE | Valid | CAC done | NOL |
 *     |______|_______|__________|_____|
 *
 * where,
 * IEEE     - Unique IEEE channel that the node represents.
 * Valid    - Number of valid subchannels of the node (for the current country).
 * CAC done - Number of subchannels of the node that are CAC
 *            (primary or preCAC) done.
 * NOL      - Number of subchannels of the node in NOL.
 *
 * PreCAC (legacy chipsets. e.g. QCA9984):
 *   The pre-CAC is done in a RADIO that has  VHT80_80 capable radio where the
 *   primary and secondary HT80s can be programmed independently with two
 *   different HT80 channels.
 *   The bandwidth of preCAC channels are always 80MHz.
 *
 * Agile CAC (e.g. Hawkeye V2):
 *   The Agile CAC is done in a chipset that has a separate Agile detector,
 *   which can perform Rx on the channel provided by stealing the chains
 *   from one of the primary pdevs.
 *   Note: This impliess that the bandwidth of the Agile detector is always
 *   the same as the pdev it is attached to.
 *   The bandwidth of Agile CAC channels may vary from 20/40/80 MHz.
 *
 * Operations on preCAC list:
 *
 *  Initialize the list:
 *    To initialize the preCAC list,
 *      1. Find a 80MHz DFS channel.
 *      2. Add an entry to the list with this channel as index and create
 *         a BSTree for this channel. This is done by level order insertion
 *         where the channel for each node is determined by adding the
 *         respective level offsets to the 80MHz channel.
 *      3. Repeat step 1 & 2 until no 80MHz DFS channels are found.
 *
 *  Remove the list:
 *   To remove the preCAC list,
 *      1. Iterate through the list and for every entry,
 *         a). Convert the tree into a left child only list, removing the
 *             root node on the way. O(n) deletion.
 *         b). Remove the preCAC list entry.
 *
 *   Algorithm to convert the tree to a left child only list:
 *     1. Find the leftmost leaf node of the BSTree.
 *     2. Set current node as root node.
 *     3. If current node has right child, add right child of current node
 *        as left child of leftmost leaf.
 *     4. Update the leftmost leaf.
 *     5. Update current node to left child and remove the node.
 *     6. Repeat steps 3 to 5 till current node is NULL.
 *
 *  Print the list:
 *   To print the contents of the preCAC list,
 *    1. Iterate through the list and for every entry,
 *       a) Perform a morris preorder traversal (iterative and O(n)) and
 *          for every node, print the Channel IEEE and CAC and NOL values.
 *          Use the level information to create a tree(3) command like
 *          structure for printing each nodes of the BSTree.
 *
 *   A sample BSTree print output:
 *
 *        A                  A(C,N)
 *       / \                 |
 *      B   C                |------- B(C,N)
 *     / \                   |        |
 *    D   E                  |        |------- D(C,N)
 *                           |        |
 *                           |        |------- E(C,N)
 *                           |
 *                           |------- E(C,N)
 *
 *    Where C is number of CACed subchannels, and N is number of
 *    NOL subchannels.
 *
 *  Find a channel to preCAC/Agile CAC:
 *   1. Given a requested bandwidth (80MHz always in case of preCAC, XMHz in
 *      case of Agile CAC where X is the current operating bandwidth of the
 *      pdev the detector is attached to), iterate through the preCAC list.
 *   2. For every entry, find if there a valid channel that is not in NOL
 *      and not in CAC done and is of the requested mode.
 *   3. If such channel exists and is not equal to the current operating
 *      channel, then return the channel. Else, go to the next entry.
 *
 *  Find if the channel is preCAC done:
 *   1. Given a IEEE channel, go through the preCAC list and find the entry
 *      which has the channel provided.
 *   2. Traverse through the BSTree and check if the channel's CACed
 *      subchannels value is equal to the number of subchannels of that level.
 *   3. If the above condition is true, return 1, else 0.
 *
 *  Mark the channel as CAC done:
 *   1. Given a channel, find all the subchannels.
 *   2. For every subchannel, iterate through the list, and find the entry
 *      the channel belongs to.
 *   3. Traverse through the BSTree of this entry, going to left or right
 *      child based on the channel IEEE.
 *   4. Increment the CACed subchannels count (by 1) along the way till
 *      (and including) the node that contains the subchannel that
 *      was searched for.
 *
 *  Unmark the channel as CAC done:
 *   1. Given a 20MHz channel, iterate through the list, and find the entry
 *      the channel belongs to.
 *   2. Traverse through the BSTree of this entry, going to left or right
 *      child based on the channel IEEE.
 *   3. Decrement the CACed subchannels count (by 1) along the way till
 *      (and including) the node that contains the subchannel that
 *      was searched for.
 *
 *  Mark the channel as NOL:
 *   1. Given a 20MHz channel, iterate through the list, and find the entry
 *      the channel belongs to.
 *   3. Traverse through the BSTree of this entry, going to left or right
 *      child based on the channel IEEE.
 *   4. Increment the NOL subchannels count (by 1) along the way till
 *      (and including) the node that contains the subchannel that
 *      was searched for.
 *   5. If the subchannel node's CAC subchannels value is non-zero, unmark
 *      the channel as CAC done.
 *
 *  Unmark the channel as NOL:
 *   1. Given a 20MHz channel, iterate through the list, and find the entry
 *      the channel belongs to.
 *   3. Traverse through the BSTree of this entry, going to left or right
 *      child based on the channel IEEE.
 *   4. Decrement the NOL subchannels count (by 1) along the way till
 *      (and including) the node that contains the subchannel that
 *      was searched for.
 *
 * New RadarTool commands:-
 * 1)radartool -i wifi[X] bangradar 1 (where 1 is the segment ID)
 * It simulates RADAR from the secondary HT80 when the
 * secondary HT80 is doing pre-CAC. If secondary is not
 * doing any pre-CAC then this command has no effect.
 * 2)radartool -i wifi[X] showPreCACLists
 * It shows all the pre-CAC Lists' contents.
 *
 * New iwpriv commands:-
 * 1)iwpriv wifi[X] preCACEn 0/1
 * This command enables/disables the zero-cac-DFS.
 * 2)iwpriv wifi[X] pCACTimeout <timeout>
 * Override the pCACTimeout.
 *
 * FAQ(Frequently Asked Questions):-
 * 1)
 * Question:
 *    Why was the separate HT80 preCAC NOL timer removed?
 * Answer:
 *    In previous design, the channels that were preCACed were always
 *    80MHz channels. Since NOL timers were maintained for 20MHz channels,
 *    a separate preCAC NOL timer was created to modularize and move
 *    lists accordingly at the expiry of the timer.
 *    With the current support of 20/40/80MHz preCAC channels, and
 *    the introduction of subchannel marking, the existing NOL timer
 *    can be used to mark the preCAC lists aswell.
 */

#include "wlan_dfs_lmac_api.h"
#include "wlan_dfs_mlme_api.h"
#include "wlan_dfs_utils_api.h"
#include "dfs_internal.h"
#include "dfs_process_radar_found_ind.h"
#include "target_if.h"
#include "wlan_dfs_init_deinit_api.h"
#include "../dfs_precac_forest.h"
#include "../dfs_misc.h"
#include "dfs_zero_cac.h"
#include <wlan_reg_channel_api.h>

static const
uint8_t dfs_phymode_decoupler[WLAN_PHYMODE_MAX][2] = {
	{ REG_PHYMODE_11AX, CH_WIDTH_160MHZ   }, /* WLAN_PHYMODE_AUTO */
	{ REG_PHYMODE_11A,  CH_WIDTH_20MHZ    }, /* WLAN_PHYMODE_11A */
	{ REG_PHYMODE_11B,  CH_WIDTH_20MHZ    }, /* WLAN_PHYMODE_11B */
	{ REG_PHYMODE_11G,  CH_WIDTH_20MHZ    }, /* WLAN_PHYMODE_11G */
	{ REG_PHYMODE_11G,  CH_WIDTH_20MHZ    }, /* WLAN_PHYMODE_11G_ONLY */
	{ REG_PHYMODE_11N,  CH_WIDTH_20MHZ    }, /* WLAN_PHYMODE_11NA_HT20 */
	{ REG_PHYMODE_11N,  CH_WIDTH_20MHZ    }, /* WLAN_PHYMODE_11NG_HT20 */
	{ REG_PHYMODE_11N,  CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11NA_HT40 */
	{ REG_PHYMODE_11N,  CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11NG_HT40PLUS */
	{ REG_PHYMODE_11N,  CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11NG_HT40MINUS */
	{ REG_PHYMODE_11N,  CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11NG_HT40 */
	{ REG_PHYMODE_11AC, CH_WIDTH_20MHZ    }, /* WLAN_PHYMODE_11AC_VHT20 */
	{ REG_PHYMODE_11AC, CH_WIDTH_20MHZ    }, /* WLAN_PHYMODE_11AC_VHT20_2G */
	{ REG_PHYMODE_11AC, CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11AC_VHT40 */
	{ REG_PHYMODE_11AC, CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11AC_VHT40PLUS_2G */
	{ REG_PHYMODE_11AC, CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11AC_VHT40MINUS_2G */
	{ REG_PHYMODE_11AC, CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11AC_VHT40_2G */
	{ REG_PHYMODE_11AC, CH_WIDTH_80MHZ    }, /* WLAN_PHYMODE_11AC_VHT80 */
	{ REG_PHYMODE_11AC, CH_WIDTH_80MHZ    }, /* WLAN_PHYMODE_11AC_VHT80_2G */
	{ REG_PHYMODE_11AC, CH_WIDTH_160MHZ   }, /* WLAN_PHYMODE_11AC_VHT160 */
	{ REG_PHYMODE_11AC, CH_WIDTH_80P80MHZ }, /* WLAN_PHYMODE_11AC_VHT80_80 */
	{ REG_PHYMODE_11AX, CH_WIDTH_20MHZ    }, /* WLAN_PHYMODE_11AXA_HE20 */
	{ REG_PHYMODE_11AX, CH_WIDTH_20MHZ    }, /* WLAN_PHYMODE_11AXG_HE20 */
	{ REG_PHYMODE_11AX, CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11AXA_HE40 */
	{ REG_PHYMODE_11AX, CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11AXG_HE40PLUS */
	{ REG_PHYMODE_11AX, CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11AXG_HE40MINUS */
	{ REG_PHYMODE_11AX, CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11AXG_HE40 */
	{ REG_PHYMODE_11AX, CH_WIDTH_80MHZ    }, /* WLAN_PHYMODE_11AXA_HE80 */
	{ REG_PHYMODE_11AX, CH_WIDTH_80MHZ    }, /* WLAN_PHYMODE_11AXG_HE80 */
	{ REG_PHYMODE_11AX, CH_WIDTH_160MHZ   }, /* WLAN_PHYMODE_11AXA_HE160 */
	{ REG_PHYMODE_11AX, CH_WIDTH_80P80MHZ }, /* WLAN_PHYMODE_11AXA_HE80_80 */
#ifdef WLAN_FEATURE_11BE
	{ REG_PHYMODE_11BE, CH_WIDTH_20MHZ    }, /* WLAN_PHYMODE_11BEA_EHT20 */
	{ REG_PHYMODE_11BE, CH_WIDTH_20MHZ    }, /* WLAN_PHYMODE_11BEG_EHT20 */
	{ REG_PHYMODE_11BE, CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11BEA_EHT40 */
	{ REG_PHYMODE_11BE, CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11BEG_EHT40PLUS */
	{ REG_PHYMODE_11BE, CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11BEG_EHT40MINUS */
	{ REG_PHYMODE_11BE, CH_WIDTH_40MHZ    }, /* WLAN_PHYMODE_11BEG_EHT40 */
	{ REG_PHYMODE_11BE, CH_WIDTH_80MHZ    }, /* WLAN_PHYMODE_11BEA_EHT80 */
	{ REG_PHYMODE_11BE, CH_WIDTH_80MHZ    }, /* WLAN_PHYMODE_11BEG_EHT80 */
	{ REG_PHYMODE_11BE, CH_WIDTH_160MHZ   }, /* WLAN_PHYMODE_11BEA_EHT160 */
	{ REG_PHYMODE_11BE, CH_WIDTH_320MHZ   }, /* WLAN_PHYMODE_11BEA_EHT320 */
#endif
};

static const
uint8_t dfs_phymode_converter[WLAN_PHYMODE_MAX] = {
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_AUTO */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11A */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11B */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11G */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11G_ONLY */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11NA_HT20 */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11NG_HT20 */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11NA_HT40 */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11NG_HT40PLUS */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11NG_HT40MINUS */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11NG_HT40 */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11AC_VHT20 */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11AC_VHT20_2G */
	WLAN_PHYMODE_11AC_VHT20,            /* WLAN_PHYMODE_11AC_VHT40 */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11AC_VHT40PLUS_2G */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11AC_VHT40MINUS_2G */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11AC_VHT40_2G */
	WLAN_PHYMODE_11AC_VHT40,            /* WLAN_PHYMODE_11AC_VHT80 */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11AC_VHT80_2G */
	WLAN_PHYMODE_11AC_VHT80,            /* WLAN_PHYMODE_11AC_VHT160 */
	WLAN_PHYMODE_11AC_VHT80,            /* WLAN_PHYMODE_11AC_VHT80_80 */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11AXA_HE20 */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11AXG_HE20 */
	WLAN_PHYMODE_11AXA_HE20,            /* WLAN_PHYMODE_11AXA_HE40 */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11AXG_HE40PLUS */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11AXG_HE40MINUS */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11AXG_HE40 */
	WLAN_PHYMODE_11AXA_HE40,            /* WLAN_PHYMODE_11AXA_HE80 */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11AXG_HE80 */
	WLAN_PHYMODE_11AXA_HE80,            /* WLAN_PHYMODE_11AXA_HE160 */
	WLAN_PHYMODE_11AXA_HE80,            /* WLAN_PHYMODE_11AXA_HE80_80 */
#ifdef WLAN_FEATURE_11BE
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11BEA_EHT20 */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11BEG_EHT20 */
	WLAN_PHYMODE_11BEA_EHT20,           /* WLAN_PHYMODE_11BEA_EHT40 */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11BEG_EHT40PLUS */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11BEG_EHT40MINUS */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11BEG_EHT40 */
	WLAN_PHYMODE_11BEA_EHT40,           /* WLAN_PHYMODE_11BEA_EHT80 */
	WLAN_PHYMODE_MAX,                   /* WLAN_PHYMODE_11BEG_EHT80 */
	WLAN_PHYMODE_11BEA_EHT80,           /* WLAN_PHYMODE_11BEA_EHT160 */
	WLAN_PHYMODE_11BEA_EHT160,          /* WLAN_PHYMODE_11BEA_EHT320 */
#endif
};

 /*dfs_zero_cac_reset() - Reset zero cac variables.
  *@dfs: Pointer to wlan_dfs
  */
#if !defined(MOBILE_DFS_SUPPORT)
void dfs_zero_cac_reset(struct wlan_dfs *dfs)
{
	dfs->dfs_precac_timeout_override = -1;
	dfs->dfs_precac_primary_freq_mhz = 0;
	dfs->dfs_precac_secondary_freq_mhz = 0;
}

int dfs_override_precac_timeout(struct wlan_dfs *dfs, int precac_timeout)
{
	if (!dfs)
		return -EIO;

	dfs->dfs_precac_timeout_override = precac_timeout;
	dfs_info(dfs, WLAN_DEBUG_DFS_ALWAYS, "PreCAC timeout is now %s (%d)",
		(precac_timeout == -1) ? "default" : "overridden",
		precac_timeout);

	return 0;
}

int dfs_get_override_precac_timeout(struct wlan_dfs *dfs, int *precac_timeout)
{
	if (!dfs)
		return -EIO;

	(*precac_timeout) = dfs->dfs_precac_timeout_override;

	return 0;
}

bool dfs_is_precac_timer_running(struct wlan_dfs *dfs)
{
	return dfs->dfs_soc_obj->dfs_precac_timer_running ? true : false;
}

#ifdef WLAN_FEATURE_11BE
bool dfs_is_11be_supported(struct wlan_dfs *dfs)
{
	struct wlan_objmgr_psoc *psoc = dfs->dfs_soc_obj->psoc;
	struct wlan_lmac_if_reg_tx_ops *tx_ops;
	uint8_t phy_id;

	tx_ops = wlan_reg_get_tx_ops(psoc);
	if (tx_ops->get_phy_id_from_pdev_id)
		tx_ops->get_phy_id_from_pdev_id(
				psoc,
				wlan_objmgr_pdev_get_pdev_id(dfs->dfs_pdev_obj),
				&phy_id);
	else
		phy_id = wlan_objmgr_pdev_get_pdev_id(dfs->dfs_pdev_obj);

	if (tx_ops->is_chip_11be(psoc, phy_id))
		return true;

	return false;
}

/*
 * dfs_is_adfs_320mhz_supported() - Check if 320 MHz adfs supported
 *
 * @dfs: Pointer to dfs structure.
 * return: True if the 320 MHz adfs supported.
 */
static
bool dfs_is_adfs_320mhz_supported(struct wlan_dfs *dfs)
{
	return dfs->dfs_fw_adfs_support_320;
}
#else
static inline
bool dfs_is_adfs_320mhz_supported(struct wlan_dfs *dfs)
{
	return false;
}
#endif /* WLAN_FEATURE_11BE */

void dfs_zero_cac_attach(struct wlan_dfs *dfs)
{
	dfs->dfs_precac_timeout_override = -1;
	PRECAC_LIST_LOCK_CREATE(dfs);
	if (dfs_is_11be_supported(dfs))
	    dfs->dfs_agile_detector_id = AGILE_DETECTOR_11BE;
	else if (dfs_is_true_160mhz_supported(dfs))
		 dfs->dfs_agile_detector_id = AGILE_DETECTOR_ID_TRUE_160MHZ;
	else
		dfs->dfs_agile_detector_id = AGILE_DETECTOR_ID_80P80;
}

void dfs_zero_cac_detach(struct wlan_dfs *dfs)
{
	dfs_deinit_precac_list(dfs);
	PRECAC_LIST_LOCK_DESTROY(dfs);
}

void dfs_cancel_precac_timer(struct wlan_dfs *dfs)
{
	struct dfs_soc_priv_obj *dfs_soc_obj;

	dfs_soc_obj = dfs->dfs_soc_obj;
	qdf_hrtimer_cancel(&dfs_soc_obj->dfs_precac_timer);
	dfs_soc_obj->dfs_precac_timer_running = 0;
}

void dfs_set_precac_enable(struct wlan_dfs *dfs, bool precac_en)
{
	if (precac_en == dfs->dfs_agile_precac_ucfg) {
		dfs_info(dfs, WLAN_DEBUG_DFS_ALWAYS,
			 "PreCAC : %d is already configured", precac_en);
		return;
	}

	if (utils_get_dfsdomain(dfs->dfs_pdev_obj) != DFS_ETSI_DOMAIN) {
		dfs_info(dfs, WLAN_DEBUG_DFS_ALWAYS,
			 "preCAC: not supported in current DFS domain");
		return;
	}

	if (dfs_is_precac_timer_running(dfs) && !precac_en) {
		dfs_info(dfs, WLAN_DEBUG_DFS_ALWAYS,
			 "Precac flag changed. Cancel the precac timer");
#ifdef QCA_SUPPORT_AGILE_DFS
		dfs_agile_sm_deliver_evt(dfs->dfs_soc_obj,
					 DFS_AGILE_SM_EV_AGILE_STOP,
					 0, (void *)dfs);
#endif
	}

	dfs->dfs_agile_precac_ucfg = precac_en;
	dfs_info(dfs, WLAN_DEBUG_DFS_ALWAYS, "preCAC is %d", precac_en);
}
#endif

#if defined(ATH_SUPPORT_ZERO_CAC_DFS) && !defined(QCA_MCL_DFS_SUPPORT)
void dfs_zero_cac_timer_detach(struct dfs_soc_priv_obj *dfs_soc_obj)
{
	qdf_hrtimer_kill(&dfs_soc_obj->dfs_precac_timer);
}
#endif

#if defined(QCA_SUPPORT_AGILE_DFS) || defined(ATH_SUPPORT_ZERO_CAC_DFS)
bool dfs_is_precac_domain(struct wlan_dfs *dfs)
{
	enum dfs_reg dfsdomain = utils_get_dfsdomain(dfs->dfs_pdev_obj);

	if (dfsdomain == DFS_ETSI_REGION)
		return true;
	return false;
}
#endif

#ifdef QCA_SUPPORT_AGILE_DFS
bool dfs_is_agile_precac_enabled(struct wlan_dfs *dfs)
{
	enum dfs_reg dfsdomain = utils_get_dfsdomain(dfs->dfs_pdev_obj);

	if (dfsdomain != DFS_ETSI_REGION)
		return false;

	return (dfs->dfs_agile_precac_ucfg && dfs->dfs_fw_adfs_support_non_160);
}
#endif

#ifdef CONFIG_CHAN_FREQ_API
/* dfs_find_curchwidth_and_center_chan_for_freq() - Find channel width and
 *                                                  center channelfrequency.
 * @dfs: Pointer to wlan_dfs.
 * @chwidth: Pointer to phy_ch_width.
 * @primary_chan_freq: Pointer to primary channel.
 * @secondary_chan_freq: Pointer to secondary channel.
 */
void
dfs_find_curchwidth_and_center_chan_for_freq(struct wlan_dfs *dfs,
					     enum phy_ch_width *chwidth,
					     qdf_freq_t *primary_chan_freq,
					     qdf_freq_t *secondary_chan_freq)
{
	struct dfs_channel *curchan = dfs->dfs_curchan;

	if (!curchan) {
		dfs_err(dfs, WLAN_DEBUG_DFS_ALWAYS, "curchan is NULL");
		return;
	}

	if (primary_chan_freq)
		*primary_chan_freq = curchan->dfs_ch_mhz_freq_seg1;
	if (WLAN_IS_CHAN_MODE_20(curchan)) {
		*chwidth = CH_WIDTH_20MHZ;
	} else if (WLAN_IS_CHAN_MODE_40(curchan)) {
		*chwidth = CH_WIDTH_40MHZ;
	} else if (WLAN_IS_CHAN_MODE_80(curchan)) {
		*chwidth = CH_WIDTH_80MHZ;
	} else if (WLAN_IS_CHAN_MODE_80_80(curchan)) {
		*chwidth = CH_WIDTH_80P80MHZ;
		if (secondary_chan_freq)
			*secondary_chan_freq =
				curchan->dfs_ch_mhz_freq_seg2;
	} else if (WLAN_IS_CHAN_MODE_160(curchan)) {
		*chwidth = CH_WIDTH_160MHZ;
		if (primary_chan_freq)
			*primary_chan_freq =
				curchan->dfs_ch_mhz_freq_seg2;
	} else if (WLAN_IS_CHAN_MODE_320(curchan)) {
		*chwidth = CH_WIDTH_320MHZ;
		if (primary_chan_freq)
			*primary_chan_freq = curchan->dfs_ch_mhz_freq_seg2;
	}

}
#endif

#ifdef QCA_SUPPORT_AGILE_DFS
void dfs_find_pdev_for_agile_precac(struct wlan_objmgr_pdev *pdev,
				    uint8_t *cur_agile_dfs_index)
{
	struct wlan_dfs *dfs;
	struct dfs_soc_priv_obj *dfs_soc_obj;
	struct wlan_objmgr_psoc *psoc;

	dfs = wlan_pdev_get_dfs_obj(pdev);
	psoc = wlan_pdev_get_psoc(pdev);

	dfs_soc_obj = dfs->dfs_soc_obj;

	*cur_agile_dfs_index =
	   (dfs_soc_obj->cur_agile_dfs_index + 1) % dfs_soc_obj->num_dfs_privs;
}

void dfs_fill_adfs_completion_params(struct wlan_dfs *dfs,
				     enum ocac_status_type ocac_status)
{
	struct adfs_completion_params *adfs_completion_status;
	qdf_freq_t ch_freq = dfs->dfs_agile_precac_freq_mhz;

	adfs_completion_status = &dfs->adfs_completion_status;

	adfs_completion_status->ocac_status = ocac_status;
	adfs_completion_status->center_freq1 =
		(ch_freq == RESTRICTED_80P80_CHAN_CENTER_FREQ) ?
		(RESTRICTED_80P80_LEFT_80_CENTER_FREQ) : ch_freq;
	adfs_completion_status->center_freq2 =
		(ch_freq == RESTRICTED_80P80_CHAN_CENTER_FREQ) ?
		(RESTRICTED_80P80_RIGHT_80_CENTER_FREQ) : 0;
	adfs_completion_status->chan_width = dfs->dfs_precac_chwidth;
}

void dfs_fill_adfs_chan_params(struct wlan_dfs *dfs,
			       struct dfs_agile_cac_params *adfs_param)
{
	qdf_freq_t ch_freq = dfs->dfs_agile_precac_freq_mhz;

	adfs_param->precac_center_freq_1 =
		(ch_freq == RESTRICTED_80P80_CHAN_CENTER_FREQ) ?
		(RESTRICTED_80P80_LEFT_80_CENTER_FREQ) : ch_freq;
	adfs_param->precac_center_freq_2 =
		(ch_freq == RESTRICTED_80P80_CHAN_CENTER_FREQ) ?
		(RESTRICTED_80P80_RIGHT_80_CENTER_FREQ) : 0;
	adfs_param->precac_chan = utils_dfs_freq_to_chan(ch_freq);
	adfs_param->precac_chwidth = dfs->dfs_precac_chwidth;
}

void dfs_agile_precac_cleanup(struct wlan_dfs *dfs)
{
	struct dfs_soc_priv_obj *dfs_soc_obj;

	dfs_soc_obj = dfs->dfs_soc_obj;
	dfs_soc_obj->precac_state_started = 0;
	dfs->dfs_agile_precac_freq_mhz = 0;
	dfs->dfs_precac_chwidth = CH_WIDTH_INVALID;
	dfs_soc_obj->ocac_status = OCAC_RESET;
}

/*
 * dfs_prepare_agile_precac_chan() - Prepare an agile channel for preCAC.
 * @dfs: Pointer to wlan_dfs.
 *
 * Return Type: void
 */
void  dfs_prepare_agile_precac_chan(struct wlan_dfs *dfs, bool *is_chan_found)
{
	struct wlan_objmgr_psoc *psoc;
	struct wlan_objmgr_pdev *pdev;
	struct wlan_dfs *temp_dfs;
	struct dfs_soc_priv_obj *dfs_soc_obj;
	struct wlan_lmac_if_dfs_tx_ops *dfs_tx_ops;
	uint16_t ch_freq = 0;
	uint8_t cur_agile_dfs_idx = 0;
	uint16_t vhtop_ch_freq_seg1, vhtop_ch_freq_seg2;
	int i;
	struct dfs_agile_cac_params adfs_param;

	psoc = wlan_pdev_get_psoc(dfs->dfs_pdev_obj);
	dfs_soc_obj = dfs->dfs_soc_obj;

	dfs_tx_ops = wlan_psoc_get_dfs_txops(psoc);

	pdev = dfs->dfs_pdev_obj;

	for (i = 0; i < dfs_soc_obj->num_dfs_privs; i++) {
		dfs_find_pdev_for_agile_precac(pdev, &cur_agile_dfs_idx);
		dfs_soc_obj->cur_agile_dfs_index = cur_agile_dfs_idx;
		temp_dfs = dfs_soc_obj->dfs_priv[cur_agile_dfs_idx].dfs;
		pdev = temp_dfs->dfs_pdev_obj;
		if (!pdev) {
			dfs_debug(dfs, WLAN_DEBUG_DFS_AGILE, "pdev at index: %u is NULL\n", cur_agile_dfs_idx);
			*is_chan_found = false;
			return;
		}
		if (!dfs_soc_obj->dfs_priv[cur_agile_dfs_idx].agile_precac_active) {
			continue;
		}
		if (!dfs_mlme_is_pdev_valid(pdev)) {
			dfs_debug(dfs, WLAN_DEBUG_DFS_AGILE, "pdev: %p is not valid in current hwmode\n", pdev);
			continue;
		}
		vhtop_ch_freq_seg1 =
			temp_dfs->dfs_curchan->dfs_ch_mhz_freq_seg1;
		vhtop_ch_freq_seg2 =
			temp_dfs->dfs_curchan->dfs_ch_mhz_freq_seg2;
		if (WLAN_IS_CHAN_MODE_160(temp_dfs->dfs_curchan)) {
			if (vhtop_ch_freq_seg2 < vhtop_ch_freq_seg1)
				vhtop_ch_freq_seg2 -=
					DFS_160MHZ_SECSEG_CHAN_OFFSET;
			else
				vhtop_ch_freq_seg2 +=
					DFS_160MHZ_SECSEG_CHAN_OFFSET;
		}
		dfs_set_agilecac_chan_for_freq(temp_dfs,
					       &ch_freq,
					       vhtop_ch_freq_seg1,
					       vhtop_ch_freq_seg2);

		if (!ch_freq) {
			qdf_info(" %s : %d No preCAC required channels left in current pdev: %pK",
				 __func__, __LINE__, pdev);
			continue;
		} else {
			break;
		}
	}

	if (ch_freq) {
		dfs_fill_adfs_chan_params(temp_dfs, &adfs_param);
		dfs_start_agile_precac_timer(temp_dfs,
					     dfs->dfs_soc_obj->ocac_status,
					     &adfs_param);
		qdf_info("%s : %d ADFS channel set request sent for pdev: %pK ch_freq: %d",
			 __func__, __LINE__, pdev, ch_freq);

		if (dfs_tx_ops && dfs_tx_ops->dfs_agile_ch_cfg_cmd)
			dfs_tx_ops->dfs_agile_ch_cfg_cmd(pdev,
							 &adfs_param);
		else
			dfs_err(NULL, WLAN_DEBUG_DFS_ALWAYS,
				"dfs_tx_ops=%pK", dfs_tx_ops);
		*is_chan_found = true;
	} else {
		dfs_cancel_precac_timer(dfs);
		dfs_soc_obj->cur_agile_dfs_index = DFS_PSOC_NO_IDX;
		dfs_agile_precac_cleanup(dfs);
		*is_chan_found = false;
	}
}
#endif

uint8_t dfs_find_subchannels_for_center_freq(qdf_freq_t pri_center_freq,
					     qdf_freq_t sec_center_freq,
					     enum phy_ch_width ch_width,
					     qdf_freq_t *channels)
{
	uint8_t nchannels = 0;

	switch (ch_width) {
	case CH_WIDTH_20MHZ:
		nchannels = 1;
		channels[0] = pri_center_freq;
		break;
	case CH_WIDTH_40MHZ:
		nchannels = 2;
		channels[0] = pri_center_freq - DFS_5GHZ_NEXT_CHAN_FREQ_OFFSET;
		channels[1] = pri_center_freq + DFS_5GHZ_NEXT_CHAN_FREQ_OFFSET;
		break;
	case CH_WIDTH_80MHZ:
		nchannels = 4;
		channels[0] = pri_center_freq - DFS_5GHZ_2ND_CHAN_FREQ_OFFSET;
		channels[1] = pri_center_freq - DFS_5GHZ_NEXT_CHAN_FREQ_OFFSET;
		channels[2] = pri_center_freq + DFS_5GHZ_NEXT_CHAN_FREQ_OFFSET;
		channels[3] = pri_center_freq + DFS_5GHZ_2ND_CHAN_FREQ_OFFSET;
		break;
	case CH_WIDTH_80P80MHZ:
		nchannels = 8;
		channels[0] = pri_center_freq - DFS_5GHZ_2ND_CHAN_FREQ_OFFSET;
		channels[1] = pri_center_freq - DFS_5GHZ_NEXT_CHAN_FREQ_OFFSET;
		channels[2] = pri_center_freq + DFS_5GHZ_NEXT_CHAN_FREQ_OFFSET;
		channels[3] = pri_center_freq + DFS_5GHZ_2ND_CHAN_FREQ_OFFSET;
		/* secondary channels */
		channels[4] = sec_center_freq - DFS_5GHZ_2ND_CHAN_FREQ_OFFSET;
		channels[5] = sec_center_freq - DFS_5GHZ_NEXT_CHAN_FREQ_OFFSET;
		channels[6] = sec_center_freq + DFS_5GHZ_NEXT_CHAN_FREQ_OFFSET;
		channels[7] = sec_center_freq + DFS_5GHZ_2ND_CHAN_FREQ_OFFSET;
		break;
	case CH_WIDTH_160MHZ:
		nchannels = 8;
		channels[0] = pri_center_freq - DFS_5GHZ_4TH_CHAN_FREQ_OFFSET;
		channels[1] = pri_center_freq - DFS_5GHZ_3RD_CHAN_FREQ_OFFSET;
		channels[2] = pri_center_freq - DFS_5GHZ_2ND_CHAN_FREQ_OFFSET;
		channels[3] = pri_center_freq - DFS_5GHZ_NEXT_CHAN_FREQ_OFFSET;
		channels[4] = pri_center_freq + DFS_5GHZ_NEXT_CHAN_FREQ_OFFSET;
		channels[5] = pri_center_freq + DFS_5GHZ_2ND_CHAN_FREQ_OFFSET;
		channels[6] = pri_center_freq + DFS_5GHZ_3RD_CHAN_FREQ_OFFSET;
		channels[7] = pri_center_freq + DFS_5GHZ_4TH_CHAN_FREQ_OFFSET;
		break;
	case CH_WIDTH_320MHZ:
		nchannels = 16;
		channels[0] = pri_center_freq - DFS_5GHZ_8TH_CHAN_FREQ_OFFSET;
		channels[1] = pri_center_freq - DFS_5GHZ_7TH_CHAN_FREQ_OFFSET;
		channels[2] = pri_center_freq - DFS_5GHZ_6TH_CHAN_FREQ_OFFSET;
		channels[3] = pri_center_freq - DFS_5GHZ_5TH_CHAN_FREQ_OFFSET;
		channels[4] = pri_center_freq - DFS_5GHZ_4TH_CHAN_FREQ_OFFSET;
		channels[5] = pri_center_freq - DFS_5GHZ_3RD_CHAN_FREQ_OFFSET;
		channels[6] = pri_center_freq - DFS_5GHZ_2ND_CHAN_FREQ_OFFSET;
		channels[7] = pri_center_freq - DFS_5GHZ_NEXT_CHAN_FREQ_OFFSET;
		channels[8] = pri_center_freq + DFS_5GHZ_NEXT_CHAN_FREQ_OFFSET;
		channels[9] = pri_center_freq + DFS_5GHZ_2ND_CHAN_FREQ_OFFSET;
		channels[10] = pri_center_freq + DFS_5GHZ_3RD_CHAN_FREQ_OFFSET;
		channels[11] = pri_center_freq + DFS_5GHZ_4TH_CHAN_FREQ_OFFSET;
		channels[12] = pri_center_freq + DFS_5GHZ_5TH_CHAN_FREQ_OFFSET;
		channels[13] = pri_center_freq + DFS_5GHZ_6TH_CHAN_FREQ_OFFSET;
		channels[14] = pri_center_freq + DFS_5GHZ_7TH_CHAN_FREQ_OFFSET;
		channels[15] = pri_center_freq + DFS_5GHZ_8TH_CHAN_FREQ_OFFSET;
		break;
	default:
		dfs_err(NULL, WLAN_DEBUG_DFS_ALWAYS, "invalid channel width");
		break;
	}

	return nchannels;
}

#ifdef QCA_SUPPORT_AGILE_DFS
#ifdef CONFIG_CHAN_FREQ_API
/* Checks the Host side agile configurations. ie if agile channel
 * is configured as 5730MHz and the agile channel width is 80P80/165MHz.
 */
#define IS_HOST_AGILE_CURCHAN_165MHZ(_x) \
	(((_x)->dfs_agile_precac_freq_mhz == \
	 RESTRICTED_80P80_CHAN_CENTER_FREQ) && \
	((_x)->dfs_precac_chwidth == CH_WIDTH_80P80MHZ))

/* Checks if the FW Agile operation was on the restricited 80P80MHz,
 * by comparing the center frequency 1 with 5690MHz, center frequency 2
 * with 5775MHz and the channel width was 80P80/165MHz.
 */
#define IS_OCAC_EVENT_ON_165_MHZ_CHAN(_x, _y, _z) \
	(((_x) == RESTRICTED_80P80_LEFT_80_CENTER_FREQ) && \
	((_y) == RESTRICTED_80P80_RIGHT_80_CENTER_FREQ) && \
	((_z) == CH_WIDTH_80P80MHZ))

/*
 * dfs_is_ocac_complete_event_for_cur_agile_chan() - Check if the OCAC
 * completion event from FW is received for the currently configured agile
 * channel in host.
 *
 * @dfs: Pointer to dfs structure.
 * @center_freq_mhz1: Center frequency of the band when the precac width is
 * 20/40/80/160MHz and center frequency of the left 80MHz in case of restricted
 * 80P80/165MHz.
 * @center_freq_mhz2: Center frequency of the right 80MHz in case of restricted
 * 80P80/165MHz. It is zero for other channel widths.
 * @chwidth: Agile channel width for which the completion event is received.
 *
 * return: True if the channel on which OCAC completion event received is same
 * as currently configured agile channel in host. False otherwise.
 */
bool
dfs_is_ocac_complete_event_for_cur_agile_chan(struct wlan_dfs *dfs)
{
	struct adfs_completion_params *adfs_completion_status;

	adfs_completion_status = &dfs->adfs_completion_status;
	if (IS_HOST_AGILE_CURCHAN_165MHZ(dfs) &&
	    IS_OCAC_EVENT_ON_165_MHZ_CHAN(adfs_completion_status->center_freq1,
					  adfs_completion_status->center_freq2,
					  adfs_completion_status->chan_width))
		return true;
	else if (dfs->dfs_agile_precac_freq_mhz == adfs_completion_status->center_freq1)
		return true;
	else
		return false;
}

/*
 * dfs_process_ocac_complete() - Process OCAC Complete eventid.
 * @pdev: Pointer to wlan_objmgr_pdev.
 * @ocac_status: OCAC Status.
 * @center_freq_mhz1: Center frequency of the band when the precac width is
 * 20/40/80/160MHz and center frequency of the left 80MHz in case of restricted
 * 80P80/165MHz.
 * @center_freq_mhz2: Center frequency of the right 80MHz in case of restricted
 * 80P80/165MHz. It is zero for other channel widths.
 * @chwidth: Agile channel width for which the completion event is received.
 */
#ifdef QCA_SUPPORT_AGILE_DFS
void dfs_process_ocac_complete(struct wlan_objmgr_pdev *pdev,
			       enum ocac_status_type ocac_status,
			       uint32_t center_freq_mhz1,
			       uint32_t center_freq_mhz2,
			       enum phy_ch_width chwidth)
{
	struct wlan_dfs *dfs = NULL;
	struct dfs_soc_priv_obj *dfs_soc_obj;
	struct adfs_completion_params *adfs_completion_status;

	dfs = wlan_pdev_get_dfs_obj(pdev);
	dfs_soc_obj = dfs->dfs_soc_obj;
	adfs_completion_status = &dfs->adfs_completion_status;

	if (ocac_status == OCAC_RESET) {
		dfs_debug(NULL, WLAN_DEBUG_DFS_ALWAYS,
			  "PreCAC timer reset, Sending Agile chan set command");
	} else if (ocac_status == OCAC_CANCEL) {
		dfs_debug(NULL, WLAN_DEBUG_DFS_ALWAYS,
			  "PreCAC timer abort, agile precac stopped");
	} else if (ocac_status == OCAC_SUCCESS) {
		dfs_debug(NULL, WLAN_DEBUG_DFS_ALWAYS,
			  "PreCAC timer Completed for agile freq: %d %d",
			  center_freq_mhz1,
			  center_freq_mhz2);
	} else {
		dfs_debug(NULL, WLAN_DEBUG_DFS_ALWAYS, "Error Unknown");
	}

	adfs_completion_status->ocac_status = ocac_status;
	adfs_completion_status->center_freq1 = center_freq_mhz1;
	adfs_completion_status->center_freq2 = center_freq_mhz2;
	adfs_completion_status->chan_width = chwidth;
	dfs_soc_obj->ocac_status = ocac_status;
	dfs_cancel_precac_timer(dfs);
	dfs_agile_sm_deliver_evt(dfs_soc_obj,
				 DFS_AGILE_SM_EV_AGILE_DONE,
				 0, (void *)dfs);
}
#endif
#endif
#endif

/*
 * dfs_find_precac_secondary_vht80_chan() - Find preCAC secondary VHT80 channel.
 * @dfs: Pointer to wlan_dfs.
 * @chan: Pointer to dfs_channel.
 */
#define VHT80_FREQ_OFFSET 30
void dfs_find_precac_secondary_vht80_chan(struct wlan_dfs *dfs,
					  struct dfs_channel *chan)
{
	uint8_t first_primary_dfs_ch_freq;

	first_primary_dfs_ch_freq =
		dfs->dfs_precac_secondary_freq_mhz - VHT80_FREQ_OFFSET;

	dfs_mlme_find_dot11_chan_for_freq(dfs->dfs_pdev_obj,
					  first_primary_dfs_ch_freq, 0,
					  WLAN_PHYMODE_11AC_VHT80,
					  &chan->dfs_ch_freq,
					  &chan->dfs_ch_flags,
					  &chan->dfs_ch_flagext,
					  &chan->dfs_ch_ieee,
					  &chan->dfs_ch_vhtop_ch_freq_seg1,
					  &chan->dfs_ch_vhtop_ch_freq_seg2,
					  &chan->dfs_ch_mhz_freq_seg1,
					  &chan->dfs_ch_mhz_freq_seg2);
}

/*
 * dfs_precac_csa() - Intitiate CSA for preCAC channel switch.
 * @dfs: pointer to wlan_dfs.
 */
#ifdef WLAN_DFS_PRECAC_AUTO_CHAN_SUPPORT
void dfs_precac_csa(struct wlan_dfs *dfs)
{
	/*
	 * Save current chan into intermediate chan, so that
	 * next time a DFS channel needs preCAC, there is no channel switch
	 * until preCAC finishes.
	 */
	dfs->dfs_precac_inter_chan_freq = dfs->dfs_autoswitch_chan->dfs_ch_freq;
	dfs_debug(dfs, WLAN_DEBUG_DFS,
		  "Use %d as intermediate channel for further channel changes",
		  dfs->dfs_precac_inter_chan_freq);

	if (global_dfs_to_mlme.mlme_precac_chan_change_csa_for_freq)
		global_dfs_to_mlme.mlme_precac_chan_change_csa_for_freq
			(dfs->dfs_pdev_obj,
			 dfs->dfs_autoswitch_chan->dfs_ch_freq,
			 dfs->dfs_autoswitch_chan->dfs_ch_mhz_freq_seg2,
			 dfs->dfs_autoswitch_des_mode);
	qdf_mem_free(dfs->dfs_autoswitch_chan);
	dfs->dfs_autoswitch_chan = NULL;
}
#endif

#ifdef QCA_DFS_BW_EXPAND
/**
 * dfs_is_completed_agile_within_target_band() - Check the frequency of last
 * agile completed channel is within the range of User configured channel.
 * @dfs: pointer to wlan_dfs.
 * @target_ch_width: User configured target channel width.
 */
static
bool dfs_is_completed_agile_within_target_band(struct wlan_dfs *dfs,
					       enum phy_ch_width target_ch_width)
{
	const struct bonded_channel_freq *target_bonded_chan_ptr;
	qdf_freq_t agile_precac_freq = dfs->dfs_agile_precac_freq_mhz;

	/*
	 * For BW 20MHz, Check if the last completed agile frequency is
	 * same as user configured frequency. If both are same frequency,
	 * check if CAC is completed in the user configured frequency.
	 */
	if (target_ch_width == CH_WIDTH_20MHZ)
	    return agile_precac_freq == dfs->dfs_bw_expand_target_freq;

	/*
	 * For BW greater than 20Mhz, check if the last completed agile frequency
	 * is within the range of user configured channel.
	 */
	wlan_reg_get_5g_bonded_channel_and_state_for_pwrmode(dfs->dfs_pdev_obj,
							  dfs->dfs_bw_expand_target_freq,
							  target_ch_width,
							  &target_bonded_chan_ptr,
							  REG_CURRENT_PWR_MODE,
							  NO_SCHANS_PUNC);

	if (target_bonded_chan_ptr)
		return agile_precac_freq >= target_bonded_chan_ptr->start_freq &&
			agile_precac_freq <= target_bonded_chan_ptr->end_freq;
	else
		return false;
}

/**
 * dfs_bwexpand_determine_primary_freq() - Find the primary frequency for
 * expanding the bandwidth and channel change.
 * @dfs: pointer to wlan_dfs.
 * @target_mode: phymode of type wlan_phymode.
 */
static
qdf_freq_t dfs_bwexpand_determine_primary_freq(struct wlan_dfs *dfs,
					       enum wlan_phymode target_mode)
{
	qdf_freq_t primary_freq;

	/* User configured phymode is found and set to target mode. */
	if (target_mode == dfs->dfs_bw_expand_des_mode)
		primary_freq = dfs->dfs_bw_expand_target_freq;
	else if (dfs->dfs_precac_chwidth == CH_WIDTH_20MHZ)
		primary_freq = dfs->dfs_agile_precac_freq_mhz;
	else
		primary_freq = dfs->dfs_agile_precac_freq_mhz - HALF_20MHZ_BW;

	return primary_freq;
}

/**
 * dfs_get_configured_bwexpand_dfs_chan() - Get a DFS chan when frequency and
 * phymode is provided.
 * @dfs: pointer to wlan_dfs.
 * @user_chan: pointer to dfs_channel.
 * @target_mode: phymode of type wlan_phymode.
 */
bool dfs_get_configured_bwexpand_dfs_chan(struct wlan_dfs *dfs,
					  struct dfs_channel *user_chan,
					  enum wlan_phymode target_mode)
{
	qdf_freq_t primary_freq = dfs_bwexpand_determine_primary_freq(dfs,
								  target_mode);
	qdf_mem_zero(user_chan, sizeof(struct dfs_channel));
	if (QDF_STATUS_SUCCESS !=
		dfs_mlme_find_dot11_chan_for_freq(dfs->dfs_pdev_obj,
						  primary_freq,
						  0,
						  target_mode,
						  &user_chan->dfs_ch_freq,
						  &user_chan->dfs_ch_flags,
						  &user_chan->dfs_ch_flagext,
						  &user_chan->dfs_ch_ieee,
						  &user_chan->dfs_ch_vhtop_ch_freq_seg1,
						  &user_chan->dfs_ch_vhtop_ch_freq_seg2,
						  &user_chan->dfs_ch_mhz_freq_seg1,
						  &user_chan->dfs_ch_mhz_freq_seg2)) {
		dfs_err(dfs, WLAN_DEBUG_DFS_ALWAYS,
			"Channel %d not found for mode %d and cfreq2 %d",
			dfs->dfs_bw_expand_target_freq,
			target_mode,
			0);
		return false;
	}
	return true;
}

/**
 * dfs_bwexpand_do_csa() - Execute BW Expansion by moving to Channel with
 * higher bandwidth.
 * @dfs: pointer to wlan_dfs.
 * @primary_freq: Frequency of type qdf_freq_t.
 * @target_mode: phymode of type wlan_phymode.
 */
static
void dfs_bwexpand_do_csa(struct wlan_dfs *dfs,
			 qdf_freq_t primary_freq,
			 enum wlan_phymode target_mode)
{
	if (global_dfs_to_mlme.mlme_postnol_chan_switch)
		global_dfs_to_mlme.mlme_postnol_chan_switch(
				dfs->dfs_pdev_obj,
				primary_freq,
				0,
				target_mode);
}

/**
 * dfs_expand_find_max_possible_target_mode() - Find the target mode to perform
 * BW Expand based on certain conditions. If the conditions are not met, reduce
 * the target mode and check the conditions again.
 * @dfs: pointer to wlan_dfs.
 * @target_mode: phymode of type wlan_phymode.
 */
static
enum wlan_phymode dfs_expand_find_max_possible_target_mode(struct wlan_dfs *dfs,
							   enum wlan_phymode target_mode)
{
	while (target_mode < WLAN_PHYMODE_MAX) {
		struct dfs_channel user_chan;
		bool is_chan_found;

		is_chan_found =
			dfs_get_configured_bwexpand_dfs_chan(dfs,
							     &user_chan,
							     target_mode);
		if (!is_chan_found)
			return WLAN_PHYMODE_MAX;

		if (dfs_is_agile_rcac_enabled(dfs) &&
		    dfs_is_rcac_cac_done(dfs, &user_chan, dfs->dfs_curchan))
			break;
		else if (dfs_is_precac_done(dfs, &user_chan))
			break;

		target_mode = dfs_phymode_converter[target_mode];
	}
	return target_mode;
}

/**
 * dfs_find_subtract_subchan_list_index() - Find the subchannel index of the
 * RCAC channel in which CAC check is done.
 * Example: Target subchans = [100, 104, 108, 112] , current subchannel = [100]
 * The subtracted n_suchans = [104, 108, 112] and index is returned as 1.
 * @chan: Pointer to dfs_channel object of target channel.
 * @subset_chan: Pointer to dfs_channel object of subset channel.
 * @target_freq_list: Pointer to target_freq_list array.
 * @cur_freq_list: Pointer to cur_freq_list array.
 * @n_subchans: Ending value of subchannel list index.
 */
static
uint8_t dfs_find_subtract_subchan_list_index(struct dfs_channel *chan,
					     struct dfs_channel *subset_chan,
					     qdf_freq_t *target_freq_list,
					     qdf_freq_t *cur_freq_list,
					     uint8_t *n_subchans)
{
	uint8_t n_target_channels, n_cur_channels;
	uint8_t i = 0;

	n_target_channels =
		dfs_get_bonding_channel_without_seg_info_for_freq(chan,
								  target_freq_list);
	n_cur_channels =
		dfs_get_bonding_channel_without_seg_info_for_freq(subset_chan,
								  cur_freq_list);
	while (i < n_cur_channels) {
		if (target_freq_list[i] == cur_freq_list[i])
			i++;
		else
			break;
	}

	*n_subchans = n_target_channels - i;
	return i;
}

/**
 * dfs_is_rcac_done_on_subchan_list() - Check if RCAC is completed on
 * subchannel list.
 * @dfs: pointer to wlan_dfs.
 * @target_freq_list: Pointer to target_freq_list array.
 * @n_subchans: Ending value of subchannel list index.
 */
static
bool dfs_is_rcac_done_on_subchan_list(struct wlan_dfs *dfs,
				      qdf_freq_t *target_freq_list,
				      uint8_t n_subchans)
{
	uint8_t i;

	if (n_subchans >= MAX_20MHZ_SUBCHANS)
		return false;

	for (i = 0; i < n_subchans; i++) {
		if (!dfs_is_precac_done_on_non_80p80_chan_for_freq(dfs,
							target_freq_list[i]))
			return false;
	}
	return true;
}

/**
 * dfs_is_rcac_cac_done API checks CAC is completed only on the RCAC channel
 * and excludes current operating channel.
 *
 * Example: Let's consider User configured chan is 100 HT160 and current
 * operating channel is 100 HT80 and RCAC completed chan is 120 HT80.
 * There is a possibilty for bandwidth expansion from 80Mhz to 160Mhz.
 * Check CAC is completed only on RCAC channel because current operating
 * chan already completed CAC.
 */
bool dfs_is_rcac_cac_done(struct wlan_dfs *dfs,
			  struct dfs_channel *chan,
			  struct dfs_channel *subset_chan)
{
	qdf_freq_t target_freq_list[MAX_20MHZ_SUBCHANS];
	qdf_freq_t cur_freq_list[MAX_20MHZ_SUBCHANS];
	uint8_t n_subchans, subtract_chan_idx;

	subtract_chan_idx = dfs_find_subtract_subchan_list_index(chan,
								 subset_chan,
								 target_freq_list,
								 cur_freq_list,
								 &n_subchans);
	if (subtract_chan_idx >= MAX_20MHZ_SUBCHANS)
		return false;

	return dfs_is_rcac_done_on_subchan_list(dfs,
						&target_freq_list[subtract_chan_idx],
						n_subchans);
}

/**
 * WORKING SEQUENCE :
 *
 * 1) When preCAC is completed on a channel. The event DFS_AGILE_SM_EV_AGILE_DONE
 * is received and AGILE SM moved to INIT STATE. If BW_EXPAND feature is enabled,
 * dfs_bwexpand_try_jumping_to_target_subchan is called.
 *
 * 2) Calculate the Target Channel width Using a dfs_phymode_decoupler similar to
 * phymode_decoupler to get Channel width from wlan_phymode.
 *
 * 3) Get the Start and end freq of the bonded channel of User configured freq.
 * Check if the freq of last completed agileCAC is in between the target bonded
 * channel.
 *
 * Example : Lets assume, the preCAC is completed in Chan 140 HT20, Then the
 * last agileCAC completed chan info is stored in dfs_agile_precac_freq_mhz.
 *
 * The API wlan_reg_get_5g_bonded_channel_and_state_for_freq takes User configured
 * freq (dfs_bw_expand_target_freq) as input and provides a bonded channel pointer
 * with Start and end freq of the bonded channel.
 *
 * For example, User configured chan is 100 HT160, then the bonded channel ptr
 * will return start freq as 5490Mhz and end freq as 5650Mhz
 *
 * Check the dfs_agile_precac_freq_mhz is in the range of bonded channel ptr.
 * In this case, 5500Mhz is in between 5490Mhz and 5650Mhz. Therefore goto
 * step 4.
 *
 * 4) Create DFS channel for User configured freq and reduced User configured mode.
 *
 * 5) Check if the DFS channel is done preCAC or not
 *    i) If preCAC is done -> Break
 *    ii) If preCAC is not done -> Reduce the Target phymode
 *
 * 6) Do Channel change by calling mlme_precac_chan_change_csa_for_freq.
 */
bool dfs_bwexpand_try_jumping_to_target_subchan(struct wlan_dfs *dfs)
{
	qdf_freq_t primary_freq;
	enum phy_ch_width target_ch_width;
	enum wlan_phymode target_mode;

	if (!dfs->dfs_use_bw_expand)
		return false;

	target_mode = dfs->dfs_bw_expand_des_mode;
	if (target_mode >= WLAN_PHYMODE_MAX) {
		dfs_debug(dfs, WLAN_DEBUG_DFS, "target_mode =%d", target_mode);
		return false;
	}

	target_ch_width = dfs_phymode_decoupler[target_mode][CH_WIDTH_COL];
	if (!dfs_is_completed_agile_within_target_band(dfs, target_ch_width))
		return false;

	target_mode = dfs_expand_find_max_possible_target_mode(dfs, target_mode);
	if (target_mode >= WLAN_PHYMODE_MAX) {
		dfs_debug(dfs, WLAN_DEBUG_DFS, "target_mode =%d", target_mode);
		return false;
	}

	primary_freq = dfs_bwexpand_determine_primary_freq(dfs, target_mode);
	dfs_bwexpand_do_csa(dfs, primary_freq, target_mode);

	return true;
}
#endif

#ifdef WLAN_DFS_PRECAC_AUTO_CHAN_SUPPORT
bool dfs_precac_check_home_chan_change(struct wlan_dfs *dfs)
{
	struct dfs_channel *chan = dfs->dfs_autoswitch_chan;

	if (chan && dfs_is_precac_done(dfs, chan)) {
		dfs_precac_csa(dfs);
		dfs->dfs_soc_obj->precac_state_started = false;
		return true;
	}
	return false;
}
#endif

/**
 * dfs_precac_timeout() - Precac timeout.
 *
 * Removes the channel from precac_required list and adds it to the
 * precac_done_list. Triggers a precac channel change.
 */
static enum qdf_hrtimer_restart_status
dfs_precac_timeout(qdf_hrtimer_data_t *arg)
{
	struct dfs_soc_priv_obj *dfs_soc_obj;
	uint32_t current_time;
	struct wlan_dfs *dfs;

	dfs_soc_obj = container_of(arg,
				   struct dfs_soc_priv_obj,
				   dfs_precac_timer);

	dfs_soc_obj->dfs_precac_timer_running = 0;
	dfs = dfs_soc_obj->dfs_priv[dfs_soc_obj->cur_agile_dfs_index].dfs;

	if (dfs_is_agile_precac_enabled(dfs)) {
		current_time = qdf_system_ticks_to_msecs(qdf_system_ticks());
		dfs_info(dfs, WLAN_DEBUG_DFS_ALWAYS,
			 "Pre-cac expired, Agile Precac chan %u curr time %d",
			 dfs->dfs_agile_precac_freq_mhz,
			 current_time / 1000);
		dfs_agile_sm_deliver_evt(dfs_soc_obj,
					 DFS_AGILE_SM_EV_AGILE_DONE,
					 0, (void *)dfs);
	}

	return QDF_HRTIMER_NORESTART;
}

#if defined(ATH_SUPPORT_ZERO_CAC_DFS) && !defined(QCA_MCL_DFS_SUPPORT)
void dfs_zero_cac_timer_init(struct dfs_soc_priv_obj *dfs_soc_obj)
{

	dfs_soc_obj->precac_state_started = false;

	qdf_hrtimer_init(&dfs_soc_obj->dfs_precac_timer,
			 dfs_precac_timeout,
			 QDF_CLOCK_MONOTONIC,
			 QDF_HRTIMER_MODE_REL,
			 QDF_CONTEXT_TASKLET);
}
#endif

/* dfs fill_precac_tree_for_entry() - Fill precac entry tree (level insertion).
 * @dfs:     WLAN DFS structure
 * @ch_ieee: root_node ieee channel.
 *
 * Since every node at a tree level is equally spaced (fixed BW for a level),
 * insertion of tree nodes are level order insertion.
 * For each depth starting from root depth (0),
 *       1. start from initial chan offset and fill node with ch_ieee
 *          provided and this offset.
 *       2. increment offset with next chan offset and fill node
 *       3. repeat step 2 till boundary offset is reached.
 *
 * If the above sequence is not maintained, the tree will not be balanced
 * as expected and would require rebalancing. Hence maintain the above
 * sequence for insertion.
 *
 */

#if defined(QCA_SUPPORT_AGILE_DFS) || defined(ATH_SUPPORT_ZERO_CAC_DFS)
void dfs_agile_soc_obj_init(struct wlan_dfs *dfs,
			    struct wlan_objmgr_psoc *psoc)
{
	struct dfs_soc_priv_obj *dfs_soc_obj;

	dfs_soc_obj = dfs->dfs_soc_obj;
	dfs->dfs_psoc_idx = dfs_soc_obj->num_dfs_privs;
	dfs_info(dfs, WLAN_DEBUG_DFS_ALWAYS,
		 "dfs->dfs_psoc_idx: %d ", dfs->dfs_psoc_idx);
	dfs_soc_obj->dfs_priv[dfs_soc_obj->num_dfs_privs].dfs = dfs;
	dfs_soc_obj->num_dfs_privs++;

	dfs_info(dfs, WLAN_DEBUG_DFS_ALWAYS, "dfs_soc_obj->num_dfs_privs: %d ",
		 dfs_soc_obj->num_dfs_privs);
}
#endif

bool dfs_is_pcac_required_for_freq(struct precac_tree_node *node, uint16_t freq)
{
	while (node) {
		if (node->ch_freq == freq) {
			if ((node->n_caced_subchs ==
			     N_SUBCHS_FOR_BANDWIDTH(node->bandwidth)) ||
			     (node->n_nol_subchs))
				return false;
			else
				return true;
		}
		node = dfs_descend_precac_tree_for_freq(node, freq);
	}
	return false;
}

uint8_t dfs_get_num_subchans_for_bw(uint8_t depth,
				    uint16_t freq,
				    uint16_t bandwidth)
{
	uint8_t  n_subchans;

	if (freq == CENTER_OF_320_MHZ && bandwidth == DFS_CHWIDTH_320_VAL)
		/* For the 240MHz node the bandwidth is 320MHz but
		 * the number of sub-channels is 12
		 */
		n_subchans = N_SUBCHS_FOR_BANDWIDTH(DFS_CHWIDTH_240_VAL);
	else if (freq == CENTER_OF_PSEUDO_160 &&
		 bandwidth == DFS_CHWIDTH_160_VAL &&
		 depth == TREE_DEPTH_1)
		/* For the right 160MHz child of the  240MHz node the
		 * bandwidth is 160MHz but the number of sub-channels
		 * is 4
		 */
		n_subchans = N_SUBCHS_FOR_BANDWIDTH(DFS_CHWIDTH_80_VAL);
	else
		n_subchans = N_SUBCHS_FOR_BANDWIDTH(bandwidth);

	return n_subchans;
}

#define DFS_160MHZ_SECSEG_CHAN_FREQ_OFFSET 40
#ifdef WLAN_DFS_PRECAC_AUTO_CHAN_SUPPORT
#ifdef CONFIG_CHAN_FREQ_API
qdf_freq_t dfs_configure_deschan_for_precac(struct wlan_dfs *dfs)
{
	struct dfs_channel *deschan = dfs->dfs_autoswitch_chan;
	qdf_freq_t channels[2];
	uint8_t i, nchannels;
	struct dfs_precac_entry *tmp_precac_entry;
	enum precac_chan_state precac_state = PRECAC_ERR;

	if (!deschan)
		return 0;

	if (dfs->dfs_precac_chwidth == CH_WIDTH_160MHZ &&
	    WLAN_IS_CHAN_MODE_160(dfs->dfs_autoswitch_chan)) {
		channels[0] = deschan->dfs_ch_mhz_freq_seg2;
		channels[1] = 0;
		nchannels = 1;
	} else {
		/* The InterCAC feature is enabled only for 80MHz or 160MHz.
		 * Hence split preferred channel into two 80MHz channels.
		 */
		channels[0] = deschan->dfs_ch_mhz_freq_seg1;
		channels[1] = deschan->dfs_ch_mhz_freq_seg2;
		if (WLAN_IS_CHAN_MODE_160(dfs->dfs_autoswitch_chan)) {
			if (deschan->dfs_ch_freq >
			    deschan->dfs_ch_mhz_freq_seg2)
				channels[1] -= DFS_160MHZ_SECSEG_CHAN_OFFSET;
			else
				channels[1] += DFS_160MHZ_SECSEG_CHAN_OFFSET;
		}
		nchannels = 2;
	}

	for (i = 0; i < nchannels; i++) {
		PRECAC_LIST_LOCK(dfs);
		TAILQ_FOREACH(tmp_precac_entry,
			      &dfs->dfs_precac_list,
			      pe_list) {
			if (IS_WITHIN_RANGE(channels[i],
					    tmp_precac_entry->center_ch_freq,
					    tmp_precac_entry->bw)) {
				precac_state = dfs_find_precac_state_of_node(
						channels[i],
						tmp_precac_entry);
				if (precac_state == PRECAC_REQUIRED) {
					PRECAC_LIST_UNLOCK(dfs);
					return channels[i];
				}
			}
		}
		PRECAC_LIST_UNLOCK(dfs);
	}

	return 0;
}
#endif
#endif

#ifdef QCA_SUPPORT_AGILE_DFS
/* FIND_IF_OVERLAP_WITH_WEATHER_RANGE() - Find if the given channel range
 * overlaps with the weather channel range.
 * @first_ch: First subchannel of the channel range.
 * @last_ch:  Last subchannel of the channel range.
 *
 * Algorithm:
 * If the first channel of given range is left of last weather channel
 * and if the last channel of given range is right of the first weather channel,
 * return true, else false.
 */
#define FIND_IF_OVERLAP_WITH_WEATHER_FREQ_RANGE(first_ch_freq, last_ch_freq) \
((first_ch_freq <= WEATHER_CHAN_END_FREQ) && (last_ch_freq >= \
					      WEATHER_CHAN_START_FREQ))

/* dfs_is_pcac_on_weather_channel_for_freq() - Given a channel number, find if
 * it's a weather radar channel.
 * @dfs: Pointer to WLAN_DFS structure.
 * @chwidth: PreCAC channel width enum.
 * @precac_freq: preCAC freq.
 *
 * Based on the precac_width, find the first and last subchannels of the given
 * preCAC channel and check if this range overlaps with weather channel range.
 *
 * Return: True if weather channel, else false.
 */
bool dfs_is_pcac_on_weather_channel_for_freq(struct wlan_dfs *dfs,
						    enum phy_ch_width chwidth,
						    uint16_t precac_freq)
{
	uint16_t first_subch, last_subch;

	switch (chwidth) {
	case CH_WIDTH_20MHZ:
		first_subch = precac_freq;
		last_subch = precac_freq;
		break;
	case CH_WIDTH_40MHZ:
		first_subch = precac_freq - DFS_5GHZ_NEXT_CHAN_FREQ_OFFSET;
		last_subch = precac_freq + DFS_5GHZ_NEXT_CHAN_FREQ_OFFSET;
		break;
	case CH_WIDTH_80MHZ:
		first_subch = precac_freq - DFS_5GHZ_2ND_CHAN_FREQ_OFFSET;
		last_subch = precac_freq + DFS_5GHZ_2ND_CHAN_FREQ_OFFSET;
		break;
	case CH_WIDTH_160MHZ:
		first_subch = precac_freq - DFS_5GHZ_4TH_CHAN_FREQ_OFFSET;
		last_subch = precac_freq + DFS_5GHZ_4TH_CHAN_FREQ_OFFSET;
		break;
	case CH_WIDTH_80P80MHZ:
		/* The restricted 80P80MHz channel or the 165MHz channel
		 * does not include any of the weather radar channels.
		 * Even though other 80P80 channels might include the weather
		 * radar channels, it is not currently possible for Agile
		 * detector to operate in a 80P80MHz channel except in the
		 * restricted 80P80MHz channel.
		 */
		return false;
	default:
		dfs_err(dfs, WLAN_DEBUG_DFS_ALWAYS,
			"Precac channel width invalid!");
		return false;
	}
	return FIND_IF_OVERLAP_WITH_WEATHER_FREQ_RANGE(first_subch, last_subch);
}

/*
 * dfs_start_agile_precac_timer() - Start Agile preCAC timer.
 * @dfs: pointer to wlan_dfs.
 * @ocac_status: OCAC Status.
 * @adfs_param: Pointer to ADFS params.
 */
#define EXTRA_TIME_IN_MS 2000
void dfs_start_agile_precac_timer(struct wlan_dfs *dfs,
				  enum ocac_status_type ocac_status,
				  struct dfs_agile_cac_params *adfs_param)
{
	uint16_t pcacfreq = adfs_param->precac_center_freq_1;
	enum phy_ch_width chwidth = adfs_param->precac_chwidth;
	uint32_t min_precac_timeout, max_precac_timeout;
	struct dfs_soc_priv_obj *dfs_soc_obj;
	uint8_t n_sub_chans;
	uint8_t i;
	qdf_freq_t sub_chans[MAX_20MHZ_SUBCHANS];


	dfs_soc_obj = dfs->dfs_soc_obj;
	dfs_soc_obj->dfs_precac_timer_running = 1;

	/* Find the minimum and maximum precac timeout. */
	max_precac_timeout = MAX_PRECAC_DURATION;
	if (dfs->dfs_precac_timeout_override != -1) {
		min_precac_timeout =
			dfs->dfs_precac_timeout_override * 1000;
	} else if (dfs_is_pcac_on_weather_channel_for_freq(dfs,
							   chwidth,
							   pcacfreq)) {
		min_precac_timeout = MIN_WEATHER_PRECAC_DURATION;
		max_precac_timeout = MAX_WEATHER_PRECAC_DURATION;
	} else {
		min_precac_timeout = MIN_PRECAC_DURATION;
	}

	dfs_info(dfs, WLAN_DEBUG_DFS_ALWAYS,
		 "precactimeout = %d ms", (min_precac_timeout));
	/* Add the preCAC timeout in the params to be sent to FW. */
	adfs_param->min_precac_timeout = min_precac_timeout;
	adfs_param->max_precac_timeout = max_precac_timeout;
	/* For preCAC, in which the FW has to run a timer of a finite amount of
	 * time, set the mode to QUICK_OCAC_MODE.
	 */
	adfs_param->ocac_mode = QUICK_OCAC_MODE;
	/* Increase the preCAC timeout in HOST by 2 seconds to avoid
	 * FW OCAC completion event and HOST timer firing at same time. */
	if (min_precac_timeout)
		min_precac_timeout += EXTRA_TIME_IN_MS;
	n_sub_chans =
		dfs_find_subchannels_for_center_freq(
				adfs_param->precac_center_freq_1,
				adfs_param->precac_center_freq_2,
				adfs_param->precac_chwidth,
				sub_chans);
	for (i = 0; i < n_sub_chans; i++)
		utils_dfs_deliver_event(dfs->dfs_pdev_obj,
					sub_chans[i],
					WLAN_EV_PCAC_STARTED);

	qdf_hrtimer_start(&dfs_soc_obj->dfs_precac_timer,
			  qdf_time_ms_to_ktime(min_precac_timeout),
			  QDF_HRTIMER_MODE_REL);
}
#endif

#ifdef CONFIG_CHAN_FREQ_API
/*
 * dfs_start_precac_timer_for_freq() - Start preCAC timer.
 * @dfs: pointer to wlan_dfs.
 * @precac_chan_freq: PreCAC channel frequency
 */
void dfs_start_precac_timer_for_freq(struct wlan_dfs *dfs,
				     uint16_t precac_chan_freq)
{
	struct dfs_channel *ichan, lc, *curchan;
	uint16_t first_primary_dfs_ch_freq;
	int primary_cac_timeout;
	int secondary_cac_timeout;
	int precac_timeout;
	struct dfs_soc_priv_obj *dfs_soc_obj;
	uint8_t n_sub_chans;
	qdf_freq_t sub_chans[MAX_20MHZ_SUBCHANS];
	uint8_t i;

	dfs_soc_obj = dfs->dfs_soc_obj;
	dfs_soc_obj->cur_agile_dfs_index = dfs->dfs_psoc_idx;
	dfs = dfs_soc_obj->dfs_priv[dfs_soc_obj->cur_agile_dfs_index].dfs;
#define EXTRA_TIME_IN_SEC 5
	dfs_soc_obj->dfs_precac_timer_running = 1;

	/*
	 * Get the first primary ieee chan in the HT80 band and find the channel
	 * pointer.
	 */
	curchan = dfs->dfs_curchan;
	first_primary_dfs_ch_freq = precac_chan_freq - VHT80_FREQ_OFFSET;

	primary_cac_timeout =
	    dfs_mlme_get_cac_timeout_for_freq(dfs->dfs_pdev_obj,
					      curchan->dfs_ch_freq,
					      curchan->dfs_ch_mhz_freq_seg2,
					      curchan->dfs_ch_flags);

	ichan = &lc;
	dfs_mlme_find_dot11_chan_for_freq(dfs->dfs_pdev_obj,
					  first_primary_dfs_ch_freq, 0,
					  WLAN_PHYMODE_11AC_VHT80,
					  &ichan->dfs_ch_freq,
					  &ichan->dfs_ch_flags,
					  &ichan->dfs_ch_flagext,
					  &ichan->dfs_ch_ieee,
					  &ichan->dfs_ch_vhtop_ch_freq_seg1,
					  &ichan->dfs_ch_vhtop_ch_freq_seg2,
					  &ichan->dfs_ch_mhz_freq_seg1,
					  &ichan->dfs_ch_mhz_freq_seg2);

	secondary_cac_timeout = (dfs->dfs_precac_timeout_override != -1) ?
		dfs->dfs_precac_timeout_override :
		dfs_mlme_get_cac_timeout_for_freq(dfs->dfs_pdev_obj,
						  ichan->dfs_ch_freq,
						  ichan->dfs_ch_mhz_freq_seg2,
						  ichan->dfs_ch_flags);

	/*
	 * EXTRA time is needed so that if CAC and PreCAC is running
	 * simultaneously, PreCAC expiry function may be called before CAC
	 * expiry and PreCAC expiry does a channel change (vdev_restart) the
	 * restart response calls CAC_start function(ieee80211_dfs_cac_start)
	 * which cancels any previous CAC timer and starts a new CAC again.
	 * So CAC expiry does not happen and moreover a new CAC is started.
	 * Therefore do not disturb the CAC by channel restart (vdev_restart).
	 *
	 * If CAC/preCAC was already completed on primary, then we do not need
	 * to calculate which CAC timeout is maximum.
	 * For example: If primary's CAC is 600 seconds and secondary's CAC
	 * is 60 seconds then maximum gives 600 seconds which is not needed
	 * if CAC/preCAC was already completed on primary. It is to be noted
	 * that precac/cac is done on primary segment.
	 */
	if (WLAN_IS_CHAN_DFS(dfs->dfs_curchan) &&
	    !dfs_is_precac_done(dfs, dfs->dfs_curchan))
		precac_timeout = QDF_MAX(primary_cac_timeout,
					 secondary_cac_timeout) +
				 EXTRA_TIME_IN_SEC;
	else
		precac_timeout = secondary_cac_timeout + EXTRA_TIME_IN_SEC;

	dfs_debug(dfs, WLAN_DEBUG_DFS,
		"precactimeout = %d", (precac_timeout)*1000);
	n_sub_chans = dfs_find_subchannels_for_center_freq(precac_chan_freq,
							  0,
							  CH_WIDTH_80MHZ,
							  sub_chans);
	for (i = 0; i < n_sub_chans; i++)
		utils_dfs_deliver_event(dfs->dfs_pdev_obj,\
					sub_chans[i],
					WLAN_EV_PCAC_STARTED);

	qdf_hrtimer_start(&dfs_soc_obj->dfs_precac_timer,
			  qdf_time_ms_to_ktime(precac_timeout * 1000),
			  QDF_HRTIMER_MODE_REL);
}
#endif

bool dfs_is_precac_completed_count_non_zero(struct wlan_dfs *dfs)
{
	struct dfs_precac_entry *precac_entry = NULL;

	PRECAC_LIST_LOCK(dfs);
	if (!TAILQ_EMPTY(&dfs->dfs_precac_list)) {
		TAILQ_FOREACH(precac_entry,
			      &dfs->dfs_precac_list,
			      pe_list) {
			/* Find if the tree root has any preCAC channels
			 * that is CAC done.
			 */
			if (!precac_entry->tree_root->n_caced_subchs)
				continue;
			if (abs(precac_entry->tree_root->n_caced_subchs -
			    precac_entry->non_dfs_subch_count)) {
				PRECAC_LIST_UNLOCK(dfs);
				return true;
			}
		}
	}
	PRECAC_LIST_UNLOCK(dfs);

	return false;
}

/*
 * dfs_set_precac_preferred_channel() - Set preCAC preferred channel.
 * @dfs: Pointer to wlan_dfs.
 * @chan: Pointer to dfs_channel.
 * @mode: Wireless mode of channel.
 */
#ifdef WLAN_DFS_PRECAC_AUTO_CHAN_SUPPORT
void dfs_set_precac_preferred_channel(struct wlan_dfs *dfs,
				      struct dfs_channel *chan, uint8_t mode)
{
	bool found = false;
	uint16_t freq_160_sec_mhz = 0;
	struct dfs_precac_entry *precac_entry;

	if (dfs_is_precac_timer_running(dfs) &&
	    WLAN_IS_CHAN_MODE_80(chan) &&
	    (dfs->dfs_precac_secondary_freq_mhz == chan->dfs_ch_freq)) {
		return;
	}

	/* Remove and insert into head, so that the user configured channel
	 * is picked first for preCAC.
	 */
	PRECAC_LIST_LOCK(dfs);
	if (WLAN_IS_CHAN_DFS(chan) &&
	    !TAILQ_EMPTY(&dfs->dfs_precac_list)) {
		TAILQ_FOREACH(precac_entry,
			      &dfs->dfs_precac_list, pe_list) {
			if (precac_entry->vht80_ch_freq ==
			    chan->dfs_ch_mhz_freq_seg1) {
				found = true;
				TAILQ_REMOVE(&dfs->dfs_precac_list,
					     precac_entry, pe_list);
				TAILQ_INSERT_HEAD(&dfs->dfs_precac_list,
						  precac_entry, pe_list);
				break;
			}
		}
	}

	if (WLAN_IS_CHAN_MODE_160(chan) && WLAN_IS_CHAN_DFS(chan) &&
	    !TAILQ_EMPTY(&dfs->dfs_precac_list)) {
		if (chan->dfs_ch_freq < chan->dfs_ch_mhz_freq_seg2)
			freq_160_sec_mhz = chan->dfs_ch_mhz_freq_seg1 +
				VHT160_FREQ_DIFF;
		else
			freq_160_sec_mhz = chan->dfs_ch_mhz_freq_seg1 -
				VHT160_FREQ_DIFF;

		found = false;
		TAILQ_FOREACH(precac_entry,
			      &dfs->dfs_precac_list, pe_list) {
			if (precac_entry->vht80_ch_freq ==
			    freq_160_sec_mhz) {
				found = true;
				TAILQ_REMOVE(&dfs->dfs_precac_list,
					     precac_entry, pe_list);
				TAILQ_INSERT_HEAD(&dfs->dfs_precac_list,
						  precac_entry, pe_list);
				break;
			}
		}
	}

	PRECAC_LIST_UNLOCK(dfs);

	if (!found) {
		dfs_err(dfs, WLAN_DEBUG_DFS_ALWAYS,
			"frequency not found in precac list");
		return;
	}
}

/**
 * dfs_is_autoswitchchan_already_set() - If the given user_des_freq
 * is same as autoswitch chan's freq, return true. Else return false.
 *
 * @dfs: Pointer to struct wlan_dfs
 * @usr_des_freq: User desired frequency in MHZ
 */
static bool
dfs_is_autoswitchchan_already_set(struct wlan_dfs *dfs,
				  qdf_freq_t usr_des_freq)
{

	if (dfs->dfs_autoswitch_chan &&
	    usr_des_freq == dfs->dfs_autoswitch_chan->dfs_ch_freq)
		return true;

	return false;
}

bool
dfs_decide_precac_preferred_chan_for_freq(struct wlan_dfs *dfs,
					  uint16_t *pref_chan_freq,
					  enum wlan_phymode mode)
{
	struct dfs_channel *chan;
	qdf_freq_t rcac_freq;

	chan = qdf_mem_malloc(sizeof(struct dfs_channel));

	if (!chan) {
		dfs_err(dfs, WLAN_DEBUG_DFS_ALWAYS, "malloc failed");
		return false;
	}

	if (QDF_STATUS_SUCCESS !=
	    dfs_mlme_find_dot11_chan_for_freq(dfs->dfs_pdev_obj,
					      *pref_chan_freq, 0,
					      mode,
					      &chan->dfs_ch_freq,
					      &chan->dfs_ch_flags,
					      &chan->dfs_ch_flagext,
					      &chan->dfs_ch_ieee,
					      &chan->dfs_ch_vhtop_ch_freq_seg1,
					      &chan->dfs_ch_vhtop_ch_freq_seg2,
					      &chan->dfs_ch_mhz_freq_seg1,
					      &chan->dfs_ch_mhz_freq_seg2))
		goto exit;
	if (!dfs->dfs_precac_inter_chan_freq)
		goto exit;

	/*
	 * If precac is done on this channel use it, else use a intermediate
	 * non-DFS channel and trigger a precac on this channel.
	 */
	if ((WLAN_IS_CHAN_DFS(chan) ||
	     (WLAN_IS_CHAN_MODE_160(chan) &&
	      WLAN_IS_CHAN_DFS_CFREQ2(chan))) &&
	    !dfs_is_precac_done(dfs, chan)) {

	    qdf_freq_t usr_des_freq = *pref_chan_freq;
	    *pref_chan_freq = dfs->dfs_precac_inter_chan_freq;

	    /*
	     * If preCAC is requested on a 160MHZ channel and the agile detector
	     * can support a max channel width of 80MHZ, we do agile twice, one
	     * on each 80MHZ channel and then mark 160MHZ chan as pre-CAC done.
	     * If interCAC freq is configured, we come up on interCAC chan in
	     * HT80 mode, vap->iv_des_mode is set to HT80 though
	     * dfs_autoswitch_des_mode is HT160. For the second vap that
	     * comes up, since desmode is HT80, it alters
	     * dfs_autoswitch_des_mode also as HT80 and hence we fail
	     * to come up desired 160MHZ channel.
	     * If the desired frequency is already a dfs_autoswitch_chan's freq,
	     * do not update the dfs_autoswitch_des_mode as interCAC chan's
	     * phymode.
	     */

	    if (dfs_is_autoswitchchan_already_set(dfs, usr_des_freq)) {
		dfs_debug(dfs, WLAN_DEBUG_DFS, "Preferred freq %d is"
			  "already the autoswitch freq %d"
			  "autoswitch_mode: %d, not updating"
			  "mode again\n",
			  usr_des_freq, *pref_chan_freq,
			  dfs->dfs_autoswitch_des_mode);
			return true;
		}
		dfs->dfs_autoswitch_des_mode = mode;
		dfs_debug(dfs, WLAN_DEBUG_DFS,
			  "des_chan=%d, des_mode=%d. Current operating channel=%d",
			  chan->dfs_ch_freq,
			  dfs->dfs_autoswitch_des_mode,
			  *pref_chan_freq);

		if (dfs_is_agile_rcac_enabled(dfs)) {
			dfs_get_rcac_freq(dfs, &rcac_freq);
			if (chan->dfs_ch_freq != rcac_freq) {
				/*
				 * Since the upper layer will not do any channel
				 * restart, the agile state machine will not
				 * automatically be restarted. Therefore, stop
				 * and start the the agile state machine on the
				 * desired channel.
				 */
				dfs_agile_sm_deliver_evt(dfs->dfs_soc_obj,
							 DFS_AGILE_SM_EV_AGILE_STOP,
							 0, (void *)dfs);
				dfs_set_rcac_freq(dfs, chan->dfs_ch_freq);
				dfs_agile_sm_deliver_evt(dfs->dfs_soc_obj,
							 DFS_AGILE_SM_EV_AGILE_START,
							 0, (void *)dfs);
			}
		}

		dfs->dfs_autoswitch_chan = chan;
		return true;
	}

	/* Since preCAC is completed on the user configured preferred channel,
	 * make this channel the future intermediate channel.
	 */
	dfs->dfs_precac_inter_chan_freq = chan->dfs_ch_freq;
exit:
	qdf_mem_free(chan);
	return false;
}

#endif

#ifdef QCA_SUPPORT_AGILE_DFS
uint16_t
dfs_translate_chwidth_enum2val(struct wlan_dfs *dfs,
			       enum phy_ch_width chwidth)
{
	switch (chwidth) {
	case CH_WIDTH_20MHZ:
		return DFS_CHWIDTH_20_VAL;
	case CH_WIDTH_40MHZ:
		return DFS_CHWIDTH_40_VAL;
	case CH_WIDTH_80MHZ:
	case CH_WIDTH_80P80MHZ:
		return DFS_CHWIDTH_80_VAL;
	case CH_WIDTH_160MHZ:
		return DFS_CHWIDTH_160_VAL;
	case CH_WIDTH_320MHZ:
		return DFS_CHWIDTH_320_VAL;
	default:
		dfs_err(dfs, WLAN_DEBUG_DFS_ALWAYS, "cannot find mode!");
		return 0;
	}
}

/* dfs_map_to_agile_width() - Given a channel width enum, find the corresponding
 *                            translation for Agile channel width.
 *                            Translation schema of different operating modes:
 *                            20 -> 20, 40 -> 40, (80 & 160 & 80_80) -> 80.
 * @dfs:     Pointer to WLAN DFS structure.
 * @chwidth: Channel width enum.
 *
 * Return: The translated channel width enum.
 */
static enum phy_ch_width
dfs_map_to_agile_width(struct wlan_dfs *dfs, enum phy_ch_width chwidth)
{
	switch (chwidth) {
	case CH_WIDTH_20MHZ:
		return CH_WIDTH_20MHZ;
	case CH_WIDTH_40MHZ:
		return CH_WIDTH_40MHZ;
	case CH_WIDTH_80MHZ:
		return CH_WIDTH_80MHZ;
	case CH_WIDTH_80P80MHZ:
	case CH_WIDTH_160MHZ:
		if (dfs_is_true_160mhz_supported(dfs) ||
		    dfs_is_restricted_80p80mhz_supported(dfs) ||
		    dfs_is_11be_supported(dfs))
			return CH_WIDTH_160MHZ;
		return CH_WIDTH_80MHZ;
	case CH_WIDTH_320MHZ:
		if (dfs_is_adfs_320mhz_supported(dfs) &&
		    dfs_is_11be_supported(dfs))
			return CH_WIDTH_320MHZ;
		return CH_WIDTH_INVALID;
	default:
		dfs_err(dfs, WLAN_DEBUG_DFS_ALWAYS, "Invalid chwidth enum!");
		return CH_WIDTH_INVALID;
	}
}

#ifdef QCA_SUPPORT_ADFS_RCAC

/* dfs_fill_des_rcac_chan_params() - Fill ch_params from dfs current channel.
 *                                   This ch_params is used to determine a
 *                                   Rolling CAC frequency invoking DFS Random
 *                                   selection algorithm.
 * @dfs: Pointer to wlan_dfs structure.
 * @ch_params: Pointer to ch_params structure.
 * @des_chwidth: Desired channel width.
 */
static void dfs_fill_des_rcac_chan_params(struct wlan_dfs *dfs,
					  struct ch_params *ch_params,
					  enum phy_ch_width des_chwidth)
{
	struct dfs_channel *chan = dfs->dfs_curchan;

	ch_params->ch_width = des_chwidth;
	ch_params->center_freq_seg0 = chan->dfs_ch_vhtop_ch_freq_seg1;
	ch_params->center_freq_seg1 = chan->dfs_ch_vhtop_ch_freq_seg2;
	ch_params->mhz_freq_seg0 = chan->dfs_ch_mhz_freq_seg1;
	ch_params->mhz_freq_seg1 = chan->dfs_ch_mhz_freq_seg2;
}

#if defined(QCA_SUPPORT_ADFS_RCAC)
bool dfs_is_rcac_domain(struct wlan_dfs *dfs)
{
	enum dfs_reg dfsdomain = utils_get_dfsdomain(dfs->dfs_pdev_obj);

	if (dfsdomain == DFS_FCC_REGION  ||
	    dfsdomain == DFS_MKK_REGION  ||
	    dfsdomain == DFS_MKKN_REGION ||
	    dfsdomain == DFS_ETSI_REGION)
		return true;

	return false;
}
#endif

bool dfs_is_agile_rcac_enabled(struct wlan_dfs *dfs)
{
	bool rcac_enabled = false;
	bool is_rcac_applicable_on_cur_dfs_domain = dfs_is_rcac_domain(dfs);

	if (is_rcac_applicable_on_cur_dfs_domain &&
	    dfs->dfs_agile_rcac_ucfg && dfs->dfs_fw_adfs_support_non_160)
	    rcac_enabled = true;

	return rcac_enabled;
}

void dfs_agile_cleanup_rcac(struct wlan_dfs *dfs)
{
	dfs->dfs_agile_precac_freq_mhz = 0;
	dfs->dfs_precac_chwidth = CH_WIDTH_INVALID;
	dfs->dfs_soc_obj->ocac_status = OCAC_RESET;
}
#endif

#ifdef QCA_SUPPORT_AGILE_DFS
bool dfs_is_agile_cac_enabled(struct wlan_dfs *dfs)
{
	return (dfs_is_agile_precac_enabled(dfs) ||
		dfs_is_agile_rcac_enabled(dfs));
}
#endif

/* dfs_convert_chwidth_to_wlan_phymode() - Given a channel width, find out the
 *                                         11AXA channel mode.
 * @chwidth: Channel width of type enum phy_ch_width.
 *
 * Return: Converted phymode of type wlan_phymode.
 */
static enum wlan_phymode
dfs_convert_chwidth_to_wlan_phymode(enum phy_ch_width chwidth)
{
	switch (chwidth) {
	case CH_WIDTH_20MHZ:
		return WLAN_PHYMODE_11AXA_HE20;
	case CH_WIDTH_40MHZ:
		return WLAN_PHYMODE_11AXA_HE40;
	case CH_WIDTH_80MHZ:
		return WLAN_PHYMODE_11AXA_HE80;
	case CH_WIDTH_160MHZ:
		return WLAN_PHYMODE_11AXA_HE160;
	case CH_WIDTH_80P80MHZ:
		return WLAN_PHYMODE_11AXA_HE80_80;
	default:
		return WLAN_PHYMODE_MAX;
	}
}

/* dfs_find_dfschan_for_freq() - Given frequency and channel width, find
 *                               compute a dfs channel structure.
 * @dfs: Pointer to struct wlan_dfs.
 * @freq: Frequency in MHZ.
 * @center_freq_seg2: Secondary center frequency in MHZ.
 * @chwidth: Channel width.
 * @chan: Pointer to struct dfs_channel to be filled.
 *
 * Return: QDF_STATUS_SUCCESS if a valid channel pointer exists, else
 *         return status as QDF_STATUS_E_FAILURE.
 */
static QDF_STATUS
dfs_find_dfschan_for_freq(struct wlan_dfs *dfs,
			  qdf_freq_t freq,
			  qdf_freq_t center_freq_seg2,
			  enum phy_ch_width chwidth,
			  struct dfs_channel *chan)
{
	enum wlan_phymode mode;
	QDF_STATUS status = QDF_STATUS_SUCCESS;

	if (!freq) {
		dfs_err(dfs, WLAN_DEBUG_DFS_AGILE, "Input freq is 0!!");
		return QDF_STATUS_E_FAILURE;
	}

	mode = dfs_convert_chwidth_to_wlan_phymode(chwidth);

	if (mode == WLAN_PHYMODE_MAX) {
		dfs_err(dfs, WLAN_DEBUG_DFS_AGILE, "Invalid RCAC mode, user "
				"rcac channel invalid!");
		return QDF_STATUS_E_FAILURE;
	}
	status =
	    dfs_mlme_find_dot11_chan_for_freq(dfs->dfs_pdev_obj,
					      freq, center_freq_seg2, mode,
					      &chan->dfs_ch_freq,
					      &chan->dfs_ch_flags,
					      &chan->dfs_ch_flagext,
					      &chan->dfs_ch_ieee,
					      &chan->dfs_ch_vhtop_ch_freq_seg1,
					      &chan->dfs_ch_vhtop_ch_freq_seg2,
					      &chan->dfs_ch_mhz_freq_seg1,
					      &chan->dfs_ch_mhz_freq_seg2);

	return status;
}

/* dfs_compute_agile_chan_width() - Compute agile detector's channel width
 *                                  and current channel's width.
 *
 * @dfs: Pointer to wlan_dfs structure.
 * @agile_ch_width: Agile channel width to be filled.
 * @cur_ch_width: Current channel width to be filled.
 */
void
dfs_compute_agile_and_curchan_width(struct wlan_dfs *dfs,
				    enum phy_ch_width *agile_ch_width,
				    enum phy_ch_width *cur_ch_width)
{
	/*
	 * Agile detector's band of operation depends on current pdev.
	 * Find the current channel's width and apply the translate rules
	 * to find the Agile detector bandwidth.
	 * Translate rules (all numbers are in MHz) from current pdev's width
	 * to Agile detector's width:
	 * 20 - 20, 40 - 40, 80 - 80, 160 - 80, 160 (non contiguous) - 80.
	 */
	dfs_find_curchwidth_and_center_chan_for_freq(dfs, cur_ch_width,
						     NULL, NULL);

	/* Check if the FW supports agile DFS when the pdev is operating on
	 * 160 or 80P80MHz bandwidth. This information is stored in the flag
	 * "dfs_fw_adfs_support_160" when the current chainmask is configured.
	 */
	if ((*cur_ch_width == CH_WIDTH_80P80MHZ ||
	     *cur_ch_width == CH_WIDTH_160MHZ) &&
	    (!dfs->dfs_fw_adfs_support_160)) {
	    dfs_err(dfs, WLAN_DEBUG_DFS_ALWAYS,
		    "aDFS during 160MHz operation not supported by target");
	    return;
	}
	*agile_ch_width = dfs_map_to_agile_width(dfs, *cur_ch_width);
}

#ifdef QCA_SUPPORT_ADFS_RCAC

/* dfs_is_subchans_of_rcac_chan_in_nol() - Find out the HT20 subchannels of the
 *                                         given dfs_channel and determine if
 *                                         sub channels are in NOL.
 * @dfs: Pointer to struct wlan_dfs.
 * @rcac_chan: Pointer to struct dfs_channel.
 *
 * Return: true if the channel is in NOL else return false.
 */
static bool
dfs_is_subchans_of_rcac_chan_in_nol(struct wlan_dfs *dfs,
				    struct dfs_channel *rcac_chan)
{
	qdf_freq_t rcac_subchans[NUM_CHANNELS_160MHZ];
	uint8_t n_rcac_sub_chans = 0;
	int i;
	bool is_nol = false;

	n_rcac_sub_chans = dfs_find_dfs_sub_channels_for_freq(dfs, rcac_chan,
							      rcac_subchans);

	for (i = 0; i < n_rcac_sub_chans; i++) {
		if (dfs_is_freq_in_nol(dfs, rcac_subchans[i])) {
			is_nol = true;
			break;
		}
	}
	return is_nol;
}

/* dfs_is_rcac_chan_valid() - Find out if the band identified by the given
 *                            primary channel frequency and the width is
 *                            supported by the agile engine.
 * @dfs: Pointer to struct wlan_dfs.
 * @chwidth: Agile channel width
 * @rcac_freq: Rolling CAC frequency.
 *
 * Return: true if the channel is valid else return false.
 */
static bool
dfs_is_rcac_chan_valid(struct wlan_dfs *dfs, enum phy_ch_width chwidth,
		       qdf_freq_t rcac_freq)
{
	struct dfs_channel rcac_chan;
	QDF_STATUS status = QDF_STATUS_SUCCESS;

	if (chwidth == CH_WIDTH_80P80MHZ &&
	    !dfs_is_restricted_80p80mhz_supported(dfs)) {
		dfs_err(dfs, WLAN_DEBUG_DFS_AGILE,
			"RCAC cannot be started for 80P80MHz with single chan");
		return false;
	}

	qdf_mem_zero(&rcac_chan, sizeof(struct dfs_channel));

	/* 1. Find a valid channel pointer with rcac freq and
	 * agile channel width. If a valid channel pointer does not exists,
	 * return failure.
	 */
	status = dfs_find_dfschan_for_freq(dfs, rcac_freq, 0, chwidth,
					   &rcac_chan);
	if (status != QDF_STATUS_SUCCESS) {
		dfs_err(dfs, WLAN_DEBUG_DFS_AGILE,
			"RCAC Channel %d not found for agile width %d",
			dfs->dfs_agile_rcac_freq_ucfg,
			chwidth);
		return false;
	}

	/* 2. Reject the RCAC channel if it is a subset of the current operating
	 * channel or if the RCAC channel is non-DFS.
	 */
	if (dfs_is_new_chan_subset_of_old_chan(dfs, &rcac_chan,
					       dfs->dfs_curchan)) {
		dfs_err(dfs, WLAN_DEBUG_DFS_AGILE,
			"RCAC Channel %d is either a subset of the current"
			"operating channel or is a non-dfs channel",
			dfs->dfs_agile_rcac_freq_ucfg);
		return false;
	}

	/* 3. Reject the RCAC channel if it has NOL channel as its subset. */
	if (dfs_is_subchans_of_rcac_chan_in_nol(dfs, &rcac_chan)) {
	    dfs_err(dfs, WLAN_DEBUG_DFS_AGILE,
		    "RCAC Channel %d has NOL channels as its subset",
		    dfs->dfs_agile_rcac_freq_ucfg);
		return false;
	}

	return true;
}

/* dfs_save_rcac_ch_params() - Save the RCAC channel's params in DFS.
 *                             It is stored in dfs->dfs_rcac_ch_params.
 *                             This ch_params is used in 80211_dfs_action
 *                             as the next channel after radar detect.
 * @dfs: Pointer to struct wlan_dfs.
 * @rcac_ch_params: Rolling CAC channel parameters.
 * @rcac_freq: Rolling CAC frequency.
 */
static void
dfs_save_rcac_ch_params(struct wlan_dfs *dfs, struct ch_params rcac_ch_params,
			qdf_freq_t rcac_freq)
{
	struct dfs_rcac_params *rcac_param = &dfs->dfs_rcac_param;

	rcac_param->rcac_pri_freq = rcac_freq;
	rcac_param->rcac_ch_params.ch_width = rcac_ch_params.ch_width;
	rcac_param->rcac_ch_params.sec_ch_offset = rcac_ch_params.sec_ch_offset;
	rcac_param->rcac_ch_params.center_freq_seg0 =
			rcac_ch_params.center_freq_seg0;
	rcac_param->rcac_ch_params.center_freq_seg1 =
			rcac_ch_params.center_freq_seg1;
	rcac_param->rcac_ch_params.mhz_freq_seg0 =
			rcac_ch_params.mhz_freq_seg0;
	rcac_param->rcac_ch_params.mhz_freq_seg1 =
			rcac_ch_params.mhz_freq_seg1;
	dfs_debug(dfs, WLAN_DEBUG_DFS_AGILE,
		  "Saved rcac params: prim_freq: %d, width: %d, cfreq0: %d"
		  "cfreq1: %d", rcac_param->rcac_pri_freq,
		  rcac_param->rcac_ch_params.ch_width,
		  rcac_param->rcac_ch_params.center_freq_seg0,
		  rcac_param->rcac_ch_params.center_freq_seg1);
}

/**
 * dfs_select_rcac_freq() - Select the RCAC frequency based on BW Expand
 * feature or user configured RCAC frequency.
 * @dfs: Pointer to struct wlan_dfs.
 * @agile_chwidth: Agile channel width.
 * @is_user_rcac_chan_valid: Boolean value to check validity of user configured
 * RCAC freq.
 *
 * Return: Rolling CAC frequency in MHZ.
 */
static
qdf_freq_t dfs_select_rcac_freq(struct wlan_dfs *dfs,
				enum phy_ch_width agile_chwidth,
				bool is_user_rcac_chan_valid)
{
	qdf_freq_t rcac_freq;

	rcac_freq = dfs_bwexpand_find_usr_cnf_chan(dfs);
	if (rcac_freq) {
		if (agile_chwidth  != CH_WIDTH_20MHZ)
			rcac_freq = rcac_freq - HALF_20MHZ_BW;
	} else if (is_user_rcac_chan_valid) {
		rcac_freq = dfs->dfs_agile_rcac_freq_ucfg;
	}

	return rcac_freq;
}

/* dfs_find_rcac_chan() - Find out a rolling CAC channel.
 *
 * @dfs: Pointer to struct wlan_dfs.
 * @curchan_chwidth: Current channel width.
 *
 * Return: Rolling CAC frequency in MHZ.
 */
static qdf_freq_t dfs_find_rcac_chan(struct wlan_dfs *dfs,
				     enum phy_ch_width curchan_chwidth,
				     enum phy_ch_width agile_chwidth)
{
	bool is_user_rcac_chan_valid = false;
	qdf_freq_t rcac_freq, rcac_center_freq = 0;
	struct dfs_channel dfs_chan;
	struct ch_params nxt_chan_params = {0};

	qdf_mem_zero(&dfs_chan, sizeof(struct dfs_channel));

	/* If Rolling CAC is configured, RCAC frequency is the user configured
	 * RCAC frequency or it is found using DFS Random Channel Algorithm.
	 */

	/* Check if user configured RCAC frequency is valid */
	if (dfs->dfs_agile_rcac_freq_ucfg) {
		/*
		 * If the user configured RCAC channel is falling under the
		 * restricted 165MHz channel and current mode is 160MHz,
		 * start RCAC on the 165MHz channel and update chwidth
		 * to 80p80MHz.
		 */
		if (agile_chwidth == CH_WIDTH_160MHZ &&
		    dfs_is_restricted_80p80mhz_supported(dfs) &&
		    IS_WITHIN_RANGE(dfs->dfs_agile_rcac_freq_ucfg,
				    RESTRICTED_80P80_CHAN_CENTER_FREQ,
				    VHT160_FREQ_OFFSET)) {
			dfs->dfs_precac_chwidth = CH_WIDTH_80P80MHZ;
			agile_chwidth = CH_WIDTH_80P80MHZ;
		}

		is_user_rcac_chan_valid =
		    dfs_is_rcac_chan_valid(dfs, agile_chwidth,
					   dfs->dfs_agile_rcac_freq_ucfg);
	}

	/*
	 * In case of BW Expansion, check if user configured frequency is
	 * free from NOL, CAC not completed and not a current channel.
	 *
	 * If the above conditions are satisfied, then set the frequency to
	 * start Rolling CAC on that frequency.
	 *
	 * If not, then set rcac_freq to user configured RCAC channel.
	 */
	rcac_freq = dfs_select_rcac_freq(dfs,
					 agile_chwidth,
					 is_user_rcac_chan_valid);

	if (rcac_freq) {
		if (dfs_find_dfschan_for_freq(dfs, rcac_freq, 0,
					      agile_chwidth,
					      &dfs_chan) != QDF_STATUS_SUCCESS)
			goto exit;

		nxt_chan_params.ch_width = agile_chwidth;
		nxt_chan_params.mhz_freq_seg1 = dfs_chan.dfs_ch_mhz_freq_seg2;
		nxt_chan_params.center_freq_seg1 =
			dfs_chan.dfs_ch_vhtop_ch_freq_seg2;

		/* Get the ch_params from regulatory. ch_width and rcac_freq
		 * are the input given to fetch other params of struct
		 * ch_params.
		 */

		wlan_reg_set_channel_params_for_pwrmode(dfs->dfs_pdev_obj,
							rcac_freq, 0,
							&nxt_chan_params,
							REG_CURRENT_PWR_MODE);
	} else {
		/* Invoke Random channel selection and select only
		 * DFS channels.
		 */
		uint16_t flags = DFS_RANDOM_CH_FLAG_NO_CURR_OPE_CH;

		/* Add Restricted 80p80 enabled bit to the flag so that
		 * random channel selection will fallback to 160MHz and pick
		 * 160MHz channels if the current operating BW is 165MHz.
		 */
		if (WLAN_IS_CHAN_MODE_165(dfs, dfs->dfs_curchan))
			flags |= DFS_RANDOM_CH_FLAG_RESTRICTED_80P80_ENABLED;

		if (!WLAN_IS_CHAN_5GHZ(dfs->dfs_curchan)) {
			dfs_debug(dfs, WLAN_DEBUG_DFS_AGILE,
				  "Current operating channel not a 5G channel");
			goto exit;
		}

		dfs_fill_des_rcac_chan_params(dfs,
					      &nxt_chan_params,
					      agile_chwidth);

		/* The current dfs channel width may not be supported by the
		 * agile engine. For example, some chips may support
		 * 160/80+80Mhz mode for its operating channel (Tx/Rx),
		 * however, the agile engine may support up to a maximum of
		 * 80Mhz bandwidth.
		 * Therefore, we need to compute the agile channel width.
		 * The function dfs_compute_agile_chan_width calculated the
		 * agile channel width elsewhere and the agile channel width is
		 * passed to the utils_dfs_get_random_channel_for_freq through
		 * ch_params->ch_width.
		 */
		utils_dfs_get_random_channel_for_freq(dfs->dfs_pdev_obj, flags,
				&nxt_chan_params, NULL,
				&rcac_freq, NULL);

		if (dfs_find_dfschan_for_freq(dfs, rcac_freq,
					      nxt_chan_params.mhz_freq_seg1,
					      nxt_chan_params.ch_width,
					      &dfs_chan) != QDF_STATUS_SUCCESS)
			goto exit;
	}

	/* Store the random channel ch params for future use on
	 * radar detection.
	 */
	dfs_save_rcac_ch_params(dfs, nxt_chan_params, rcac_freq);

	if (!WLAN_IS_PRIMARY_OR_SECONDARY_CHAN_DFS(&dfs_chan)) {
		dfs_debug(dfs, WLAN_DEBUG_DFS_AGILE,
			  "Not picking an RCAC channel as the random channel"
			  "cfreq1: %d, cfreq2:%d chosen in non-DFS",
			  dfs_chan.dfs_ch_vhtop_ch_freq_seg1,
			  dfs_chan.dfs_ch_vhtop_ch_freq_seg2);
		return 0;
	}

	if (nxt_chan_params.ch_width != dfs->dfs_precac_chwidth) {
		dfs_debug(dfs, WLAN_DEBUG_DFS_AGILE,
			  "Not picking an RCAC channel as next channel"
			  "width: %d is not an agile supported width: %d",
			  nxt_chan_params.ch_width, dfs->dfs_precac_chwidth);
		return 0;
	}
	/* Store the rcac chan params in dfs */
	rcac_center_freq = nxt_chan_params.mhz_freq_seg0;
	switch (nxt_chan_params.ch_width) {
	case CH_WIDTH_160MHZ:
		rcac_center_freq = nxt_chan_params.mhz_freq_seg1;
		break;
	case CH_WIDTH_80P80MHZ:
		if ((rcac_center_freq ==
		     RESTRICTED_80P80_LEFT_80_CENTER_FREQ) ||
		    (rcac_center_freq ==
		     RESTRICTED_80P80_RIGHT_80_CENTER_FREQ))
			rcac_center_freq = RESTRICTED_80P80_CHAN_CENTER_FREQ;
		break;
	default:
		break;
	}

	return rcac_center_freq;
exit:
	qdf_mem_zero(&dfs->dfs_rcac_param, sizeof(struct dfs_rcac_params));
	return 0;
}

#else
static inline qdf_freq_t dfs_find_rcac_chan(struct wlan_dfs *dfs,
					    enum phy_ch_width curchan_chwidth,
					    enum phy_ch_width agile_chwidth)
{
	return 0;
}
#endif

#endif

#ifdef QCA_SUPPORT_AGILE_DFS
/* dfs_find_precac_chan() - Find out a channel to perform preCAC.
 *
 * @dfs: Pointer to struct wlan_dfs.
 * @pri_ch_freq: Primary channel frequency in MHZ.
 * @sec_ch_freq: Secondary channel frequency in MHZ.
 *
 * Return: PreCAC frequency in MHZ.
 */
static qdf_freq_t dfs_find_precac_chan(struct wlan_dfs *dfs,
				       qdf_freq_t pri_ch_freq,
				       qdf_freq_t sec_ch_freq)
{
	/* Convert precac_chwidth to DFS width and find a valid Agile
	 * PreCAC frequency from the preCAC tree.
	 */
	uint16_t chwidth_val;

	/* Find chwidth value for the given enum */
	chwidth_val = dfs_translate_chwidth_enum2val(dfs,
						     dfs->dfs_precac_chwidth);

	dfs->dfs_soc_obj->ocac_status = OCAC_RESET;
	return dfs_get_ieeechan_for_precac_for_freq(dfs,
						    pri_ch_freq,
						    sec_ch_freq,
						    chwidth_val);
}

/*
 * dfs_set_agilecac_chan_for_freq() - Set agile CAC frequency.
 * @dfs: Pointer to wlan_dfs.
 * @ch_freq: Channel frequency in MHZ.
 * @pri_ch_freq: Primary channel frequency.
 * @sec_ch_freq: Secondary channel frequency.
 */
void dfs_set_agilecac_chan_for_freq(struct wlan_dfs *dfs,
				    qdf_freq_t *ch_freq,
				    qdf_freq_t pri_ch_freq,
				    qdf_freq_t sec_ch_freq)
{
	qdf_freq_t ieee_chan_freq;
	enum phy_ch_width agile_chwidth = CH_WIDTH_INVALID;
	enum phy_ch_width curchan_chwidth = CH_WIDTH_INVALID;

	dfs_compute_agile_and_curchan_width(dfs, &agile_chwidth,
					    &curchan_chwidth);
	if (agile_chwidth == CH_WIDTH_INVALID) {
		qdf_info("Cannot start Agile CAC as a valid agile channel width "
			 "could not be found\n");
		return;
	}
	dfs->dfs_precac_chwidth = agile_chwidth;

	if (dfs_is_agile_rcac_enabled(dfs))
		ieee_chan_freq = dfs_find_rcac_chan(dfs, curchan_chwidth,
						    agile_chwidth);
	else
		ieee_chan_freq = dfs_find_precac_chan(dfs, pri_ch_freq,
						      sec_ch_freq);

	dfs->dfs_agile_precac_freq_mhz = ieee_chan_freq;

	/* It was assumed that the bandwidth of the restricted 80p80 channel is
	 * 160MHz to build the precac tree. But when configuring Agile the
	 * channel width should be given as 80p80.
	 */
	if (ieee_chan_freq == RESTRICTED_80P80_CHAN_CENTER_FREQ)
		dfs->dfs_precac_chwidth = CH_WIDTH_80P80MHZ;

	*ch_freq = dfs->dfs_agile_precac_freq_mhz;

	dfs_debug(dfs, WLAN_DEBUG_DFS_AGILE, "Current channel width: %d,"
		  "Agile channel width: %d",
		  curchan_chwidth, agile_chwidth);

	if (!*ch_freq)
		qdf_info("%s: No valid Agile channels available in the current pdev", __func__);
}
#endif

#ifdef QCA_SUPPORT_AGILE_DFS
void dfs_agile_precac_start(struct wlan_dfs *dfs)
{
	struct dfs_agile_cac_params adfs_param;
	uint8_t ocac_status = 0;
	struct dfs_soc_priv_obj *dfs_soc_obj;

	dfs_soc_obj = dfs->dfs_soc_obj;

	qdf_info("%s : %d agile_precac_started: %d",
		 __func__, __LINE__,
		dfs_soc_obj->precac_state_started);

	dfs_soc_obj->dfs_priv[dfs->dfs_psoc_idx].agile_precac_active = true;
	dfs_info(dfs, WLAN_DEBUG_DFS_ALWAYS,
		 "Agile preCAC set to active for dfs_index = %d, dfs: %pK",
		 dfs->dfs_psoc_idx, dfs);

	if (!dfs_soc_obj->precac_state_started) {
		dfs_soc_obj->cur_agile_dfs_index = dfs->dfs_psoc_idx;
		/*
		 * Initiate first call to start preCAC here, for channel as 0,
		 * and ocac_status as 0
		 */
		adfs_param.precac_chan = 0;
		adfs_param.precac_center_freq_1 = 0;
		adfs_param.precac_chwidth = CH_WIDTH_INVALID;
		qdf_info("%s : %d Initiated agile precac",
			 __func__, __LINE__);
		dfs->dfs_soc_obj->precac_state_started = true;
		dfs_start_agile_precac_timer(dfs, ocac_status, &adfs_param);
	}
}
#endif

#ifdef WLAN_DFS_PRECAC_AUTO_CHAN_SUPPORT
/*
 * dfs_set_precac_intermediate_chan() - Set preCAC intermediate channel.
 * @dfs: Pointer to wlan_dfs.
 * @freq: Channel frequency in MHZ.
 */
int32_t dfs_set_precac_intermediate_chan(struct wlan_dfs *dfs, uint32_t freq)
{
	struct dfs_channel chan;

	qdf_mem_zero(&chan, sizeof(struct dfs_channel));
	if (QDF_STATUS_SUCCESS !=
	    dfs_mlme_find_dot11_chan_for_freq(dfs->dfs_pdev_obj,
					      freq, 0,
					      WLAN_PHYMODE_11AC_VHT80,
					      &chan.dfs_ch_freq,
					      &chan.dfs_ch_flags,
					      &chan.dfs_ch_flagext,
					      &chan.dfs_ch_ieee,
					      &chan.dfs_ch_vhtop_ch_freq_seg1,
					      &chan.dfs_ch_vhtop_ch_freq_seg2,
					      &chan.dfs_ch_mhz_freq_seg1,
					      &chan.dfs_ch_mhz_freq_seg2)) {
		dfs_err(dfs, WLAN_DEBUG_DFS_ALWAYS,
			"Intermediate channel not found");
		return -EINVAL;
	}

	/*Intermediate channel should be non-DFS channel*/
	if (!WLAN_IS_CHAN_DFS(&chan)) {
		dfs->dfs_precac_inter_chan_freq = freq;
	} else {
		dfs_err(dfs, WLAN_DEBUG_DFS_ALWAYS,  "intermediate channel %s",
			(chan.dfs_ch_freq == freq) ?
			"should not be DFS channel" : "is invalid");
		dfs->dfs_precac_inter_chan_freq = 0;
		return -EINVAL;
	}

	return 0;
}
#endif
/*
 * dfs_get_precac_intermediate_chan() - Get interCAC channel.
 * @dfs: Pointer to wlan_dfs.
 */
#ifdef WLAN_DFS_PRECAC_AUTO_CHAN_SUPPORT
uint32_t dfs_get_precac_intermediate_chan(struct wlan_dfs *dfs)
{
	return dfs->dfs_precac_inter_chan_freq;
}
#endif

#ifdef WLAN_FEATURE_11BE
static inline void
dfs_set_fw_adfs_support_320(struct wlan_dfs *dfs, bool fw_adfs_support_320)
{
	dfs->dfs_fw_adfs_support_320 = fw_adfs_support_320;
}
#else
static inline void
dfs_set_fw_adfs_support_320(struct wlan_dfs *dfs, bool fw_adfs_support_320)
{
	return;
}
#endif /* WLAN_FEATURE_11BE */

#ifdef QCA_SUPPORT_AGILE_DFS
void dfs_reset_agile_config(struct dfs_soc_priv_obj *dfs_soc)
{
	dfs_soc->cur_agile_dfs_index = PCAC_DFS_INDEX_ZERO;
	dfs_soc->dfs_precac_timer_running = PCAC_TIMER_NOT_RUNNING;
	dfs_soc->precac_state_started = PRECAC_NOT_STARTED;
	dfs_soc->ocac_status = OCAC_RESET;
}

void dfs_set_fw_adfs_support(struct wlan_dfs *dfs,
			     bool fw_adfs_support_160,
			     bool fw_adfs_support_non_160,
			     bool fw_adfs_support_320)
{
	dfs->dfs_fw_adfs_support_non_160 = fw_adfs_support_non_160;
	dfs->dfs_fw_adfs_support_160 = fw_adfs_support_160;
	dfs_set_fw_adfs_support_320(dfs, fw_adfs_support_320);
}
#endif

#if defined(QCA_SUPPORT_AGILE_DFS) || defined(ATH_SUPPORT_ZERO_CAC_DFS) || \
    defined(QCA_SUPPORT_ADFS_RCAC)
QDF_STATUS
dfs_process_radar_ind_on_agile_chan(struct wlan_dfs *dfs,
				    struct radar_found_info *radar_found)
{
	uint32_t freq_center;
	uint32_t radarfound_freq;
	QDF_STATUS status;
	uint8_t num_channels;
	uint16_t freq_list[MAX_20MHZ_SUBCHANS];
	uint16_t nol_freq_list[MAX_20MHZ_SUBCHANS];
	bool is_radar_source_agile =
		(radar_found->detector_id == dfs_get_agile_detector_id(dfs));

	dfs_compute_radar_found_cfreq(dfs, radar_found, &freq_center);
	radarfound_freq = freq_center + radar_found->freq_offset;
	if (is_radar_source_agile)
		dfs_debug(dfs, WLAN_DEBUG_DFS_ALWAYS,
			 "Radar found on Agile detector freq=%d radar freq=%d",
			 freq_center, radarfound_freq);
	else if (radar_found->segment_id == SEG_ID_SECONDARY)
		dfs_debug(dfs, WLAN_DEBUG_DFS_ALWAYS,
			 "Radar found on second segment.Radarfound Freq=%d MHz.Secondary Chan cfreq=%d MHz.",
			 radarfound_freq, freq_center);
	utils_dfs_deliver_event(dfs->dfs_pdev_obj, radarfound_freq,
				WLAN_EV_RADAR_DETECTED);
	if (!dfs->dfs_use_nol) {
		dfs_reset_bangradar(dfs);
		dfs_send_csa_to_current_chan(dfs);
		status = QDF_STATUS_SUCCESS;
		goto exit;
	}

	 num_channels = dfs_find_radar_affected_channels(dfs,
			 radar_found,
			 freq_list,
			 freq_center);

	 dfs_reset_bangradar(dfs);

	 status = dfs_radar_add_channel_list_to_nol_for_freq(dfs,
			 freq_list,
			 nol_freq_list,
			 &num_channels);
	 if (QDF_IS_STATUS_ERROR(status)) {
		dfs_err(dfs, WLAN_DEBUG_DFS,
			"radar event received on invalid channel");
		goto exit;
	 }
	 /*
	  * If precac is running and the radar found in secondary
	  * VHT80 mark the channel as radar and add to NOL list.
	  * Otherwise random channel selection can choose this
	  * channel.
	  */
	 dfs_debug(dfs, WLAN_DEBUG_DFS,
			 "found_on_second=%d is_pre=%d",
			 dfs->is_radar_found_on_secondary_seg,
			 dfs_is_precac_timer_running(dfs));
	 /*
	  * Even if radar found on primary, we need to mark the channel as NOL
	  * in preCAC list. The preCAC list also maintains the current CAC
	  * channels as part of pre-cleared DFS. Hence call the API
	  * to mark channels as NOL irrespective of preCAC being enabled or not.
	  */

	 dfs_debug(dfs, WLAN_DEBUG_DFS,
			 "%s: %d Radar found on dfs detector:%d",
			 __func__, __LINE__, radar_found->detector_id);
	 dfs_mark_precac_nol_for_freq(dfs,
			 dfs->is_radar_found_on_secondary_seg,
			 radar_found->detector_id,
			 nol_freq_list,
			 num_channels);
	 /*
	  * EV 129487 : We have detected radar in the channel,
	  * stop processing PHY error data as this can cause
	  * false detect in the new channel while channel
	  * change is in progress.
	  */

	 if (!dfs->dfs_is_offload_enabled) {
		 dfs_radar_disable(dfs);
		 dfs_second_segment_radar_disable(dfs);
		 dfs_reset_radarq(dfs);
	 }

	if (is_radar_source_agile)
		utils_dfs_agile_sm_deliver_evt(dfs->dfs_pdev_obj,
					       DFS_AGILE_SM_EV_ADFS_RADAR);

exit:
	return status;
}
#endif

#if (defined(QCA_SUPPORT_AGILE_DFS) || defined(QCA_SUPPORT_ADFS_RCAC)) && \
     defined(WLAN_DFS_TRUE_160MHZ_SUPPORT) && defined(WLAN_DFS_FULL_OFFLOAD)
void dfs_translate_radar_params_for_agile_chan(struct wlan_dfs *dfs,
					       struct radar_found_info *r_info)
{
	if (dfs->dfs_precac_chwidth == CH_WIDTH_160MHZ) {
		if (r_info->freq_offset > 0) {
			/*
			 * If the radar hit frequency is right to the center of
			 * 160MHz center frequency, then the segment id should
			 * be secondary segment. The offset frequeny that was
			 * with respect to the 160MHz channel center should be
			 * converted offset frequency based on the right 80MHz
			 * center by subtracting 40MHz on the offset received.
			 */

			r_info->segment_id = SECONDARY_SEG;
			r_info->freq_offset -= DFS_160MHZ_SECOND_SEG_OFFSET;
		} else {
			/*
			 * If the radar hit frequency is left to the center of
			 * 160MHz center frequency, then the segment id should
			 * be primary segment. The offset frequeny that was with
			 * respect to the 160MHz channel center should be
			 * converted into offset frequency based on the left
			 * 80MHz center by adding 40MHz on the offset received.
			 */
			r_info->segment_id = PRIMARY_SEG;
			r_info->freq_offset += DFS_160MHZ_SECOND_SEG_OFFSET;
		}
	} else if (IS_HOST_AGILE_CURCHAN_165MHZ(dfs)) {
		if (r_info->freq_offset > DFS_160MHZ_SECOND_SEG_OFFSET) {
			/*
			 * If the radar hit frequency is on the right 80MHz
			 * segment of the 165MHz channel then the segment id
			 * should be secondary segment id and the offset should
			 * be converted to be based on the right 80MHz center
			 * frequency 5775MHz by subtracting 85MHz.
			 */
			r_info->segment_id = SECONDARY_SEG;
			r_info->freq_offset -= DFS_80P80MHZ_SECOND_SEG_OFFSET;
		}
	}
}

#if defined(QCA_SUPPORT_ADFS_RCAC) && \
    defined(WLAN_DFS_PRECAC_AUTO_CHAN_SUPPORT) && \
    defined(QCA_SUPPORT_AGILE_DFS)
bool dfs_restart_rcac_on_nol_expiry(struct wlan_dfs *dfs)
{
	struct dfs_channel *chan;
	enum phy_ch_width des_ch_width;
	qdf_freq_t rcac_pref_freq;
	enum wlan_phymode des_mode;
	bool is_rcac_started = false;

	/* If rcac is not enabled, exit */
	if (!dfs_is_agile_rcac_enabled(dfs))
		return is_rcac_started;
	/* We have a desired autoswitch channel, but we were not able to come up
	 * on it due to radar. After NOL expiry, we try to come up on that
	 * channel doing RCAC.
	 */

	/**
	 * dfs_autoswitch_chan is set to NULL once we come up on that
	 * channel. However, "dfs_precac_inter_chan_freq" is not set to 0
	 * once interCAC channel change is done. The setting of this channel
	 * is retained so that, next time the same channel can be used to come
	 * up without CAC. Hence validating if dfs_precac_inter_chan_freq is 0
	 * is of no purpose.
	 */
	if (!dfs->dfs_autoswitch_chan)
		return is_rcac_started;

	chan = qdf_mem_malloc(sizeof(struct dfs_channel));

	if (!chan) {
		dfs_err(dfs, WLAN_DEBUG_DFS_ALWAYS, "malloc failed");
		return is_rcac_started;
	}
	rcac_pref_freq = dfs->dfs_autoswitch_chan->dfs_ch_freq;
	des_mode = dfs->dfs_autoswitch_des_mode;

	/* Find a dfs channel pointer with the desired rcac freq and mode. */
	if (QDF_STATUS_SUCCESS !=
	    dfs_mlme_find_dot11_chan_for_freq(dfs->dfs_pdev_obj,
					      rcac_pref_freq, 0,
					      des_mode,
					      &chan->dfs_ch_freq,
					      &chan->dfs_ch_flags,
					      &chan->dfs_ch_flagext,
					      &chan->dfs_ch_ieee,
					      &chan->dfs_ch_vhtop_ch_freq_seg1,
					      &chan->dfs_ch_vhtop_ch_freq_seg2,
					      &chan->dfs_ch_mhz_freq_seg1,
					      &chan->dfs_ch_mhz_freq_seg2))
	    goto exit;

	des_ch_width = utils_dfs_convert_wlan_phymode_to_chwidth(des_mode);

	/* Validate the preferred rcac channel.
	 * The NOL list is per 20Mhz subchannel of a channel.
	 * Therefore, until all the subchannels of the desired
	 * channel are out of NOL, we do not start the RCAC.
	 */
	if (!dfs_is_rcac_chan_valid(dfs, des_ch_width, rcac_pref_freq))
		goto exit;

	/* If preCAC is not completed on the preferred rcac channel,
	 * trigger RCAC on it.
	 */
	if ((WLAN_IS_CHAN_DFS(chan) ||
		 (WLAN_IS_CHAN_MODE_160(chan) &&
		  WLAN_IS_CHAN_DFS_CFREQ2(chan))) &&
		!dfs_is_precac_done(dfs, chan)) {
		/*
		 * Since the upper layer will not do any channel
		 * restart, the agile state machine will not
		 * automatically be restarted. Therefore, stop
		 * and start the the agile state machine on the
		 * desired channel.
		 */
		dfs_agile_sm_deliver_evt(dfs->dfs_soc_obj,
					 DFS_AGILE_SM_EV_AGILE_STOP,
					 0, (void *)dfs);
		dfs_set_rcac_freq(dfs, chan->dfs_ch_freq);
		dfs_agile_sm_deliver_evt(dfs->dfs_soc_obj,
					 DFS_AGILE_SM_EV_AGILE_START,
					 0, (void *)dfs);
		is_rcac_started = true;
	}
exit:
	qdf_mem_free(chan);
	return is_rcac_started;
}
#endif

#ifdef QCA_DFS_BW_EXPAND
/**
 * dfs_find_bonded_chanset_cen() - Find the center frequency for BW Expand.
 * @target_freq_list: Pointer to target_freq_list array.
 * @offset: Starting frequency of the subchannel.
 * @n_agile_subchans: Number of agile subchannels.
 */
static
qdf_freq_t dfs_find_bonded_chanset_cen(qdf_freq_t *target_freq_list,
				       uint8_t offset,
				       uint8_t n_agile_subchans)
{
	uint8_t i;
	qdf_freq_t sum;

	if (!n_agile_subchans)
	    return 0;

	for (sum = 0, i = 0; i < n_agile_subchans; i++)
		sum += target_freq_list[offset + i];

	return sum / n_agile_subchans;
}

/**
 * dfs_bwexpand_is_chanset_agile_eligible() - Checks if the subchannel satifies
 * the condtions for BW Expand.
 * @dfs: Pointer to wlan_dfs.
 * @n_agile_subchans: Number of agile subchannels.
 * @n_cur_channels: Number of current subchannels.
 * @target_freq_list: Pointer to target_freq_list array.
 * @cur_freq_list: Pointer to cur_freq_list array.
 * @offset: Starting frequency of the subchannel.
 */
static
bool dfs_bwexpand_is_chanset_agile_eligible(struct wlan_dfs *dfs,
					    uint8_t n_agile_subchans,
					    uint8_t n_cur_channels,
					    qdf_freq_t *target_freq_list,
					    qdf_freq_t *cur_freq_list,
					    uint8_t offset)
{
	uint8_t temp = 0;
	uint8_t n_cac_done_chan = 0;

	while (temp < n_agile_subchans) {
		qdf_freq_t *p_tgt_freq = &target_freq_list[temp + offset];

		if (dfs_is_freq_in_nol(dfs, *p_tgt_freq) ||
		    dfs_is_subset_channel_for_freq(cur_freq_list,
						   n_cur_channels,
						   p_tgt_freq,
						   1))
			return false;

		if (dfs_is_precac_done_on_non_80p80_chan_for_freq(dfs, *p_tgt_freq))
			n_cac_done_chan++;

		temp++;
	}

	/* If there is at least one channel on which cac is not done,
	 * it is agile CAC eligible
	 */
	if (n_cac_done_chan == n_agile_subchans)
		return false;

	return true;
}

/**
 * Working Sequence of dfs_bwexpand_find_usr_cnf_chan is as follows:
 *
 * Consider User enabled BW Expand through UCI or cfgcommand, then
 * for both Rolling CAC and preCAC, the next channel in which Agile
 * SM needs to run is chosen by dfs_bwexpand_find_usr_cnf_chan API.
 *
 *  1) Find out current Agile Channel width and number of 20BW agile sub-chans.
 *  2) Create a DFS channel with user configured frequency and bandwidth.
 *     Example: User configured Chan 100 HT160 and current agile chanwidth is
 *              40Mhz.
 *  3) Get bonding channel list for both current chan and the target chan.
 *  4) Loop through the target channel list by number of agile bw subchans.
 *     Example: Consider the previous case, 100 HT160 is the target channel
 *              and current agile BW is 40Mhz. The loop should iterate through
 *              102 HT40, 110 HT40, 118 HT40 and 126 HT40 subchans.
 *  5) Check if any of subchans is non-DFS, then Skip to next subchan of agile
 *     BW.
 *  6) All the frequencies in the subchan of agile BW, must satisfy the
 *     following three condtions:
 *               i) The frequency must not be in NOL.
 *               ii) The frequency must not be already CAC completed.
 *               iii) The frequency must not be current channel frequency.
 *  7) If any agile BW subchans satisy the above all conditions, then return
 *     center frequency of the subchan with agile BW.
 */
qdf_freq_t dfs_bwexpand_find_usr_cnf_chan(struct wlan_dfs *dfs)
{
	struct dfs_channel user_chan;
	uint8_t n_target_channels, n_cur_channels, i;
	enum phy_ch_width agile_chwidth = CH_WIDTH_INVALID;
	enum phy_ch_width curchan_chwidth = CH_WIDTH_INVALID;
	qdf_freq_t target_freq_list[MAX_20MHZ_SUBCHANS];
	qdf_freq_t cur_freq_list[MAX_20MHZ_SUBCHANS];
	uint16_t agile_bw;
	uint8_t n_agile_subchans;

	if (!dfs->dfs_use_bw_expand)
		return 0;

	dfs_get_configured_bwexpand_dfs_chan(dfs,
					     &user_chan,
					     dfs->dfs_bw_expand_des_mode);
	n_target_channels =
		dfs_get_bonding_channel_without_seg_info_for_freq(&user_chan,
								  target_freq_list);
	n_cur_channels =
		dfs_get_bonding_channel_without_seg_info_for_freq(dfs->dfs_curchan,
								  cur_freq_list);
	dfs_compute_agile_and_curchan_width(dfs, &agile_chwidth,
					    &curchan_chwidth);
	agile_bw = wlan_reg_get_bw_value(agile_chwidth);
	n_agile_subchans = agile_bw / MIN_DFS_SUBCHAN_BW;

	/*
	 * For every bonded channel set (of the agile bandwidth), starting
	 * from the left, check if the bonded channel set is eligible to become
	 * an agile channel, if not repeat the same for the next bonded
	 * channel set.
	 */
	for (i = 0; i < n_target_channels ; i = i + n_agile_subchans) {
		bool isfound;

		if (!wlan_reg_is_freq_width_dfs(dfs->dfs_pdev_obj,
						target_freq_list[i],
						agile_chwidth))
			continue;

		isfound = dfs_bwexpand_is_chanset_agile_eligible(dfs,
								 n_agile_subchans,
								 n_cur_channels,
								 target_freq_list,
								 cur_freq_list,
								 i);
		if (isfound)
			return dfs_find_bonded_chanset_cen(target_freq_list,
							   i,
							   n_agile_subchans);
	}
	return 0;
}
#endif /* QCA_DFS_BW_EXPAND */
#endif
