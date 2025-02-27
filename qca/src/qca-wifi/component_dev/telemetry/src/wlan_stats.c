/*
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <wlan_objmgr_cmn.h>
#include <wlan_objmgr_psoc_obj.h>
#include <wlan_objmgr_pdev_obj.h>
#include <wlan_objmgr_vdev_obj.h>
#include <wlan_objmgr_peer_obj.h>
#include <cdp_txrx_host_stats.h>
#if CONFIG_SAWF
#include <cdp_txrx_extd_struct.h>
#include <cdp_txrx_sawf.h>
#endif
#include <wlan_cfg80211_ic_cp_stats.h>
#include <qdf_event.h>
#include <ieee80211_var.h>
#include <ieee80211_cfg80211.h>
#include <wlan_stats.h>
#ifdef CONFIG_MLO_SINGLE_DEV
#include "cdp_txrx_mlo.h"
#include "wlan_mlo_mgr_ap.h"
#include <wlan_mlo_mgr_peer.h>
#endif
#include <wlan_objmgr_global_obj_i.h>

/* Maximum number of asynchronous requests that can be scheduled by driver */
#define WLAN_STATS_ASYNC_LIST_SIZE 8

static uint8_t get_monitor_version(struct wlan_objmgr_pdev *pdev)
{
	struct ieee80211com *ic = NULL;

	ic = wlan_pdev_get_mlme_ext_obj(pdev);
	return ic->ic_monitor_version;
}

/* Global structure for stats work*/
static struct stats_work_context g_stats_ctx = {0};

static void free_list_entry(struct stats_list_entry *list_entry)
{
	qdf_mem_free(list_entry->cfg);
	qdf_mem_free(list_entry);
}

static void stats_evt_work_handler(void *ctx)
{
	struct stats_list_entry *list_entry = NULL;
	struct stats_work_context *work_ctx = (struct stats_work_context *)ctx;
	qdf_list_node_t *cur_node = NULL;
	qdf_list_t temp_list;

	qdf_list_create(&temp_list, WLAN_STATS_ASYNC_LIST_SIZE);
	qdf_spin_lock_bh(&work_ctx->list_lock);
	qdf_list_join(&temp_list, &work_ctx->nb_stats_work_list);
	qdf_spin_unlock_bh(&work_ctx->list_lock);

	while (!qdf_list_remove_front(&temp_list, &cur_node)) {
		if (!cur_node)
			continue;
		list_entry = qdf_container_of(cur_node, struct stats_list_entry,
					      node);
		if (!list_entry)
			continue;

		telemetric_reply_setup(list_entry->pdev, list_entry->vdev,
				       list_entry->cfg, list_entry->mac);

		free_list_entry(list_entry);
		cur_node = NULL;
		list_entry = NULL;
	}
	qdf_list_destroy(&temp_list);
}

void wlan_stats_nb_stats_work_attach(void)
{
	if (g_stats_ctx.is_initialized)
		return;

	g_stats_ctx.is_initialized = true;
	qdf_create_work(0, &g_stats_ctx.work, stats_evt_work_handler,
			&g_stats_ctx);
	qdf_spinlock_create(&g_stats_ctx.list_lock);
	qdf_list_create(&g_stats_ctx.nb_stats_work_list,
			WLAN_STATS_ASYNC_LIST_SIZE);
}

void wlan_stats_nb_stats_work_detach(void)
{
	qdf_list_t *list = &g_stats_ctx.nb_stats_work_list;
	qdf_list_node_t *cur_node = NULL;
	struct stats_list_entry *list_entry = NULL;

	if (!g_stats_ctx.is_initialized)
		return;

	g_stats_ctx.is_initialized = false;
	qdf_flush_work(&g_stats_ctx.work);
	qdf_disable_work(&g_stats_ctx.work);

	qdf_spin_lock_bh(&g_stats_ctx.list_lock);

	//Flush the queue until list is empty. Remove the nodes from the end
	while (!qdf_list_remove_front(list, &cur_node)) {
		if (cur_node) {
			list_entry = qdf_container_of(cur_node,
						      struct stats_list_entry,
						      node);
			if (!list_entry)
				continue;
			free_list_entry(list_entry);
			cur_node = NULL;
			list_entry = NULL;
		}
	}

	qdf_spin_unlock_bh(&g_stats_ctx.list_lock);

	/* destroy list */
	qdf_list_destroy(list);
	qdf_spinlock_destroy(&g_stats_ctx.list_lock);
}

QDF_STATUS wlan_stats_schedule_nb_stats_work(struct wlan_objmgr_pdev *pdev,
					     struct wlan_objmgr_vdev *vdev,
					     struct stats_config *cfg,
					     uint8_t *mac)
{
	struct stats_list_entry *entry = NULL;
	struct stats_config *stats_cfg = NULL;

	if (!g_stats_ctx.is_initialized) {
		qdf_err("Stats context is already detached");
		return QDF_STATUS_E_FAILURE;
	}

	if (qdf_list_size(&g_stats_ctx.nb_stats_work_list) >=
			  WLAN_STATS_ASYNC_LIST_SIZE) {
		qdf_err("Not Scheduling stats work as MAX_SIZE reached");
		return QDF_STATUS_E_FAILURE;
	}

	stats_cfg = qdf_mem_malloc(sizeof(struct stats_config));
	entry = qdf_mem_malloc(sizeof(struct stats_list_entry));

	if (!stats_cfg || !entry) {
		qdf_err("Memory not allocated.");
		return QDF_STATUS_E_FAILURE;
	}

	qdf_mem_zero(entry, sizeof(struct stats_list_entry));
	qdf_mem_copy(stats_cfg, cfg, sizeof(struct stats_config));

	entry->pdev = pdev;
	entry->vdev = vdev;
	qdf_mem_copy(entry->mac, mac, QDF_MAC_ADDR_SIZE);
	entry->cfg = stats_cfg;

	qdf_spin_lock_bh(&g_stats_ctx.list_lock);
	qdf_list_insert_back(&g_stats_ctx.nb_stats_work_list, &entry->node);
	qdf_spin_unlock_bh(&g_stats_ctx.list_lock);
	qdf_sched_work(0, &g_stats_ctx.work);

	return QDF_STATUS_SUCCESS;
}

static void fill_basic_data_tx_stats(struct basic_data_tx_stats *tx,
				     struct cdp_tx_stats *cdp_tx)
{
	tx->tx_success.num = cdp_tx->tx_success.num;
	tx->tx_success.bytes = cdp_tx->tx_success.bytes;
	tx->comp_pkt.num = cdp_tx->comp_pkt.num;
	tx->comp_pkt.bytes = cdp_tx->comp_pkt.bytes;
	tx->tx_failed = cdp_tx->tx_failed;
	tx->dropped_count = cdp_tx->dropped.fw_rem.num +
			    cdp_tx->dropped.fw_rem_notx +
			    cdp_tx->dropped.fw_rem_tx.num +
			    cdp_tx->dropped.age_out +
			    cdp_tx->dropped.fw_reason1 +
			    cdp_tx->dropped.fw_reason2 +
			    cdp_tx->dropped.fw_reason3 +
			    cdp_tx->dropped.drop_threshold +
			    cdp_tx->dropped.drop_link_desc_na +
			    cdp_tx->dropped.invalid_drop +
			    cdp_tx->dropped.mcast_vdev_drop +
			    cdp_tx->dropped.invalid_rr;
}

static void update_basic_peer_data_rx_stats(struct basic_data_rx_stats *rx,
					    struct cdp_rx_stats *cdp_rx)
{
	rx->to_stack.num = cdp_rx->rx_success.num;
	rx->to_stack.bytes = cdp_rx->rx_success.bytes;
}

static void fill_basic_data_rx_stats(struct basic_data_rx_stats *rx,
				     struct cdp_rx_stats *cdp_rx)
{
	rx->to_stack.num = cdp_rx->to_stack.num;
	rx->to_stack.bytes = cdp_rx->to_stack.bytes;
	rx->total_rcvd.num = cdp_rx->unicast.num + cdp_rx->multicast.num;
	rx->total_rcvd.bytes = cdp_rx->unicast.bytes + cdp_rx->multicast.bytes;
	rx->rx_error_count = cdp_rx->err.mic_err + cdp_rx->err.decrypt_err +
			     cdp_rx->err.fcserr + cdp_rx->err.pn_err +
			     cdp_rx->err.oor_err;
}

static void fill_basic_peer_data_tx(struct basic_peer_data_tx *data,
				    struct cdp_tx_stats *tx)
{
	fill_basic_data_tx_stats(&data->tx, tx);
}

static void fill_basic_peer_ctrl_tx(struct basic_peer_ctrl_tx *ctrl,
				    struct peer_ic_cp_stats *peer_cp_stats)
{
	ctrl->cs_tx_mgmt = peer_cp_stats->cs_tx_mgmt;
	ctrl->cs_is_tx_not_ok = peer_cp_stats->cs_is_tx_not_ok;
}

static void fill_basic_peer_data_rx(struct basic_peer_data_rx *data,
				    struct cdp_rx_stats *rx)
{
	fill_basic_data_rx_stats(&data->rx, rx);

	/* update to_stack with rx_success for link level support */
	update_basic_peer_data_rx_stats(&data->rx, rx);
}

static void fill_basic_peer_ctrl_rx(struct basic_peer_ctrl_rx *ctrl,
				    struct peer_ic_cp_stats *peer_cp_stats)
{
	ctrl->cs_rx_mgmt = peer_cp_stats->cs_rx_mgmt;
	ctrl->cs_rx_decryptcrc = peer_cp_stats->cs_rx_decryptcrc;
	ctrl->cs_rx_security_failure = peer_cp_stats->cs_rx_wepfail +
				       peer_cp_stats->cs_rx_ccmpmic +
				       peer_cp_stats->cs_rx_wpimic;
}

static void fill_basic_peer_data_link(struct basic_peer_data_link *data,
				      struct cdp_rx_stats *rx)
{
	data->avg_snr = rx->avg_snr;
	data->snr = rx->snr;
	data->last_snr = rx->last_snr;
}

static void fill_basic_peer_ctrl_link(struct basic_peer_ctrl_link *ctrl,
				      struct peer_ic_cp_stats *cp_stats)
{
	ctrl->cs_rx_mgmt_snr = cp_stats->cs_rx_mgmt_snr;
}

static void fill_basic_peer_data_rate(struct basic_peer_data_rate *data,
				      struct cdp_peer_stats *peer_stats)
{
	struct cdp_tx_stats *tx = &peer_stats->tx;
	struct cdp_rx_stats *rx = &peer_stats->rx;

	/* rx_rate from cdp_rx_stats gives MCS number. Use last_rx_rate */
	data->rx_rate = rx->last_rx_rate;
	data->last_rx_rate = rx->last_rx_rate;
	data->tx_rate = tx->tx_rate;
	data->last_tx_rate = tx->last_tx_rate;
}

static void fill_basic_peer_ctrl_rate(struct basic_peer_ctrl_rate *ctrl,
				      struct peer_ic_cp_stats *cp_stats)
{
	ctrl->cs_rx_mgmt_rate = cp_stats->cs_rx_mgmt_rate;
}

static void fill_basic_vdev_data_tx(struct basic_vdev_data_tx *data,
				    struct cdp_vdev_stats *vdev_stats)
{
	struct cdp_tx_ingress_stats *tx_i = &vdev_stats->tx_i;

	fill_basic_data_tx_stats(&data->tx, &vdev_stats->tx);
	data->ingress.num = tx_i->rcvd.num;
	data->ingress.bytes = tx_i->rcvd.bytes;
	data->processed.num = tx_i->processed.num;
	data->processed.bytes = tx_i->processed.bytes;
	data->dropped.num = tx_i->dropped.dropped_pkt.num;
	data->dropped.bytes = tx_i->dropped.dropped_pkt.bytes;
}

static void fill_basic_vdev_ctrl_tx(struct basic_vdev_ctrl_tx *ctrl,
				    struct vdev_ic_cp_stats *cp_stats)
{
	ctrl->cs_tx_mgmt = cp_stats->ucast_stats.cs_tx_mgmt;
	ctrl->cs_tx_error_counter = cp_stats->stats.cs_tx_nodefkey +
				    cp_stats->stats.cs_tx_noheadroom +
				    cp_stats->stats.cs_tx_nobuf +
				    cp_stats->stats.cs_tx_nonode +
				    cp_stats->stats.cs_tx_cipher_err +
				    cp_stats->stats.cs_tx_not_ok;
	ctrl->cs_tx_discard = cp_stats->ucast_stats.cs_tx_discard +
			      cp_stats->mcast_stats.cs_tx_discard;
}

static void fill_basic_vdev_data_rx(struct basic_vdev_data_rx *data,
				    struct cdp_rx_stats *rx)
{
	fill_basic_data_rx_stats(&data->rx, rx);
}

static void fill_basic_vdev_ctrl_rx(struct basic_vdev_ctrl_rx *ctrl,
				    struct vdev_ic_cp_stats *cp_stats)
{
	ctrl->cs_rx_mgmt = cp_stats->ucast_stats.cs_rx_mgmt;
	ctrl->cs_rx_error_counter = cp_stats->stats.cs_rx_wrongbss +
				    cp_stats->stats.cs_rx_tooshort +
				    cp_stats->stats.cs_rx_ssid_mismatch;
	ctrl->cs_rx_mgmt_discard = cp_stats->stats.cs_rx_mgmt_discard;
	ctrl->cs_rx_ctl = cp_stats->stats.cs_rx_ctl;
	ctrl->cs_rx_discard = cp_stats->ucast_stats.cs_rx_discard +
			      cp_stats->mcast_stats.cs_rx_discard;
	ctrl->cs_rx_security_failure = cp_stats->ucast_stats.cs_rx_wepfail +
				       cp_stats->mcast_stats.cs_rx_wepfail +
				       cp_stats->ucast_stats.cs_rx_tkipicv +
				       cp_stats->mcast_stats.cs_rx_tkipicv +
				       cp_stats->ucast_stats.cs_rx_ccmpmic +
				       cp_stats->mcast_stats.cs_rx_ccmpmic +
				       cp_stats->ucast_stats.cs_rx_wpimic +
				       cp_stats->mcast_stats.cs_rx_wpimic;
}

static void fill_basic_vdev_ctrl_link(struct basic_vdev_ctrl_link *ctrl,
				      struct wlan_objmgr_vdev *vdev)
{
	/* Deduct 1 for BSS peer */
	ctrl->cs_peer_count = wlan_vdev_get_peer_count(vdev) - 1;
}

static void fill_basic_pdev_data_tx(struct basic_pdev_data_tx *data,
				    struct cdp_pdev_stats *pdev_stats)
{
	struct cdp_tx_ingress_stats *tx_i = &pdev_stats->tx_i;

	fill_basic_data_tx_stats(&data->tx, &pdev_stats->tx);
	data->ingress.num = tx_i->rcvd.num;
	data->ingress.bytes = tx_i->rcvd.bytes;
	data->processed.num = tx_i->processed.num;
	data->processed.bytes = tx_i->processed.bytes;
	data->dropped.num = tx_i->dropped.dropped_pkt.num;
	data->dropped.bytes = tx_i->dropped.dropped_pkt.bytes;
}

static void fill_basic_pdev_ctrl_tx(struct basic_pdev_ctrl_tx *ctrl,
				    struct pdev_ic_cp_stats *pdev_cp_stats)
{
	ctrl->cs_tx_mgmt = pdev_cp_stats->stats.cs_tx_mgmt;
	ctrl->cs_tx_frame_count = pdev_cp_stats->stats.cs_tx_frame_count;
}

static void fill_basic_pdev_data_rx(struct basic_pdev_data_rx *data,
				    struct cdp_pdev_stats *pdev_stats)
{
	fill_basic_data_rx_stats(&data->rx, &pdev_stats->rx);
	data->dropped_count = pdev_stats->dropped.msdu_not_done +
			      pdev_stats->dropped.mec +
			      pdev_stats->dropped.mesh_filter +
			      pdev_stats->dropped.wifi_parse +
			      pdev_stats->dropped.mon_rx_drop +
			      pdev_stats->dropped.mon_radiotap_update_err;
	data->err_count = pdev_stats->err.desc_alloc_fail +
			  pdev_stats->err.ip_csum_err +
			  pdev_stats->err.tcp_udp_csum_err +
			  pdev_stats->err.rxdma_error +
			  pdev_stats->err.reo_error +
			  pdev_stats->err.desc_lt_alloc_fail +
			  pdev_stats->err.fw_reported_rxdma_error;
}

static void fill_basic_pdev_ctrl_rx(struct basic_pdev_ctrl_rx *ctrl,
				    struct pdev_ic_cp_stats *pdev_cp_stats)
{
	ctrl->cs_rx_mgmt = pdev_cp_stats->stats.cs_rx_mgmt;
	ctrl->cs_rx_num_mgmt = pdev_cp_stats->stats.cs_rx_num_mgmt;
	ctrl->cs_rx_num_ctl = pdev_cp_stats->stats.cs_rx_num_ctl;
	ctrl->cs_rx_frame_count = pdev_cp_stats->stats.cs_rx_frame_count;
	ctrl->cs_rx_error_sum = pdev_cp_stats->stats.cs_fcsbad +
				pdev_cp_stats->stats.cs_be_nobuf +
				pdev_cp_stats->stats.cs_rx_overrun +
				pdev_cp_stats->stats.cs_rx_phy_err +
				pdev_cp_stats->stats.cs_rx_ack_err +
				pdev_cp_stats->stats.cs_rx_rts_err +
				pdev_cp_stats->stats.cs_no_beacons +
				pdev_cp_stats->stats.cs_rx_mgmt_rssi_drop +
				pdev_cp_stats->stats.cs_phy_err_count;
}

static void fill_basic_pdev_ctrl_link(struct basic_pdev_ctrl_link *ctrl,
				      struct pdev_ic_cp_stats *cp_stats,
				      struct wlan_objmgr_pdev *pdev)
{
	ctrl->cs_chan_tx_pwr = cp_stats->stats.cs_chan_tx_pwr;
	ctrl->cs_chan_nf = cp_stats->stats.cs_chan_nf;
	ctrl->cs_chan_nf_sec80 = cp_stats->stats.cs_chan_nf_sec80;
	ctrl->cs_peer_count = wlan_pdev_get_peer_count(pdev) -
				wlan_pdev_get_vdev_count(pdev);
}

static void fill_basic_psoc_data_tx(struct basic_psoc_data_tx *data,
				    struct cdp_soc_stats *soc_stats)
{
	data->egress.num = soc_stats->tx.egress.num;
	data->egress.bytes = soc_stats->tx.egress.bytes;
}

static void fill_basic_psoc_data_rx(struct basic_psoc_data_rx *data,
				    struct cdp_soc_stats *soc_stats)
{
	data->ingress.num = soc_stats->rx.ingress.num;
	data->ingress.bytes = soc_stats->rx.ingress.bytes;
}

static QDF_STATUS get_basic_peer_data_tx(struct unified_stats *stats,
					 struct cdp_peer_stats *peer_stats)
{
	struct basic_peer_data_tx *data = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct basic_peer_data_tx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_data_tx(data, &peer_stats->tx);

	stats->feat[INX_FEAT_TX] = data;
	stats->size[INX_FEAT_TX] = sizeof(struct basic_peer_data_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_peer_ctrl_tx(struct unified_stats *stats,
					 struct peer_ic_cp_stats *peer_cp_stats)
{
	struct basic_peer_ctrl_tx *ctrl = NULL;

	if (!stats || !peer_cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct basic_peer_ctrl_tx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_ctrl_tx(ctrl, peer_cp_stats);

	stats->feat[INX_FEAT_TX] = ctrl;
	stats->size[INX_FEAT_TX] = sizeof(struct basic_peer_ctrl_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_peer_data_rx(struct unified_stats *stats,
					 struct cdp_peer_stats *peer_stats)
{
	struct basic_peer_data_rx *data = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct basic_peer_data_rx));
	if (!data) {
		qdf_err("Failed Allocation");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_data_rx(data, &peer_stats->rx);

	stats->feat[INX_FEAT_RX] = data;
	stats->size[INX_FEAT_RX] = sizeof(struct basic_peer_data_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_peer_ctrl_rx(struct unified_stats *stats,
					 struct peer_ic_cp_stats *peer_cp_stats)
{
	struct basic_peer_ctrl_rx *ctrl = NULL;

	if (!stats || !peer_cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct basic_peer_ctrl_rx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_ctrl_rx(ctrl, peer_cp_stats);

	stats->feat[INX_FEAT_RX] = ctrl;
	stats->size[INX_FEAT_RX] = sizeof(struct basic_peer_ctrl_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_peer_data_link(struct unified_stats *stats,
					   struct cdp_peer_stats *peer_stats)
{
	struct basic_peer_data_link *data = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct basic_peer_data_link));
	if (!data) {
		qdf_err("Failed Allocation");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_data_link(data, &peer_stats->rx);

	stats->feat[INX_FEAT_LINK] = data;
	stats->size[INX_FEAT_LINK] = sizeof(struct basic_peer_data_link);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_peer_ctrl_link(struct unified_stats *stats,
					   struct peer_ic_cp_stats *cp_stats)
{
	struct basic_peer_ctrl_link *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct basic_peer_ctrl_link));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_ctrl_link(ctrl, cp_stats);

	stats->feat[INX_FEAT_LINK] = ctrl;
	stats->size[INX_FEAT_LINK] = sizeof(struct basic_peer_ctrl_link);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_peer_data_rate(struct unified_stats *stats,
					   struct cdp_peer_stats *peer_stats)
{
	struct basic_peer_data_rate *data = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct basic_peer_data_rate));
	if (!data) {
		qdf_err("Failed Allocation");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_data_rate(data, peer_stats);

	stats->feat[INX_FEAT_RATE] = data;
	stats->size[INX_FEAT_RATE] = sizeof(struct basic_peer_data_rate);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_peer_ctrl_rate(struct unified_stats *stats,
					   struct peer_ic_cp_stats *cp_stats)
{
	struct basic_peer_ctrl_rate *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct basic_peer_ctrl_rate));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_ctrl_rate(ctrl, cp_stats);

	stats->feat[INX_FEAT_RATE] = ctrl;
	stats->size[INX_FEAT_RATE] = sizeof(struct basic_peer_ctrl_rate);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_vdev_data_tx(struct unified_stats *stats,
					 struct cdp_vdev_stats *vdev_stats)
{
	struct basic_vdev_data_tx *data = NULL;
	struct cdp_tx_stats *tx = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &vdev_stats->tx_i;
	tx = &vdev_stats->tx;
	data = qdf_mem_malloc(sizeof(struct basic_vdev_data_tx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_data_tx(data, vdev_stats);

	stats->feat[INX_FEAT_TX] = data;
	stats->size[INX_FEAT_TX] = sizeof(struct basic_vdev_data_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_vdev_ctrl_tx(struct unified_stats *stats,
					 struct vdev_ic_cp_stats *cp_stats)
{
	struct basic_vdev_ctrl_tx *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct basic_vdev_ctrl_tx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_ctrl_tx(ctrl, cp_stats);

	stats->feat[INX_FEAT_TX] = ctrl;
	stats->size[INX_FEAT_TX] = sizeof(struct basic_vdev_ctrl_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_vdev_data_rx(struct unified_stats *stats,
					 struct cdp_vdev_stats *vdev_stats)
{
	struct basic_vdev_data_rx *data = NULL;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct basic_vdev_data_rx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_data_rx(data, &vdev_stats->rx);

	stats->feat[INX_FEAT_RX] = data;
	stats->size[INX_FEAT_RX] = sizeof(struct basic_vdev_data_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_vdev_ctrl_rx(struct unified_stats *stats,
					 struct vdev_ic_cp_stats *cp_stats)
{
	struct basic_vdev_ctrl_rx *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct basic_vdev_ctrl_rx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_ctrl_rx(ctrl, cp_stats);

	stats->feat[INX_FEAT_RX] = ctrl;
	stats->size[INX_FEAT_RX] = sizeof(struct basic_vdev_ctrl_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_vdev_ctrl_link(struct unified_stats *stats,
					   struct wlan_objmgr_vdev *vdev)
{
	struct basic_vdev_ctrl_link *ctrl;

	if (!stats || !vdev) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct basic_vdev_ctrl_link));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_ctrl_link(ctrl, vdev);

	stats->feat[INX_FEAT_LINK] = ctrl;
	stats->size[INX_FEAT_LINK] = sizeof(struct basic_vdev_ctrl_link);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_pdev_data_tx(struct unified_stats *stats,
					 struct cdp_pdev_stats *pdev_stats)
{
	struct basic_pdev_data_tx *data = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct basic_pdev_data_tx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_data_tx(data, pdev_stats);

	stats->feat[INX_FEAT_TX] = data;
	stats->size[INX_FEAT_TX] = sizeof(struct basic_pdev_data_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_pdev_data_rx(struct unified_stats *stats,
					 struct cdp_pdev_stats *pdev_stats)
{
	struct basic_pdev_data_rx *data = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct basic_pdev_data_rx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_data_rx(data, pdev_stats);

	stats->feat[INX_FEAT_RX] = data;
	stats->size[INX_FEAT_RX] = sizeof(struct basic_pdev_data_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_pdev_ctrl_tx(struct unified_stats *stats,
					 struct pdev_ic_cp_stats *pdev_cp_stats)
{
	struct basic_pdev_ctrl_tx *ctrl = NULL;

	if (!stats || !pdev_cp_stats) {
		qdf_err("Invalid Input");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct basic_pdev_ctrl_tx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_ctrl_tx(ctrl, pdev_cp_stats);

	stats->feat[INX_FEAT_TX] = ctrl;
	stats->size[INX_FEAT_TX] = sizeof(struct basic_pdev_ctrl_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_pdev_ctrl_rx(struct unified_stats *stats,
					 struct pdev_ic_cp_stats *pdev_cp_stats)
{
	struct basic_pdev_ctrl_rx *ctrl = NULL;

	if (!stats || !pdev_cp_stats) {
		qdf_err("Invalid Input");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct basic_pdev_ctrl_rx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_ctrl_rx(ctrl, pdev_cp_stats);

	stats->feat[INX_FEAT_RX] = ctrl;
	stats->size[INX_FEAT_RX] = sizeof(struct basic_pdev_ctrl_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_pdev_ctrl_link(struct unified_stats *stats,
					   struct pdev_ic_cp_stats *cp_stats,
					   struct wlan_objmgr_pdev *pdev)
{
	struct basic_pdev_ctrl_link *ctrl = NULL;

	if (!stats || !cp_stats || !pdev) {
		qdf_err("Invalid Input");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct basic_pdev_ctrl_link));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_ctrl_link(ctrl, cp_stats, pdev);

	stats->feat[INX_FEAT_LINK] = ctrl;
	stats->size[INX_FEAT_LINK] = sizeof(struct basic_pdev_ctrl_link);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_psoc_data_tx(struct unified_stats *stats,
					 struct cdp_soc_stats *soc_stats)
{
	struct basic_psoc_data_tx *data = NULL;

	if (!stats || !soc_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct basic_psoc_data_tx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_psoc_data_tx(data, soc_stats);

	stats->feat[INX_FEAT_TX] = data;
	stats->size[INX_FEAT_TX] = sizeof(struct basic_psoc_data_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_psoc_data_rx(struct unified_stats *stats,
					 struct cdp_soc_stats *soc_stats)
{
	struct basic_psoc_data_rx *data = NULL;

	if (!stats || !soc_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct basic_psoc_data_rx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_psoc_data_rx(data, soc_stats);

	stats->feat[INX_FEAT_RX] = data;
	stats->size[INX_FEAT_RX] = sizeof(struct basic_psoc_data_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_basic_peer_data(struct wlan_objmgr_psoc *psoc,
				      struct wlan_objmgr_vdev *vdev,
				      uint8_t *peer_mac,
				      struct unified_stats *stats,
				      uint32_t feat,
				      enum stats_peer_type peer_type)
{
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	struct cdp_peer_stats *peer_stats = NULL;
	uint8_t vdev_id = 0;
	bool stats_collected = false;

	peer_stats = qdf_mem_malloc(sizeof(struct cdp_peer_stats));
	if (!peer_stats) {
		qdf_err("Failed allocation!");
		return QDF_STATUS_E_NOMEM;
	}
	vdev_id = wlan_vdev_get_id(vdev);
	ret = cdp_host_get_peer_stats_based_on_peer_type(
				wlan_psoc_get_dp_handle(psoc), vdev_id,
				peer_mac, peer_stats, peer_type);
	if (ret != QDF_STATUS_SUCCESS) {
		qdf_err("Unable to fetch stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_basic_peer_data_tx(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch Peer Basic TX stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_basic_peer_data_rx(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch Peer Basic RX stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_LINK) {
		ret = get_basic_peer_data_link(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch Peer Basic LINK stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RATE) {
		ret = get_basic_peer_data_rate(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch Peer Basic RATE stats!");
		else
			stats_collected = true;
	}

get_failed:
	qdf_mem_free(peer_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_basic_peer_ctrl(struct wlan_objmgr_psoc *psoc,
				      uint8_t *peer_mac,
				      struct unified_stats *stats,
				      uint32_t feat)
{
	struct wlan_objmgr_peer *peer;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	struct peer_ic_cp_stats *peer_cp_stats = NULL;
	bool stats_collected = false;

	peer = wlan_objmgr_get_peer_by_mac(psoc, peer_mac, WLAN_MLME_SB_ID);
	if (!peer) {
		qdf_err("Peer (%s) not found!", ether_sprintf(peer_mac));
		return QDF_STATUS_E_FAILURE;
	}
	peer_cp_stats = qdf_mem_malloc(sizeof(struct peer_ic_cp_stats));
	if (!peer_cp_stats) {
		qdf_err("Allocation Failed!");
		ret = QDF_STATUS_E_NOMEM;
		goto get_failed;
	}
	ret = wlan_cfg80211_get_peer_cp_stats(peer, peer_cp_stats);
	if (QDF_STATUS_SUCCESS != ret) {
		qdf_err("Failed to get Peer Control Stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_basic_peer_ctrl_tx(stats, peer_cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch Peer Basic TX stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_basic_peer_ctrl_rx(stats, peer_cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch Peer Basic RX stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_LINK) {
		ret = get_basic_peer_ctrl_link(stats, peer_cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch Peer Basic LINK stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RATE) {
		ret = get_basic_peer_ctrl_rate(stats, peer_cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch Peer Basic RATE stats!");
		else
			stats_collected = true;
	}

get_failed:
	wlan_objmgr_peer_release_ref(peer, WLAN_MLME_SB_ID);
	if (peer_cp_stats)
		qdf_mem_free(peer_cp_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_basic_vdev_data(struct wlan_objmgr_psoc *psoc,
				      struct wlan_objmgr_vdev *vdev,
				      struct unified_stats *stats,
				      uint32_t feat, bool mld_req,
				      bool mld_link)
{
	struct cdp_vdev_stats *vdev_stats = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	bool stats_collected = false;

	if (!psoc || !vdev) {
		qdf_err("Invalid psoc or vdev!");
		return QDF_STATUS_E_INVAL;
	}
	vdev_stats = qdf_mem_malloc(sizeof(struct cdp_vdev_stats));
	if (!vdev_stats) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}

#ifdef CONFIG_MLO_SINGLE_DEV
	if (mld_link)
		ret = cdp_mlo_get_mld_vdev_stats(wlan_psoc_get_dp_handle(psoc),
						 wlan_vdev_get_id(vdev),
						 vdev_stats, true);
	else if (mld_req)
		ret = cdp_mlo_get_mld_vdev_stats(wlan_psoc_get_dp_handle(psoc),
						 wlan_vdev_get_id(vdev),
						 vdev_stats, false);
	else
#endif
		ret = cdp_host_get_interface_stats(
						wlan_psoc_get_dp_handle(psoc),
						wlan_vdev_get_id(vdev),
						vdev_stats);

	if (ret != QDF_STATUS_SUCCESS) {
		qdf_err("Unable to get Vdev Stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_basic_vdev_data_tx(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Basic TX stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_basic_vdev_data_rx(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Basic RX stats!");
		else
			stats_collected = true;
	}

get_failed:
	qdf_mem_free(vdev_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

#ifdef CONFIG_MLO_SINGLE_DEV
static void aggregate_mld_vdev_stats(struct vdev_ic_cp_stats *tgt,
				     struct vdev_ic_cp_stats *src)
{
	tgt->stats.cs_rx_wrongbss += src->stats.cs_rx_wrongbss;
	tgt->stats.cs_rx_tooshort += src->stats.cs_rx_tooshort;
	tgt->stats.cs_rx_wrongdir += src->stats.cs_rx_wrongdir;
	tgt->stats.cs_rx_mcast_echo += src->stats.cs_rx_mcast_echo;
	tgt->stats.cs_rx_not_assoc += src->stats.cs_rx_not_assoc;
	tgt->stats.cs_rx_noprivacy += src->stats.cs_rx_noprivacy;
	tgt->stats.cs_rx_mgmt_discard += src->stats.cs_rx_mgmt_discard;
	tgt->stats.cs_rx_ctl += src->stats.cs_rx_ctl;
	tgt->stats.cs_rx_rs_too_big += src->stats.cs_rx_rs_too_big;
	tgt->stats.cs_rx_elem_missing += src->stats.cs_rx_elem_missing;
	tgt->stats.cs_rx_elem_too_big += src->stats.cs_rx_elem_too_big;
	tgt->stats.cs_rx_chan_err += src->stats.cs_rx_chan_err;
	tgt->stats.cs_rx_node_alloc += src->stats.cs_rx_node_alloc;
	tgt->stats.cs_rx_ssid_mismatch += src->stats.cs_rx_ssid_mismatch;
	tgt->stats.cs_rx_auth_unsupported += src->stats.cs_rx_auth_unsupported;
	tgt->stats.cs_rx_auth_fail += src->stats.cs_rx_auth_fail;
	tgt->stats.cs_rx_auth_countermeasures += src->stats.cs_rx_auth_countermeasures;
	tgt->stats.cs_rx_assoc_bss += src->stats.cs_rx_assoc_bss;
	tgt->stats.cs_rx_assoc_notauth += src->stats.cs_rx_assoc_notauth;
	tgt->stats.cs_rx_assoc_cap_mismatch += src->stats.cs_rx_assoc_cap_mismatch;
	tgt->stats.cs_rx_assoc_norate += src->stats.cs_rx_assoc_norate;
	tgt->stats.cs_rx_assoc_wpaie_err += src->stats.cs_rx_assoc_wpaie_err;
	tgt->stats.cs_rx_action += src->stats.cs_rx_action;
	tgt->stats.cs_rx_auth_err += src->stats.cs_rx_auth_err;
	tgt->stats.cs_tx_nodefkey += src->stats.cs_tx_nodefkey;
	tgt->stats.cs_tx_noheadroom += src->stats.cs_tx_noheadroom;
	tgt->stats.cs_rx_acl += src->stats.cs_rx_acl;
	tgt->stats.cs_rx_nowds += src->stats.cs_rx_nowds;
	tgt->stats.cs_tx_nobuf += src->stats.cs_tx_nobuf;
	tgt->stats.cs_tx_nonode += src->stats.cs_tx_nonode;
	tgt->stats.cs_tx_cipher_err += src->stats.cs_tx_cipher_err;
	tgt->stats.cs_tx_not_ok += src->stats.cs_tx_not_ok;
	tgt->stats.cs_tx_bcn_swba += src->stats.cs_tx_bcn_swba;
	tgt->stats.cs_node_timeout += src->stats.cs_node_timeout;
	tgt->stats.cs_crypto_nomem += src->stats.cs_crypto_nomem;
	tgt->stats.cs_crypto_tkip += src->stats.cs_crypto_tkip;
	tgt->stats.cs_crypto_tkipenmic += src->stats.cs_crypto_tkipenmic;
	tgt->stats.cs_crypto_tkipcm += src->stats.cs_crypto_tkipcm;
	tgt->stats.cs_crypto_ccmp += src->stats.cs_crypto_ccmp;
	tgt->stats.cs_crypto_wep += src->stats.cs_crypto_wep;
	tgt->stats.cs_crypto_setkey_cipher += src->stats.cs_crypto_setkey_cipher;
	tgt->stats.cs_crypto_setkey_nokey += src->stats.cs_crypto_setkey_nokey;
	tgt->stats.cs_crypto_delkey += src->stats.cs_crypto_delkey;
	tgt->stats.cs_crypto_cipher_err += src->stats.cs_crypto_cipher_err;
	tgt->stats.cs_crypto_attach_fail += src->stats.cs_crypto_attach_fail;
	tgt->stats.cs_crypto_swfallback += src->stats.cs_crypto_swfallback;
	tgt->stats.cs_crypto_keyfail += src->stats.cs_crypto_keyfail;
	tgt->stats.cs_ibss_capmismatch += src->stats.cs_ibss_capmismatch;
	tgt->stats.cs_ps_unassoc += src->stats.cs_ps_unassoc;
	tgt->stats.cs_ps_aid_err += src->stats.cs_ps_aid_err;
	tgt->stats.cs_padding += src->stats.cs_padding;
	tgt->stats.cs_tx_offchan_mgmt += src->stats.cs_tx_offchan_mgmt;
	tgt->stats.cs_tx_offchan_data += src->stats.cs_tx_offchan_data;
	tgt->stats.cs_tx_offchan_fail += src->stats.cs_tx_offchan_fail;
	tgt->stats.cs_invalid_macaddr_nodealloc_fail += src->stats.cs_invalid_macaddr_nodealloc_fail;
	tgt->stats.cs_tx_bcn_success += src->stats.cs_tx_bcn_success;
	tgt->stats.cs_tx_bcn_outage += src->stats.cs_tx_bcn_outage;
	tgt->stats.cs_sta_xceed_rlim += src->stats.cs_sta_xceed_rlim;
	tgt->stats.cs_sta_xceed_vlim += src->stats.cs_sta_xceed_vlim;
	tgt->stats.cs_mlme_auth_attempt += src->stats.cs_mlme_auth_attempt;
	tgt->stats.cs_mlme_auth_success += src->stats.cs_mlme_auth_success;
	tgt->stats.cs_authorize_attempt += src->stats.cs_authorize_attempt;
	tgt->stats.cs_authorize_success += src->stats.cs_authorize_success;
	tgt->stats.cs_peer_delete_req += src->stats.cs_peer_delete_req;
	tgt->stats.cs_peer_delete_resp += src->stats.cs_peer_delete_resp;
	tgt->stats.cs_peer_delete_all_req += src->stats.cs_peer_delete_all_req;
	tgt->stats.cs_peer_delete_all_resp += src->stats.cs_peer_delete_all_resp;
	tgt->stats.cs_prob_req_drops += src->stats.cs_prob_req_drops;
	tgt->stats.cs_oob_probe_req_count += src->stats.cs_oob_probe_req_count;
	tgt->stats.cs_wc_probe_req_drops += src->stats.cs_wc_probe_req_drops;
	tgt->stats.cs_fils_frames_sent += src->stats.cs_fils_frames_sent;
	tgt->stats.cs_fils_frames_sent_fail += src->stats.cs_fils_frames_sent_fail;
	tgt->stats.cs_tx_offload_prb_resp_succ_cnt += src->stats.cs_tx_offload_prb_resp_succ_cnt;
	tgt->stats.cs_tx_offload_prb_resp_fail_cnt += src->stats.cs_tx_offload_prb_resp_fail_cnt;
#ifdef QCA_SUPPORT_SCAN_SPCL_VAP_STATS
	tgt->stats.cs_tx_mgmt_ok_cnt += src->stats.cs_tx_mgmt_ok_cnt;
	tgt->stats.cs_tx_mgmt_ok_bytes += src->stats.cs_tx_mgmt_ok_bytes;
	tgt->stats.cs_tx_mgmt_retry_cnt += src->stats.cs_tx_mgmt_retry_cnt;
	tgt->stats.cs_tx_mgmt_retry_bytes += src->stats.cs_tx_mgmt_retry_bytes;
	tgt->stats.cs_tx_mgmt_err_cnt += src->stats.cs_tx_mgmt_err_cnt;
	tgt->stats.cs_tx_mgmt_err_bytes += src->stats.cs_tx_mgmt_err_bytes;
#endif

	/* Ucast stats */
	tgt->ucast_stats.cs_rx_badkeyid += src->ucast_stats.cs_rx_badkeyid;
	tgt->ucast_stats.cs_rx_decryptok += src->ucast_stats.cs_rx_decryptok;
	tgt->ucast_stats.cs_rx_decryptcrc += src->ucast_stats.cs_rx_decryptcrc;
	tgt->ucast_stats.cs_rx_pnerr += src->ucast_stats.cs_rx_pnerr;
	tgt->ucast_stats.cs_rx_wepfail += src->ucast_stats.cs_rx_wepfail;
	tgt->ucast_stats.cs_rx_tkipreplay += src->ucast_stats.cs_rx_tkipreplay;
	tgt->ucast_stats.cs_rx_tkipformat += src->ucast_stats.cs_rx_tkipformat;
	tgt->ucast_stats.cs_rx_tkipicv += src->ucast_stats.cs_rx_tkipicv;
	tgt->ucast_stats.cs_rx_ccmpreplay += src->ucast_stats.cs_rx_ccmpreplay;
	tgt->ucast_stats.cs_rx_ccmpformat += src->ucast_stats.cs_rx_ccmpformat;
	tgt->ucast_stats.cs_rx_ccmpmic += src->ucast_stats.cs_rx_ccmpmic;
	tgt->ucast_stats.cs_rx_wpireplay += src->ucast_stats.cs_rx_wpireplay;
	tgt->ucast_stats.cs_rx_wpimic += src->ucast_stats.cs_rx_wpimic;
	tgt->ucast_stats.cs_rx_countermeasure += src->ucast_stats.cs_rx_countermeasure;
	tgt->ucast_stats.cs_tx_mgmt += src->ucast_stats.cs_tx_mgmt;
	tgt->ucast_stats.cs_rx_mgmt += src->ucast_stats.cs_rx_mgmt;
	tgt->ucast_stats.cs_tx_discard += src->ucast_stats.cs_tx_discard;
	tgt->ucast_stats.cs_rx_discard += src->ucast_stats.cs_rx_discard;

	/*Mcast stats */
	tgt->mcast_stats.cs_rx_badkeyid += src->mcast_stats.cs_rx_badkeyid;
	tgt->mcast_stats.cs_rx_decryptok += src->mcast_stats.cs_rx_decryptok;
	tgt->mcast_stats.cs_rx_decryptcrc += src->mcast_stats.cs_rx_decryptcrc;
	tgt->mcast_stats.cs_rx_pnerr += src->mcast_stats.cs_rx_pnerr;
	tgt->mcast_stats.cs_rx_wepfail += src->mcast_stats.cs_rx_wepfail;
	tgt->mcast_stats.cs_rx_tkipreplay += src->mcast_stats.cs_rx_tkipreplay;
	tgt->mcast_stats.cs_rx_tkipformat += src->mcast_stats.cs_rx_tkipformat;
	tgt->mcast_stats.cs_rx_tkipicv += src->mcast_stats.cs_rx_tkipicv;
	tgt->mcast_stats.cs_rx_ccmpreplay += src->mcast_stats.cs_rx_ccmpreplay;
	tgt->mcast_stats.cs_rx_ccmpformat += src->mcast_stats.cs_rx_ccmpformat;
	tgt->mcast_stats.cs_rx_ccmpmic += src->mcast_stats.cs_rx_ccmpmic;
	tgt->mcast_stats.cs_rx_wpireplay += src->mcast_stats.cs_rx_wpireplay;
	tgt->mcast_stats.cs_rx_wpimic += src->mcast_stats.cs_rx_wpimic;
	tgt->mcast_stats.cs_rx_countermeasure += src->mcast_stats.cs_rx_countermeasure;
	tgt->mcast_stats.cs_tx_mgmt += src->mcast_stats.cs_tx_mgmt;
	tgt->mcast_stats.cs_rx_mgmt += src->mcast_stats.cs_rx_mgmt;
	tgt->mcast_stats.cs_tx_discard += src->mcast_stats.cs_tx_discard;
	tgt->mcast_stats.cs_rx_discard += src->mcast_stats.cs_rx_discard;
}
#endif

static QDF_STATUS get_basic_vdev_ctrl(struct wlan_objmgr_vdev *vdev,
				      struct unified_stats *stats,
				      uint32_t feat, bool mld_req)
{
	struct vdev_ic_cp_stats *cp_stats = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	bool stats_collected = false;

	if (!vdev) {
		qdf_err("Invalid vdev!");
		return QDF_STATUS_E_INVAL;
	}
	cp_stats = qdf_mem_malloc(sizeof(struct vdev_ic_cp_stats));
	if (!cp_stats) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}

#ifdef CONFIG_MLO_SINGLE_DEV
	if (mld_req) {
		uint16_t num_links = 0, i = 0;
		QDF_STATUS status = QDF_STATUS_SUCCESS;
		struct vdev_ic_cp_stats *temp_cp_stats = NULL;
		struct wlan_objmgr_vdev *vdev_list[WLAN_UMAC_MLO_MAX_VDEVS] = {NULL};

		temp_cp_stats = qdf_mem_malloc(sizeof(struct vdev_ic_cp_stats));
		if (!temp_cp_stats) {
			qdf_err("Allocation Failed!");
			qdf_mem_free(cp_stats);
			return QDF_STATUS_E_NOMEM;
		}

		mlo_ap_get_vdev_list(vdev, &num_links, vdev_list);
		for (i = 0; i < num_links; i++) {
			status = wlan_cfg80211_get_vdev_cp_stats(vdev_list[i],
								 temp_cp_stats);
			mlo_release_vdev_ref(vdev_list[i]);
			if (status != QDF_STATUS_SUCCESS) {
				ret = status;
				continue;
			}
			aggregate_mld_vdev_stats(cp_stats, temp_cp_stats);
		}
		qdf_mem_free(temp_cp_stats);
	} else
#endif
	{
		ret = wlan_cfg80211_get_vdev_cp_stats(vdev, cp_stats);
	}

	if (QDF_STATUS_SUCCESS != ret) {
		qdf_err("Unable to get Vdev Control stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_basic_vdev_ctrl_tx(stats, cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Basic TX stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_basic_vdev_ctrl_rx(stats, cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Basic RX stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_LINK) {
		ret = get_basic_vdev_ctrl_link(stats, vdev);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Basic LINK stats!");
		else
			stats_collected = true;
	}

get_failed:
	qdf_mem_free(cp_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_vdev_cp_stats(struct wlan_objmgr_vdev *vdev,
				    struct vdev_ic_cp_stats *cp_stats)
{
	qdf_event_t wait_event;
	struct ieee80211vap *vap;

	vap = wlan_vdev_mlme_get_ext_hdl(vdev);
	if (!vap) {
		qdf_err("vap is NULL!");
		return QDF_STATUS_E_INVAL;
	}

	if (vap->get_vdev_bcn_stats &&
	    (CONVERT_SYSTEM_TIME_TO_MS(OS_GET_TICKS() -
	    vap->vap_bcn_stats_time) > 2000)) {
		qdf_mem_zero(&wait_event, sizeof(wait_event));
		qdf_event_create(&wait_event);
		qdf_event_reset(&wait_event);
		vap->get_vdev_bcn_stats(vap);
		/* give enough delay in ms (50ms) to get beacon stats */
		qdf_wait_single_event(&wait_event, 50);
		if (qdf_atomic_read(&vap->vap_bcn_event) != 1)
			qdf_debug("VAP BCN STATS FAILED");
		qdf_event_destroy(&wait_event);
	}
	if (vap->get_vdev_prb_fils_stats) {
		qdf_mem_zero(&wait_event, sizeof(wait_event));
		qdf_event_create(&wait_event);
		qdf_event_reset(&wait_event);
		qdf_atomic_init(&vap->vap_prb_fils_event);
		vap->get_vdev_prb_fils_stats(vap);
		/* give enough delay in ms (50ms) to get fils stats */
		qdf_wait_single_event(&wait_event, 50);
		if (qdf_atomic_read(&vap->vap_prb_fils_event) != 1)
			qdf_debug("VAP PRB and FILS STATS FAILED");
		qdf_event_destroy(&wait_event);
	}

	return wlan_cfg80211_get_vdev_cp_stats(vdev, cp_stats);
}

static void aggregate_basic_pdev_ctrl_tx(struct basic_pdev_ctrl_tx *tx,
					 struct vdev_ic_cp_stats *cp_stats)
{
	tx->cs_tx_mgmt += cp_stats->ucast_stats.cs_tx_mgmt;
}

static void aggregate_basic_pdev_ctrl_rx(struct basic_pdev_ctrl_rx *rx,
					 struct vdev_ic_cp_stats *cp_stats)
{
	rx->cs_rx_mgmt += cp_stats->ucast_stats.cs_rx_mgmt;
}

static void aggr_basic_pdev_ctrl_stats(struct wlan_objmgr_pdev *pdev,
				       void *object, void *arg)
{
	struct wlan_objmgr_vdev *vdev = object;
	struct iterator_ctx *ctx = arg;
	struct unified_stats *stats;
	struct vdev_ic_cp_stats *cp_stats;

	if (!vdev || !ctx || !ctx->pvt || !ctx->stats)
		return;
	if (wlan_vdev_mlme_get_opmode(vdev) != QDF_SAP_MODE)
		return;
	cp_stats = ctx->pvt;
	stats = ctx->stats;
	qdf_mem_zero(cp_stats, sizeof(struct vdev_ic_cp_stats));
	get_vdev_cp_stats(vdev, cp_stats);

	if (stats->feat[INX_FEAT_TX])
		aggregate_basic_pdev_ctrl_tx(stats->feat[INX_FEAT_TX],
					     cp_stats);
	if (stats->feat[INX_FEAT_RX])
		aggregate_basic_pdev_ctrl_rx(stats->feat[INX_FEAT_RX],
					     cp_stats);
}

static void aggregate_basic_pdev_stats(struct wlan_objmgr_pdev *pdev,
				       struct unified_stats *stats,
				       enum stats_type_e type)
{
	struct iterator_ctx ctx = {0};
	struct vdev_ic_cp_stats *cp_stats = NULL;

	switch (type) {
	case STATS_TYPE_DATA:
		break;
	case STATS_TYPE_CTRL:
		cp_stats = qdf_mem_malloc(sizeof(struct vdev_ic_cp_stats));
		if (!cp_stats) {
			qdf_debug("Allocation Failed!");
			return;
		}
		ctx.pvt = cp_stats;
		ctx.stats = stats;
		wlan_objmgr_pdev_iterate_obj_list(pdev, WLAN_VDEV_OP,
						  aggr_basic_pdev_ctrl_stats,
						  &ctx, 1, WLAN_MLME_SB_ID);
		break;
	default:
		qdf_err("Invalid type %d!", type);
	}

	if (cp_stats)
		qdf_mem_free(cp_stats);
}

static QDF_STATUS get_basic_pdev_data(struct wlan_objmgr_psoc *psoc,
				      struct wlan_objmgr_pdev *pdev,
				      struct unified_stats *stats,
				      uint32_t feat)
{
	struct cdp_pdev_stats *pdev_stats = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	bool stats_collected = false;

	if (!psoc || !pdev) {
		qdf_err("Invalid pdev and psoc!");
		return QDF_STATUS_E_INVAL;
	}
	pdev_stats = qdf_mem_malloc(sizeof(struct cdp_pdev_stats));
	if (!pdev_stats) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	ret = cdp_host_get_pdev_stats(wlan_psoc_get_dp_handle(psoc),
				      wlan_objmgr_pdev_get_pdev_id(pdev),
				      pdev_stats);
	if (ret != QDF_STATUS_SUCCESS) {
		qdf_err("Unable to get Pdev stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_basic_pdev_data_tx(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Basic TX stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_basic_pdev_data_rx(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Basic RX stats!");
		else
			stats_collected = true;
	}

get_failed:
	qdf_mem_free(pdev_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_basic_pdev_ctrl(struct wlan_objmgr_pdev *pdev,
				      struct unified_stats *stats,
				      uint32_t feat, bool aggregate)
{
	struct pdev_ic_cp_stats *pdev_cp_stats = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	bool stats_collected = false;
	bool aggr_required = false;

	if (!pdev) {
		qdf_err("Invalid pdev!");
		return QDF_STATUS_E_INVAL;
	}
	pdev_cp_stats = qdf_mem_malloc(sizeof(struct pdev_ic_cp_stats));
	if (!pdev_cp_stats) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	ret = wlan_cfg80211_get_pdev_cp_stats(pdev, pdev_cp_stats);
	if (QDF_STATUS_SUCCESS != ret) {
		qdf_err("Unbale to get pdev control stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_basic_pdev_ctrl_tx(stats, pdev_cp_stats);
		if (ret != QDF_STATUS_SUCCESS) {
			qdf_err("Unable to fetch pdev Basic TX stats!");
		} else {
			stats_collected = true;
			aggr_required = aggregate;
		}
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_basic_pdev_ctrl_rx(stats, pdev_cp_stats);
		if (ret != QDF_STATUS_SUCCESS) {
			qdf_err("Unable to fetch pdev Basic RX stats!");
		} else {
			stats_collected = true;
			aggr_required = aggregate;
		}
	}
	if (feat & STATS_FEAT_FLG_LINK) {
		ret = get_basic_pdev_ctrl_link(stats, pdev_cp_stats, pdev);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Basic LINK stats!");
		else
			stats_collected = true;
	}

	if (aggr_required)
		aggregate_basic_pdev_stats(pdev, stats, STATS_TYPE_CTRL);

get_failed:
	qdf_mem_free(pdev_cp_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_basic_psoc_data(struct wlan_objmgr_psoc *psoc,
				      struct unified_stats *stats,
				      uint32_t feat)
{
	struct cdp_soc_stats *psoc_stats = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	bool stats_collected = false;

	if (!psoc) {
		qdf_err("Invalid psoc!");
		return QDF_STATUS_E_INVAL;
	}
	psoc_stats = qdf_mem_malloc(sizeof(struct cdp_soc_stats));
	if (!psoc_stats) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	ret = cdp_host_get_soc_stats(wlan_psoc_get_dp_handle(psoc), psoc_stats);
	if (QDF_STATUS_SUCCESS != ret) {
		qdf_err("Unable to get Psoc stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_basic_psoc_data_tx(stats, psoc_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch psoc Basic TX stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_basic_psoc_data_rx(stats, psoc_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch psoc Basic RX stats!");
		else
			stats_collected = true;
	}

get_failed:
	qdf_mem_free(psoc_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

#if CONFIG_SAWF
static void fill_advance_peer_if_delay_avg_stats(
		struct stats_if_delay_tid_stats *delay_stats,
		struct cdp_delay_tx_stats *ptx_delay)
{
	delay_stats->tx_delay.nwdelay_avg =
					ptx_delay->nwdelay_avg;
	delay_stats->tx_delay.swdelay_avg =
					ptx_delay->swdelay_avg;
	delay_stats->tx_delay.hwdelay_avg =
					ptx_delay->hwdelay_avg;
}
#else
#define fill_advance_peer_if_delay_avg_stats(delay_stats, ptx_delay)
#endif

#if WLAN_ADVANCE_TELEMETRY
static void
fill_advance_peer_if_delay_stats(struct advance_peer_data_delay *data,
				 struct cdp_delay_tid_stats *cdp_delay)
{
	uint8_t inx = 0, tidx = 0, tids_count, max_bucket;
	struct stats_if_delay_tid_stats *delay_stats;
	struct cdp_delay_tid_stats *peer_delay;
	struct cdp_delay_tx_stats *ptx_delay;
	struct cdp_delay_rx_stats *prx_delay;

	tids_count = qdf_min((uint8_t)STATS_IF_MAX_DATA_TIDS,
			     (uint8_t)CDP_MAX_DATA_TIDS);
	max_bucket = qdf_min((uint8_t)STATS_IF_HIST_BUCKET_MAX,
			     (uint8_t)CDP_HIST_BUCKET_MAX);
	for (tidx = 0; tidx < tids_count; tidx++) {
		delay_stats = &data->delay_stats[tidx];
		peer_delay = &cdp_delay[tidx];
		ptx_delay = &peer_delay->tx_delay;
		prx_delay = &peer_delay->rx_delay;

		delay_stats->tx_delay.tx_swq_delay.max =
						ptx_delay->tx_swq_delay.max;
		delay_stats->tx_delay.tx_swq_delay.min =
						ptx_delay->tx_swq_delay.min;
		delay_stats->tx_delay.tx_swq_delay.avg =
						ptx_delay->tx_swq_delay.avg;
		delay_stats->tx_delay.tx_swq_delay.hist.hist_type =
					ptx_delay->tx_swq_delay.hist.hist_type;
		delay_stats->tx_delay.hwtx_delay.max =
						ptx_delay->hwtx_delay.max;
		delay_stats->tx_delay.hwtx_delay.min =
						ptx_delay->hwtx_delay.min;
		delay_stats->tx_delay.hwtx_delay.avg =
						ptx_delay->hwtx_delay.avg;
		fill_advance_peer_if_delay_avg_stats(delay_stats, ptx_delay);

		delay_stats->tx_delay.hwtx_delay.hist.hist_type =
					ptx_delay->hwtx_delay.hist.hist_type;

		delay_stats->rx_delay.to_stack_delay.max =
						prx_delay->to_stack_delay.max;
		delay_stats->rx_delay.to_stack_delay.min =
						prx_delay->to_stack_delay.min;
		delay_stats->rx_delay.to_stack_delay.avg =
						prx_delay->to_stack_delay.avg;

		delay_stats->rx_delay.to_stack_delay.hist.hist_type =
				prx_delay->to_stack_delay.hist.hist_type;

		for (inx = 0; inx < max_bucket; inx++) {
			delay_stats->tx_delay.tx_swq_delay.hist.freq[inx] =
					ptx_delay->tx_swq_delay.hist.freq[inx];

			delay_stats->tx_delay.hwtx_delay.hist.freq[inx] =
					ptx_delay->hwtx_delay.hist.freq[inx];

			delay_stats->rx_delay.to_stack_delay.hist.freq[inx] =
				prx_delay->to_stack_delay.hist.freq[inx];
		}
	}
}

static void
fill_advance_peer_jitter_stats(struct advance_peer_data_jitter *data,
			       struct cdp_peer_tid_stats *cdp_jitter)
{
	uint8_t tidx = 0, tids_count;
	struct stats_if_jitter_tid_stats *jitter_stats;

	tids_count = qdf_min((uint8_t)STATS_IF_MAX_DATA_TIDS,
			     (uint8_t)CDP_MAX_DATA_TIDS);
	for (tidx = 0; tidx < tids_count; tidx++) {
		jitter_stats = &data->jitter_stats[tidx];
		jitter_stats->tx_avg_jitter = cdp_jitter[tidx].tx_avg_jitter;
		jitter_stats->tx_avg_delay = cdp_jitter[tidx].tx_avg_delay;
		jitter_stats->tx_avg_err = cdp_jitter[tidx].tx_avg_err;
		jitter_stats->tx_total_success = cdp_jitter[tidx].tx_total_success;
		jitter_stats->tx_drop = cdp_jitter[tidx].tx_drop;
	}
}

#if CONFIG_SAWF
static void
fill_advance_peer_sawftx_stats(struct advance_peer_data_sawftx *data,
			       struct sawf_tx_stats *tx_stats)
{
	uint8_t tidx = 0, queues = 0, tidx_count, queues_count;

	tidx_count = qdf_min((uint8_t)STATS_IF_MAX_SAWF_DATA_TIDS,
			     (uint8_t)DP_SAWF_MAX_TIDS);
	queues_count = qdf_min((uint8_t)STATS_IF_MAX_SAWF_DATA_QUEUE,
			       (uint8_t)DP_SAWF_MAX_QUEUES);
	for (tidx = 0; tidx < tidx_count; tidx++) {
		for (queues = 0; queues < queues_count; queues++) {
			data->tx[tidx][queues].tx_success.num =
					tx_stats->tx_success.num;
			data->tx[tidx][queues].tx_success.bytes =
					tx_stats->tx_success.bytes;
			data->tx[tidx][queues].tx_ingress.num =
					tx_stats->tx_ingress.num;
			data->tx[tidx][queues].tx_ingress.bytes =
					tx_stats->tx_ingress.bytes;
			data->tx[tidx][queues].dropped.fw_rem.num =
					tx_stats->dropped.fw_rem.num;
			data->tx[tidx][queues].dropped.fw_rem.bytes =
					tx_stats->dropped.fw_rem.bytes;
			data->tx[tidx][queues].dropped.fw_rem_notx =
					tx_stats->dropped.fw_rem_notx;
			data->tx[tidx][queues].dropped.fw_rem_tx =
					tx_stats->dropped.fw_rem_tx;
			data->tx[tidx][queues].dropped.age_out =
					tx_stats->dropped.age_out;
			data->tx[tidx][queues].dropped.fw_reason1 =
					tx_stats->dropped.fw_reason1;
			data->tx[tidx][queues].dropped.fw_reason2 =
					tx_stats->dropped.fw_reason2;
			data->tx[tidx][queues].dropped.fw_reason3 =
					tx_stats->dropped.fw_reason3;
			data->tx[tidx][queues].dropped.fw_rem_queue_disable =
					tx_stats->dropped.fw_rem_queue_disable;
			data->tx[tidx][queues].dropped.fw_rem_no_match =
					tx_stats->dropped.fw_rem_no_match;
			data->tx[tidx][queues].dropped.drop_threshold =
					tx_stats->dropped.drop_threshold;
			data->tx[tidx][queues].dropped.drop_link_desc_na =
					tx_stats->dropped.drop_link_desc_na;
			data->tx[tidx][queues].dropped.invalid_drop =
					tx_stats->dropped.invalid_drop;
			data->tx[tidx][queues].dropped.mcast_vdev_drop =
					tx_stats->dropped.mcast_vdev_drop;
			data->tx[tidx][queues].dropped.invalid_rr =
					tx_stats->dropped.invalid_rr;
			data->tx[tidx][queues].tx_failed =
					tx_stats->tx_failed;
			data->tx[tidx][queues].queue_depth =
					tx_stats->queue_depth;
			data->tx[tidx][queues].retry_count =
					tx_stats->retry_count;
			data->tx[tidx][queues].multiple_retry_count =
					tx_stats->multiple_retry_count;
			data->tx[tidx][queues].failed_retry_count =
					tx_stats->failed_retry_count;
			data->tx[tidx][queues].svc_intval_stats.success_cnt =
					tx_stats->svc_intval_stats.success_cnt;
			data->tx[tidx][queues].svc_intval_stats.failure_cnt =
					tx_stats->svc_intval_stats.failure_cnt;
			data->tx[tidx][queues].burst_size_stats.success_cnt =
					tx_stats->burst_size_stats.success_cnt;
			data->tx[tidx][queues].burst_size_stats.failure_cnt =
					tx_stats->burst_size_stats.failure_cnt;
			data->tx[tidx][queues].throughput =
					tx_stats->throughput;
			data->tx[tidx][queues].ingress_rate =
					tx_stats->ingress_rate;
			tx_stats++;
		}
	}
}

static void
fill_advance_peer_sawfdelay_stats(struct advance_peer_data_sawfdelay *data,
				  struct sawf_delay_stats *delay_stats)
{
	uint8_t tidx = 0, queues = 0, idx = 0, tidx_count, queues_count;
	uint8_t mx_win = 0, mx_buc;

	tidx_count = qdf_min((uint8_t)STATS_IF_MAX_SAWF_DATA_TIDS,
			     (uint8_t)DP_SAWF_MAX_TIDS);
	queues_count = qdf_min((uint8_t)STATS_IF_MAX_SAWF_DATA_QUEUE,
			       (uint8_t)DP_SAWF_MAX_QUEUES);
	mx_buc = qdf_min((uint8_t)STATS_IF_HIST_BUCKET_MAX,
			 (uint8_t)CDP_HIST_BUCKET_MAX);
	mx_win = qdf_min((uint8_t)STATS_IF_NUM_AVG_WINDOWS,
			 (uint8_t)DP_SAWF_NUM_AVG_WINDOWS);
	for (tidx = 0; tidx < tidx_count; tidx++) {
		for (queues = 0; queues < queues_count; queues++) {
			data->delay[tidx][queues].delay_hist.max =
					delay_stats->delay_hist.max;
			data->delay[tidx][queues].delay_hist.min =
					delay_stats->delay_hist.min;
			data->delay[tidx][queues].delay_hist.avg =
					delay_stats->delay_hist.avg;
			data->delay[tidx][queues].delay_hist.hist.hist_type =
					delay_stats->delay_hist.hist.hist_type;
			for (idx = 0; idx < mx_buc; idx++)
				data->delay[tidx][queues].delay_hist.hist.freq[idx] =
					delay_stats->delay_hist.hist.freq[idx];
			data->delay[tidx][queues].nwdelay_avg =
					delay_stats->nwdelay_avg;
			data->delay[tidx][queues].swdelay_avg =
					delay_stats->swdelay_avg;
			data->delay[tidx][queues].hwdelay_avg =
					delay_stats->hwdelay_avg;
			data->delay[tidx][queues].delay_bound_success =
					delay_stats->success;
			data->delay[tidx][queues].delay_bound_failure =
					delay_stats->failure;
			delay_stats++;
		}
	}
}
#endif

#ifdef WLAN_FEATURE_11BE
#define update_ppdu_be_stats(dst, src)                                         \
	do {                                                                   \
		uint8_t inx, count, mcs_cnt;                                   \
		count = qdf_min((uint8_t)MAX_PUNCTURED_MODE,                   \
				(uint8_t)STATS_IF_MAX_PUNCTURED_MODE);         \
		qdf_mem_copy(&dst->punc_bw[0], &src->punc_bw[0],               \
			     count * sizeof(uint32_t));                        \
		mcs_cnt = qdf_min((uint8_t)MAX_MCS,                            \
				  (uint8_t)STATS_IF_MAX_MCS);                  \
		qdf_mem_copy(&dst->su_be_ppdu_cnt.mcs_count[0],                \
			     &src->su_be_ppdu_cnt.mcs_count[0],                \
			     mcs_cnt * sizeof(uint32_t));                      \
		loop_cnt = qdf_min((uint8_t)TXRX_TYPE_MU_MAX,                  \
				   (uint8_t)STATS_IF_TXRX_TYPE_MU_MAX);        \
		for (inx = 0; inx < loop_cnt; inx++)                           \
			qdf_mem_copy(&dst->mu_be_ppdu_cnt[inx].mcs_count[0],   \
				     &src->mu_be_ppdu_cnt[inx].mcs_count[0],   \
				     mcs_cnt * sizeof(uint32_t));              \
	} while (0)
#else
#define update_ppdu_be_stats(dst, src) {}
#endif /* WLAN_FEATURE_11BE */

static void fill_advance_data_tx_stats(struct advance_data_tx_stats *tx,
				       struct cdp_tx_stats *cdp_tx)
{
	uint8_t inx = 0;
	uint8_t loop_cnt;

	tx->ucast.num = cdp_tx->ucast.num;
	tx->ucast.bytes = cdp_tx->ucast.bytes;
	tx->mcast.num = cdp_tx->mcast.num;
	tx->mcast.bytes = cdp_tx->mcast.bytes;
	tx->bcast.num = cdp_tx->bcast.num;
	tx->bcast.bytes = cdp_tx->bcast.bytes;

	loop_cnt = qdf_min((uint8_t)MAX_GI, (uint8_t)STATS_IF_MAX_GI);
	for (inx = 0; inx < loop_cnt; inx++)
		tx->sgi_count[inx] = cdp_tx->sgi_count[inx];
	loop_cnt = qdf_min((uint8_t)SS_COUNT, (uint8_t)STATS_IF_SS_COUNT);
	for (inx = 0; inx < loop_cnt; inx++)
		tx->nss[inx] = cdp_tx->nss[inx];
	loop_cnt = qdf_min((uint8_t)MAX_BW, (uint8_t)STATS_IF_MAX_BW);
	for (inx = 0; inx < loop_cnt; inx++)
		tx->bw[inx] = cdp_tx->bw[inx];
	loop_cnt = qdf_min((uint8_t)WME_AC_MAX, (uint8_t)STATS_IF_WME_AC_MAX);
	for (inx = 0; inx < loop_cnt; inx++) {
		tx->wme_ac_type[inx] = cdp_tx->wme_ac_type[inx];
		tx->wme_ac_type_bytes[inx] =
					cdp_tx->wme_ac_type_bytes[inx];
	}

	tx->retries = cdp_tx->retries;
	tx->retries_mpdu = cdp_tx->retries_mpdu;
	tx->per = cdp_tx->per;
	tx->tx_rate = cdp_tx->tx_byte_rate;
	tx->non_amsdu_cnt = cdp_tx->non_amsdu_cnt;
	tx->amsdu_cnt = cdp_tx->amsdu_cnt;
	tx->ampdu_cnt = cdp_tx->ampdu_cnt;
	tx->non_ampdu_cnt = cdp_tx->non_ampdu_cnt;
	tx->failed_retry_count = cdp_tx->failed_retry_count;
	tx->retry_count = cdp_tx->retry_count;
	tx->multiple_retry_count = cdp_tx->multiple_retry_count;
	tx->mpdu_retries = cdp_tx->mpdu_retries;
	tx->total_mpdu_retries = cdp_tx->total_mpdu_retries;
	tx->release_src_not_tqm = cdp_tx->release_src_not_tqm;
	tx->inval_link_id = cdp_tx->inval_link_id_pkt_cnt;
	tx->tx_ppdus = cdp_tx->tx_ppdus;
	tx->tx_mpdus_success = cdp_tx->tx_mpdus_success;
	tx->tx_mpdus_tried = cdp_tx->tx_mpdus_tried;
	tx->mpdu_success_with_retries = cdp_tx->mpdu_success_with_retries;
	update_ppdu_be_stats(tx, cdp_tx);
}

static void fill_advance_data_rx_stats(struct advance_data_rx_stats *rx,
				       struct cdp_rx_stats *cdp_rx)
{
	uint8_t inx = 0;
	uint8_t loop_cnt;

	rx->unicast.num = cdp_rx->unicast.num;
	rx->unicast.bytes = cdp_rx->unicast.bytes;
	rx->multicast.num = cdp_rx->multicast.num;
	rx->multicast.bytes = cdp_rx->multicast.bytes;
	rx->bcast.num = cdp_rx->bcast.num;
	rx->bcast.bytes = cdp_rx->bcast.bytes;
	loop_cnt = qdf_min((uint8_t)MAX_MCS, (uint8_t)STATS_IF_MAX_MCS);
	for (inx = 0; inx < loop_cnt; inx++) {
		rx->su_ax_ppdu_cnt[inx] =
			cdp_rx->su_ax_ppdu_cnt.mcs_count[inx];
		rx->rx_mpdu_cnt[inx] = cdp_rx->rx_mpdu_cnt[inx];
	}
	loop_cnt = qdf_min((uint8_t)WME_AC_MAX, (uint8_t)STATS_IF_WME_AC_MAX);
	for (inx = 0; inx < loop_cnt; inx++) {
		rx->wme_ac_type[inx] = cdp_rx->wme_ac_type[inx];
		rx->wme_ac_type_bytes[inx] = cdp_rx->wme_ac_type_bytes[inx];
	}

	loop_cnt = qdf_min((uint8_t)MAX_GI, (uint8_t)STATS_IF_MAX_GI);
	for (inx = 0; inx < loop_cnt; inx++)
		rx->sgi_count[inx] = cdp_rx->sgi_count[inx];
	loop_cnt = qdf_min((uint8_t)SS_COUNT, (uint8_t)STATS_IF_SS_COUNT);
	for (inx = 0; inx < loop_cnt; inx++) {
		rx->nss[inx] = cdp_rx->nss[inx];
		rx->ppdu_nss[inx] = cdp_rx->ppdu_nss[inx];
	}
	loop_cnt = qdf_min((uint8_t)MAX_BW, (uint8_t)STATS_IF_MAX_BW);
	for (inx = 0; inx < loop_cnt; inx++)
		rx->bw[inx] = cdp_rx->bw[inx];
	rx->mpdu_cnt_fcs_ok = cdp_rx->mpdu_cnt_fcs_ok;
	rx->mpdu_cnt_fcs_err = cdp_rx->mpdu_cnt_fcs_err;
	rx->non_amsdu_cnt = cdp_rx->non_amsdu_cnt;
	rx->non_ampdu_cnt = cdp_rx->non_ampdu_cnt;
	rx->ampdu_cnt = cdp_rx->ampdu_cnt;
	rx->amsdu_cnt = cdp_rx->amsdu_cnt;
	rx->bar_recv_cnt = cdp_rx->bar_recv_cnt;
	rx->rx_retries = cdp_rx->rx_retries;
	rx->multipass_rx_pkt_drop = cdp_rx->multipass_rx_pkt_drop;
	rx->inval_link_id = cdp_rx->inval_link_id_pkt_cnt;
	rx->mcast_3addr_drop = cdp_rx->mcast_3addr_drop;
	rx->policy_check_drop = cdp_rx->policy_check_drop;
	rx->peer_unauth_rx_pkt_drop = cdp_rx->peer_unauth_rx_pkt_drop;
	rx->mpdu_retry_cnt = cdp_rx->mpdu_retry_cnt;
	update_ppdu_be_stats(rx, cdp_rx);
}

static QDF_STATUS
get_advance_peer_data_delay(struct cdp_delay_tid_stats *delay,
			    struct unified_stats *stats)
{
	struct advance_peer_data_delay *data = NULL;

	data = qdf_mem_malloc(sizeof(struct advance_peer_data_delay));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_advance_peer_if_delay_stats(data, delay);
	stats->feat[INX_FEAT_DELAY] = data;
	stats->size[INX_FEAT_DELAY] = sizeof(struct advance_peer_data_delay);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS
get_advance_peer_jitter_stats(struct cdp_peer_tid_stats *jitter,
			      struct unified_stats *stats)
{
	struct advance_peer_data_jitter *data = NULL;

	data = qdf_mem_malloc(sizeof(struct advance_peer_data_jitter));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_advance_peer_jitter_stats(data, jitter);
	stats->feat[INX_FEAT_JITTER] = data;
	stats->size[INX_FEAT_JITTER] = sizeof(struct advance_peer_data_jitter);

	return QDF_STATUS_SUCCESS;
}

#if CONFIG_SAWF
static QDF_STATUS
get_advance_peer_data_sawfdelay(struct sawf_delay_stats *sawf_delay_stats,
				struct unified_stats *stats, uint8_t svc_id)
{
	struct advance_peer_data_sawfdelay *data = NULL;

	data = qdf_mem_malloc(sizeof(struct advance_peer_data_sawfdelay));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	if (svc_id == 0) {
		fill_advance_peer_sawfdelay_stats(data, sawf_delay_stats);
	} else {
		uint8_t idx = 0;
		uint8_t mx_buc, mx_win;

		mx_buc = qdf_min((uint8_t)STATS_IF_HIST_BUCKET_MAX,
				 (uint8_t)CDP_HIST_BUCKET_MAX);
		mx_win = qdf_min((uint8_t)STATS_IF_NUM_AVG_WINDOWS,
				 (uint8_t)DP_SAWF_NUM_AVG_WINDOWS);

		data->delay[0][0].delay_hist.max =
					sawf_delay_stats->delay_hist.max;
		data->delay[0][0].delay_hist.min =
					sawf_delay_stats->delay_hist.min;
		data->delay[0][0].delay_hist.avg =
					sawf_delay_stats->delay_hist.avg;
		data->delay[0][0].delay_hist.hist.hist_type =
				sawf_delay_stats->delay_hist.hist.hist_type;
		data->delay[0][0].nwdelay_avg =
					sawf_delay_stats->nwdelay_avg;
		data->delay[0][0].swdelay_avg =
					sawf_delay_stats->swdelay_avg;
		data->delay[0][0].hwdelay_avg =
					sawf_delay_stats->hwdelay_avg;

		for (idx = 0; idx < mx_buc; idx++)
			data->delay[0][0].delay_hist.hist.freq[idx] =
				sawf_delay_stats->delay_hist.hist.freq[idx];
		data->delay[0][0].delay_bound_success =
				sawf_delay_stats->success;
		data->delay[0][0].delay_bound_failure =
			sawf_delay_stats->failure;
		data->tid = sawf_delay_stats->tid;
		data->msduq = sawf_delay_stats->msduq;
	}

	stats->feat[INX_FEAT_SAWFDELAY] = data;
	stats->size[INX_FEAT_SAWFDELAY] =
			sizeof(struct advance_peer_data_sawfdelay);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS
get_advance_peer_data_sawftx(struct sawf_tx_stats *sawf_tx_stats,
			     struct unified_stats *stats, uint8_t svc_id)
{
	struct advance_peer_data_sawftx *data = NULL;

	data = qdf_mem_malloc(sizeof(struct advance_peer_data_sawftx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	if (svc_id == 0) {
		fill_advance_peer_sawftx_stats(data, sawf_tx_stats);
	} else {
		data->tx[0][0].tx_success.num =
					sawf_tx_stats->tx_success.num;
		data->tx[0][0].tx_success.bytes =
					sawf_tx_stats->tx_success.bytes;
		data->tx[0][0].tx_ingress.num =
					sawf_tx_stats->tx_ingress.num;
		data->tx[0][0].tx_ingress.bytes =
					sawf_tx_stats->tx_ingress.bytes;
		data->tx[0][0].dropped.fw_rem.num =
					sawf_tx_stats->dropped.fw_rem.num;
		data->tx[0][0].dropped.fw_rem.bytes =
					sawf_tx_stats->dropped.fw_rem.bytes;
		data->tx[0][0].dropped.fw_rem_notx =
					sawf_tx_stats->dropped.fw_rem_notx;
		data->tx[0][0].dropped.fw_rem_tx =
					sawf_tx_stats->dropped.fw_rem_tx;
		data->tx[0][0].dropped.age_out =
					sawf_tx_stats->dropped.age_out;
		data->tx[0][0].dropped.fw_reason1 =
					sawf_tx_stats->dropped.fw_reason1;
		data->tx[0][0].dropped.fw_reason2 =
					sawf_tx_stats->dropped.fw_reason2;
		data->tx[0][0].dropped.fw_reason3 =
					sawf_tx_stats->dropped.fw_reason3;
		data->tx[0][0].dropped.fw_rem_queue_disable =
				sawf_tx_stats->dropped.fw_rem_queue_disable;
		data->tx[0][0].dropped.fw_rem_no_match =
				sawf_tx_stats->dropped.fw_rem_no_match;
		data->tx[0][0].dropped.drop_threshold =
				sawf_tx_stats->dropped.drop_threshold;
		data->tx[0][0].dropped.drop_link_desc_na =
				sawf_tx_stats->dropped.drop_link_desc_na;
		data->tx[0][0].dropped.invalid_drop =
				sawf_tx_stats->dropped.invalid_drop;
		data->tx[0][0].dropped.mcast_vdev_drop =
				sawf_tx_stats->dropped.mcast_vdev_drop;
		data->tx[0][0].dropped.invalid_rr =
				sawf_tx_stats->dropped.invalid_rr;
		data->tx[0][0].tx_failed =
					sawf_tx_stats->tx_failed;
		data->tx[0][0].queue_depth = sawf_tx_stats->queue_depth;
		data->tx[0][0].retry_count =
				sawf_tx_stats->retry_count;
		data->tx[0][0].multiple_retry_count =
				sawf_tx_stats->multiple_retry_count;
		data->tx[0][0].failed_retry_count =
				sawf_tx_stats->failed_retry_count;
		data->tx[0][0].svc_intval_stats.success_cnt =
			sawf_tx_stats->svc_intval_stats.success_cnt;
		data->tx[0][0].svc_intval_stats.failure_cnt =
			sawf_tx_stats->svc_intval_stats.failure_cnt;
		data->tx[0][0].burst_size_stats.success_cnt =
			sawf_tx_stats->burst_size_stats.success_cnt;
		data->tx[0][0].burst_size_stats.failure_cnt =
			sawf_tx_stats->burst_size_stats.failure_cnt;
		data->tid = sawf_tx_stats->tid;
		data->msduq = sawf_tx_stats->msduq;
		data->tx[0][0].throughput = sawf_tx_stats->throughput;
		data->tx[0][0].ingress_rate = sawf_tx_stats->ingress_rate;
	}

	stats->feat[INX_FEAT_SAWFTX] = data;
	stats->size[INX_FEAT_SAWFTX] = sizeof(struct advance_peer_data_sawftx);

	return QDF_STATUS_SUCCESS;
}
#endif

static QDF_STATUS get_advance_peer_data_tx(struct unified_stats *stats,
					   struct cdp_peer_stats *peer_stats)
{
	struct advance_peer_data_tx *data = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct advance_peer_data_tx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_data_tx(&data->b_tx, &peer_stats->tx);
	fill_advance_data_tx_stats(&data->adv_tx, &peer_stats->tx);

	stats->feat[INX_FEAT_TX] = data;
	stats->size[INX_FEAT_TX] = sizeof(struct advance_peer_data_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_peer_data_rx(struct unified_stats *stats,
					   struct cdp_peer_stats *peer_stats)
{
	struct advance_peer_data_rx *data = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct advance_peer_data_rx));
	if (!data) {
		qdf_err("Failed Allocation");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_data_rx(&data->b_rx, &peer_stats->rx);
	fill_advance_data_rx_stats(&data->adv_rx, &peer_stats->rx);

	stats->feat[INX_FEAT_RX] = data;
	stats->size[INX_FEAT_RX] = sizeof(struct advance_peer_data_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_peer_data_link(struct unified_stats *stats,
					     struct cdp_peer_stats *peer_stats)
{
	struct advance_peer_data_link *data = NULL;
	struct cdp_rx_stats *rx = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	rx = &peer_stats->rx;
	data = qdf_mem_malloc(sizeof(struct advance_peer_data_link));
	if (!data) {
		qdf_err("Failed Allocation");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_data_link(&data->b_link, rx);

	data->rx_snr_measured_time = rx->rx_snr_measured_time;

	stats->feat[INX_FEAT_LINK] = data;
	stats->size[INX_FEAT_LINK] = sizeof(struct advance_peer_data_link);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_peer_data_rate(struct unified_stats *stats,
					     struct cdp_peer_stats *peer_stats)
{
	struct advance_peer_data_rate *data = NULL;
	struct cdp_tx_stats *tx = NULL;
	struct cdp_rx_stats *rx = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx = &peer_stats->tx;
	rx = &peer_stats->rx;
	data = qdf_mem_malloc(sizeof(struct advance_peer_data_rate));
	if (!data) {
		qdf_err("Failed Allocation");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_data_rate(&data->b_rate, peer_stats);

	data->rnd_avg_rx_rate = rx->rnd_avg_rx_rate;
	data->rnd_avg_tx_rate = tx->rnd_avg_tx_rate;
	data->avg_rx_rate = rx->avg_rx_rate;
	data->last_tx_rate_used = tx->last_tx_rate_used;
	data->avg_tx_rate = tx->avg_tx_rate;

	stats->feat[INX_FEAT_RATE] = data;
	stats->size[INX_FEAT_RATE] = sizeof(struct advance_peer_data_rate);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_peer_data_raw(struct unified_stats *stats,
					    struct cdp_peer_stats *peer_stats)
{
	struct advance_peer_data_raw *data = NULL;
	struct cdp_rx_stats *rx = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	rx = &peer_stats->rx;
	data = qdf_mem_malloc(sizeof(struct advance_peer_data_raw));
	if (!data) {
		qdf_err("Failed Allocation");
		return QDF_STATUS_E_NOMEM;
	}
	data->raw.num = rx->raw.num;
	data->raw.bytes = rx->raw.bytes;

	stats->feat[INX_FEAT_RAW] = data;
	stats->size[INX_FEAT_RAW] = sizeof(struct advance_peer_data_raw);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_peer_data_fwd(struct unified_stats *stats,
					    struct cdp_peer_stats *peer_stats)
{
	struct advance_peer_data_fwd *data = NULL;
	struct cdp_rx_stats *rx = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	rx = &peer_stats->rx;
	data = qdf_mem_malloc(sizeof(struct advance_peer_data_fwd));
	if (!data) {
		qdf_err("Failed Allocation");
		return QDF_STATUS_E_NOMEM;
	}
	data->pkts.num = rx->intra_bss.pkts.num;
	data->pkts.bytes = rx->intra_bss.pkts.bytes;
	data->fail.num = rx->intra_bss.fail.num;
	data->fail.bytes = rx->intra_bss.fail.bytes;
	data->mdns_no_fwd = rx->intra_bss.mdns_no_fwd;

	stats->feat[INX_FEAT_FWD] = data;
	stats->size[INX_FEAT_FWD] = sizeof(struct advance_peer_data_fwd);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_peer_data_twt(struct unified_stats *stats,
					    struct cdp_peer_stats *peer_stats)
{
	struct advance_peer_data_twt *data = NULL;
	struct cdp_rx_stats *rx = NULL;
	struct cdp_tx_stats *tx = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	rx = &peer_stats->rx;
	tx = &peer_stats->tx;
	data = qdf_mem_malloc(sizeof(struct advance_peer_data_twt));
	if (!data) {
		qdf_err("Failed Allocation");
		return QDF_STATUS_E_NOMEM;
	}
	data->to_stack_twt.num = rx->to_stack_twt.num;
	data->to_stack_twt.bytes = rx->to_stack_twt.bytes;
	data->tx_success_twt.num = tx->tx_success_twt.num;
	data->tx_success_twt.bytes = tx->tx_success_twt.bytes;

	stats->feat[INX_FEAT_TWT] = data;
	stats->size[INX_FEAT_TWT] = sizeof(struct advance_peer_data_twt);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_peer_data_nawds(struct unified_stats *stats,
					      struct cdp_peer_stats *peer_stats)
{
	struct advance_peer_data_nawds *data = NULL;
	struct cdp_rx_stats *rx = NULL;
	struct cdp_tx_stats *tx = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	rx = &peer_stats->rx;
	tx = &peer_stats->tx;
	data = qdf_mem_malloc(sizeof(struct advance_peer_data_nawds));
	if (!data) {
		qdf_err("Failed Allocation");
		return QDF_STATUS_E_NOMEM;
	}
	data->nawds_mcast.num = tx->nawds_mcast.num;
	data->nawds_mcast.bytes = tx->nawds_mcast.bytes;
	data->nawds_mcast_tx_drop = tx->nawds_mcast_drop;
	data->nawds_mcast_rx_drop = rx->nawds_mcast_drop;

	stats->feat[INX_FEAT_NAWDS] = data;
	stats->size[INX_FEAT_NAWDS] = sizeof(struct advance_peer_data_nawds);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_peer_ctrl_tx(struct unified_stats *stats,
					   struct peer_ic_cp_stats *cp_stats)
{
	struct advance_peer_ctrl_tx *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct advance_peer_ctrl_tx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_ctrl_tx(&ctrl->b_tx, cp_stats);

	ctrl->cs_tx_assoc = cp_stats->cs_tx_assoc;
	ctrl->cs_tx_assoc_fail = cp_stats->cs_tx_assoc_fail;

	stats->feat[INX_FEAT_TX] = ctrl;
	stats->size[INX_FEAT_TX] = sizeof(struct advance_peer_ctrl_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_peer_ctrl_rx(struct unified_stats *stats,
					   struct peer_ic_cp_stats *cp_stats)
{
	struct advance_peer_ctrl_rx *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct advance_peer_ctrl_rx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_ctrl_rx(&ctrl->b_rx, cp_stats);

	stats->feat[INX_FEAT_RX] = ctrl;
	stats->size[INX_FEAT_RX] = sizeof(struct advance_peer_ctrl_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_peer_ctrl_twt(struct unified_stats *stats,
					    struct peer_ic_cp_stats *cp_stats)
{
	struct advance_peer_ctrl_twt *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct advance_peer_ctrl_twt));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	ctrl->cs_twt_event_type = cp_stats->cs_twt_event_type;
	ctrl->cs_twt_flow_id = cp_stats->cs_twt_flow_id;
	ctrl->cs_twt_bcast = cp_stats->cs_twt_bcast;
	ctrl->cs_twt_trig = cp_stats->cs_twt_trig;
	ctrl->cs_twt_announ = cp_stats->cs_twt_announ;
	ctrl->cs_twt_dialog_id = cp_stats->cs_twt_dialog_id;
	ctrl->cs_twt_wake_dura_us = cp_stats->cs_twt_wake_dura_us;
	ctrl->cs_twt_wake_intvl_us = cp_stats->cs_twt_wake_intvl_us;
	ctrl->cs_twt_sp_offset_us = cp_stats->cs_twt_sp_offset_us;

	stats->feat[INX_FEAT_TWT] = ctrl;
	stats->size[INX_FEAT_TWT] = sizeof(struct advance_peer_ctrl_twt);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_peer_ctrl_link(struct unified_stats *stats,
					     struct peer_ic_cp_stats *cp_stats)
{
	struct advance_peer_ctrl_link *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct advance_peer_ctrl_link));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_ctrl_link(&ctrl->b_link, cp_stats);

	stats->feat[INX_FEAT_LINK] = ctrl;
	stats->size[INX_FEAT_LINK] = sizeof(struct advance_peer_ctrl_link);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_peer_ctrl_rate(struct unified_stats *stats,
					     struct peer_ic_cp_stats *cp_stats)
{
	struct advance_peer_ctrl_rate *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct advance_peer_ctrl_rate));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_ctrl_rate(&ctrl->b_rate, cp_stats);

	stats->feat[INX_FEAT_RATE] = ctrl;
	stats->size[INX_FEAT_RATE] = sizeof(struct advance_peer_ctrl_rate);

	return QDF_STATUS_SUCCESS;
}

#if CONFIG_SAWF_STATS
static bool get_advance_sawf_stats(uint32_t feat,
				   void *dp_soc,
				   struct unified_stats *stats,
				   uint8_t service_id,
				   uint8_t *peer_mac)
{
	struct sawf_delay_stats *sawf_delay = NULL;
	struct sawf_tx_stats *sawf_tx = NULL;
	bool stats_collected = false;
	uint32_t size = 0;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;

	if (feat & STATS_FEAT_FLG_SAWFDELAY) {
		if (service_id == 0)
			size = sizeof(struct sawf_delay_stats) *
				DP_SAWF_MAX_TIDS * DP_SAWF_MAX_QUEUES;
		else
			size = sizeof(struct sawf_delay_stats);
		sawf_delay = qdf_mem_malloc(size);
		if (!sawf_delay) {
			qdf_err("Allocation failed");
			ret = QDF_STATUS_E_NOMEM;
			goto get_failed;
		}
		ret = cdp_get_peer_sawf_delay_stats(dp_soc, service_id,
						    peer_mac, sawf_delay);
		if (ret == QDF_STATUS_SUCCESS)
			ret = get_advance_peer_data_sawfdelay(sawf_delay, stats,
							      service_id);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_nofl_err("Unable to fetch peer Sawf Delay Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_SAWFTX) {
		if (service_id == 0)
			size = sizeof(struct sawf_tx_stats) *
				DP_SAWF_MAX_TIDS * DP_SAWF_MAX_QUEUES;
		else
			size = sizeof(struct sawf_tx_stats);
		sawf_tx = qdf_mem_malloc(size);
		if (!sawf_tx) {
			qdf_err("Allocation failed");
			ret = QDF_STATUS_E_NOMEM;
			goto get_failed;
		}
		ret = cdp_get_peer_sawf_tx_stats(dp_soc, service_id,
						 peer_mac, sawf_tx);
		if (ret == QDF_STATUS_SUCCESS)
			ret = get_advance_peer_data_sawftx(sawf_tx, stats,
							   service_id);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_nofl_err("Unable to fetch peer Sawf Tx Stats!");
		else
			stats_collected = true;
	}

get_failed:
	if (sawf_tx)
		qdf_mem_free(sawf_tx);
	if (sawf_delay)
		qdf_mem_free(sawf_delay);

	return stats_collected;
}
#else
static bool  get_advance_sawf_stats(uint32_t feat,
				    void *dp_soc,
				    struct unified_stats *stats,
				    uint8_t service_id,
				    uint8_t *peer_mac)
{
	qdf_nofl_err("Sawf Stats collection is disabled");
	return false;
}
#endif

static QDF_STATUS get_advance_peer_data(struct wlan_objmgr_psoc *psoc,
					struct wlan_objmgr_vdev *vdev,
					uint8_t *peer_mac,
					struct unified_stats *stats,
					struct stats_config *cfg)
{
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	struct cdp_peer_stats *peer_stats = NULL;
	void *dp_soc = NULL;
	uint8_t vdev_id = 0;
	struct cdp_delay_tid_stats *delay = NULL;
	struct cdp_peer_tid_stats *jitter = NULL;
	uint32_t size;
	bool stats_collected = false;
	uint32_t feat = 0;
	uint8_t service_id = 0;

	feat = cfg->feat;
	service_id = cfg->serviceid;

	dp_soc = wlan_psoc_get_dp_handle(psoc);
	vdev_id = wlan_vdev_get_id(vdev);
	if (feat & ~(STATS_FEAT_FLG_DELAY | STATS_FEAT_FLG_JITTER)) {
		peer_stats = qdf_mem_malloc(sizeof(struct cdp_peer_stats));
		if (!peer_stats) {
			qdf_err("Failed allocation!");
			return QDF_STATUS_E_NOMEM;
		}
		ret = cdp_host_get_peer_stats_based_on_peer_type(dp_soc,
						vdev_id, peer_mac,
						peer_stats, cfg->peer_type);
		if (ret != QDF_STATUS_SUCCESS) {
			qdf_err("Unable to fetch stats!");
			goto get_failed;
		}
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_advance_peer_data_tx(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Advance TX stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_advance_peer_data_rx(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Advance RX stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_LINK) {
		ret = get_advance_peer_data_link(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Advance LINK stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RATE) {
		ret = get_advance_peer_data_rate(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Advance RATE stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RAW) {
		ret = get_advance_peer_data_raw(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Advance RAW stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_FWD) {
		ret = get_advance_peer_data_fwd(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Advance FWD stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_TWT) {
		ret = get_advance_peer_data_twt(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Advance TWT stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_NAWDS) {
		ret = get_advance_peer_data_nawds(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Advance NAWDS stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_DELAY) {
		size = sizeof(struct cdp_delay_tid_stats) * CDP_MAX_DATA_TIDS;
		delay = qdf_mem_malloc(size);
		if (!delay) {
			qdf_err("Allocation failed");
			ret = QDF_STATUS_E_NOMEM;
			goto get_failed;
		}
		ret = cdp_get_peer_delay_stats(dp_soc, vdev_id,
					       peer_mac, delay);
		if (ret == QDF_STATUS_SUCCESS)
			ret = get_advance_peer_data_delay(delay, stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_debug("Unable to fetch peer Advance Delay stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_JITTER) {
		uint8_t pdev_id = 0;
		struct wlan_objmgr_pdev *pdev = wlan_vdev_get_pdev(vdev);

		if (!pdev) {
			qdf_err("Pdev not found!");
			goto get_failed;
		}
		size = sizeof(struct cdp_peer_tid_stats) * CDP_MAX_DATA_TIDS;
		jitter = qdf_mem_malloc(size);
		if (!jitter) {
			qdf_err("Allocation failed");
			ret = QDF_STATUS_E_NOMEM;
			goto get_failed;
		}
		pdev_id = wlan_objmgr_pdev_get_pdev_id(pdev);
		ret = cdp_get_peer_jitter_stats(dp_soc, pdev_id, vdev_id,
						peer_mac, jitter);
		if (ret == QDF_STATUS_SUCCESS)
			ret = get_advance_peer_jitter_stats(jitter, stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_debug("Unable to fetch peer Advance Jitter Stats!");
		else
			stats_collected = true;
	}
	if (feat & (STATS_FEAT_FLG_SAWFDELAY | STATS_FEAT_FLG_SAWFTX)) {
		if (get_advance_sawf_stats(feat, dp_soc, stats,
					   service_id, peer_mac))
			stats_collected = true;
	}

get_failed:
	if (peer_stats)
		qdf_mem_free(peer_stats);
	if (delay)
		qdf_mem_free(delay);
	if (jitter)
		qdf_mem_free(jitter);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_advance_peer_ctrl(struct wlan_objmgr_psoc *psoc,
					uint8_t *peer_mac,
					struct unified_stats *stats,
					uint32_t feat)
{
	struct wlan_objmgr_peer *peer;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	struct peer_ic_cp_stats *peer_cp_stats = NULL;
	bool stats_collected = false;

	peer = wlan_objmgr_get_peer_by_mac(psoc, peer_mac, WLAN_MLME_SB_ID);
	if (!peer) {
		qdf_err("Peer (%s) not found!", ether_sprintf(peer_mac));
		return QDF_STATUS_E_FAILURE;
	}
	peer_cp_stats = qdf_mem_malloc(sizeof(struct peer_ic_cp_stats));
	if (!peer_cp_stats) {
		qdf_err("Allocation Failed!");
		ret = QDF_STATUS_E_NOMEM;
		goto get_failed;
	}
	ret = wlan_cfg80211_get_peer_cp_stats(peer, peer_cp_stats);
	if (QDF_STATUS_SUCCESS != ret) {
		qdf_err("Failed to get Peer Control Stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_advance_peer_ctrl_rx(stats, peer_cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Advance RX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_advance_peer_ctrl_tx(stats, peer_cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Advance TX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_TWT) {
		ret = get_advance_peer_ctrl_twt(stats, peer_cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Advance TWT Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_LINK) {
		ret = get_advance_peer_ctrl_link(stats, peer_cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Advance LINK Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RATE) {
		ret = get_advance_peer_ctrl_rate(stats, peer_cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Advance RATE Stats!");
		else
			stats_collected = true;
	}

get_failed:
	wlan_objmgr_peer_release_ref(peer, WLAN_MLME_SB_ID);
	if (peer_cp_stats)
		qdf_mem_free(peer_cp_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_advance_vdev_data_tx(struct unified_stats *stats,
					   struct cdp_vdev_stats *vdev_stats)
{
	struct advance_vdev_data_tx *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;
	uint8_t inx;
	uint8_t loop_cnt;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &vdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct advance_vdev_data_tx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_data_tx(&data->b_tx, vdev_stats);
	fill_advance_data_tx_stats(&data->adv_tx, &vdev_stats->tx);

	data->reinject_pkts.num = tx_i->reinject_pkts.num;
	data->reinject_pkts.bytes = tx_i->reinject_pkts.bytes;
	data->inspect_pkts.num = tx_i->inspect_pkts.num;
	data->inspect_pkts.bytes = tx_i->inspect_pkts.bytes;
	data->cce_classified = tx_i->cce_classified;
	data->rcvd_in_fast_xmit_flow =  tx_i->rcvd_in_fast_xmit_flow;

	loop_cnt = qdf_min((uint8_t)CDP_MAX_TX_DATA_RINGS,
			   (uint8_t)STATS_IF_MAX_TX_DATA_RINGS);
	for (inx = 0; inx < loop_cnt; inx++)
		data->rcvd_per_core[inx] = tx_i->rcvd_per_core[inx];

	stats->feat[INX_FEAT_TX] = data;
	stats->size[INX_FEAT_TX] = sizeof(struct advance_vdev_data_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_vdev_data_rx(struct unified_stats *stats,
					   struct cdp_vdev_stats *vdev_stats)
{
	struct advance_vdev_data_rx *data = NULL;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct advance_vdev_data_rx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_data_rx(&data->b_rx, &vdev_stats->rx);
	fill_advance_data_rx_stats(&data->adv_rx, &vdev_stats->rx);

	stats->feat[INX_FEAT_RX] = data;
	stats->size[INX_FEAT_RX] = sizeof(struct advance_vdev_data_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_vdev_data_me(struct unified_stats *stats,
					   struct cdp_vdev_stats *vdev_stats)
{
	struct advance_vdev_data_me *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &vdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct advance_vdev_data_me));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->mcast_pkt.num = tx_i->mcast_en.mcast_pkt.num;
	data->mcast_pkt.bytes = tx_i->mcast_en.mcast_pkt.bytes;
	data->ucast = tx_i->mcast_en.ucast;

	stats->feat[INX_FEAT_ME] = data;
	stats->size[INX_FEAT_ME] = sizeof(struct advance_vdev_data_me);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_vdev_data_raw(struct unified_stats *stats,
					    struct cdp_vdev_stats *vdev_stats)
{
	struct advance_vdev_data_raw *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;
	struct cdp_rx_stats *rx = NULL;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &vdev_stats->tx_i;
	rx = &vdev_stats->rx;
	data = qdf_mem_malloc(sizeof(struct advance_vdev_data_raw));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->rx_raw.num = rx->raw.num;
	data->rx_raw.bytes = rx->raw.bytes;
	data->tx_raw_pkt.num = tx_i->raw.raw_pkt.num;
	data->tx_raw_pkt.bytes = tx_i->raw.raw_pkt.bytes;
	data->cce_classified_raw = tx_i->cce_classified_raw;

	stats->feat[INX_FEAT_RAW] = data;
	stats->size[INX_FEAT_RAW] = sizeof(struct advance_vdev_data_raw);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_vdev_data_tso(struct unified_stats *stats,
					    struct cdp_vdev_stats *vdev_stats)
{
	struct advance_vdev_data_tso *data = NULL;
	struct cdp_tso_stats *tso = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &vdev_stats->tx_i;
	tso = &tx_i->tso_stats;
	data = qdf_mem_malloc(sizeof(struct advance_vdev_data_tso));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->sg_pkt.num = tx_i->sg.sg_pkt.num;
	data->sg_pkt.bytes = tx_i->sg.sg_pkt.bytes;
	data->non_sg_pkts.num = tx_i->sg.non_sg_pkts.num;
	data->non_sg_pkts.bytes = tx_i->sg.non_sg_pkts.bytes;
	data->num_tso_pkts.num = tso->num_tso_pkts.num;
	data->num_tso_pkts.bytes = tso->num_tso_pkts.bytes;
	data->tso_comp = tso->tso_comp;
	data->dropped_host.num = tso->dropped_host.num;
	data->dropped_host.bytes = tso->dropped_host.bytes;
	data->tso_no_mem_dropped.num = tso->tso_no_mem_dropped.num;
	data->tso_no_mem_dropped.bytes = tso->tso_no_mem_dropped.bytes;
	data->dropped_target = tso->dropped_target;

	stats->feat[INX_FEAT_TSO] = data;
	stats->size[INX_FEAT_TSO] = sizeof(struct advance_vdev_data_tso);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_vdev_data_igmp(struct unified_stats *stats,
					     struct cdp_vdev_stats *vdev_stats)
{
	struct advance_vdev_data_igmp *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &vdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct advance_vdev_data_igmp));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->igmp_rcvd = tx_i->igmp_mcast_en.igmp_rcvd;
	data->igmp_ucast_converted = tx_i->igmp_mcast_en.igmp_ucast_converted;

	stats->feat[INX_FEAT_IGMP] = data;
	stats->size[INX_FEAT_IGMP] = sizeof(struct advance_vdev_data_igmp);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_vdev_data_mesh(struct unified_stats *stats,
					     struct cdp_vdev_stats *vdev_stats)
{
	struct advance_vdev_data_mesh *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &vdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct advance_vdev_data_mesh));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->exception_fw = tx_i->mesh.exception_fw;
	data->completion_fw = tx_i->mesh.completion_fw;

	stats->feat[INX_FEAT_MESH] = data;
	stats->size[INX_FEAT_MESH] = sizeof(struct advance_vdev_data_mesh);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_vdev_data_nawds(struct unified_stats *stats,
					      struct cdp_vdev_stats *vdev_stats)
{
	struct advance_vdev_data_nawds *data = NULL;
	struct cdp_rx_stats *rx = NULL;
	struct cdp_tx_stats *tx = NULL;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	rx = &vdev_stats->rx;
	tx = &vdev_stats->tx;
	data = qdf_mem_malloc(sizeof(struct advance_vdev_data_nawds));
	if (!data) {
		qdf_err("Failed Allocation");
		return QDF_STATUS_E_NOMEM;
	}
	data->tx_nawds_mcast.num = tx->nawds_mcast.num;
	data->tx_nawds_mcast.bytes = tx->nawds_mcast.bytes;
	data->nawds_mcast_tx_drop = tx->nawds_mcast_drop;
	data->nawds_mcast_rx_drop = rx->nawds_mcast_drop;

	stats->feat[INX_FEAT_NAWDS] = data;
	stats->size[INX_FEAT_NAWDS] = sizeof(struct advance_vdev_data_nawds);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_vdev_ctrl_tx(struct unified_stats *stats,
					   struct vdev_ic_cp_stats *cp_stats)
{
	struct advance_vdev_ctrl_tx *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct advance_vdev_ctrl_tx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_ctrl_tx(&ctrl->b_tx, cp_stats);

	ctrl->cs_tx_offchan_mgmt = cp_stats->stats.cs_tx_offchan_mgmt;
	ctrl->cs_tx_offchan_data = cp_stats->stats.cs_tx_offchan_data;
	ctrl->cs_tx_offchan_fail = cp_stats->stats.cs_tx_offchan_fail;
	ctrl->cs_tx_bcn_success = cp_stats->stats.cs_tx_bcn_success;
	ctrl->cs_tx_bcn_outage = cp_stats->stats.cs_tx_bcn_outage;
	ctrl->cs_fils_frames_sent = cp_stats->stats.cs_fils_frames_sent;
	ctrl->cs_fils_frames_sent_fail =
		cp_stats->stats.cs_fils_frames_sent_fail;
	ctrl->cs_tx_offload_prb_resp_succ_cnt =
		cp_stats->stats.cs_tx_offload_prb_resp_succ_cnt;
	ctrl->cs_tx_offload_prb_resp_fail_cnt =
		cp_stats->stats.cs_tx_offload_prb_resp_fail_cnt;

	stats->feat[INX_FEAT_TX] = ctrl;
	stats->size[INX_FEAT_TX] = sizeof(struct advance_vdev_ctrl_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_vdev_ctrl_rx(struct unified_stats *stats,
					   struct vdev_ic_cp_stats *cp_stats)
{
	struct advance_vdev_ctrl_rx *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct advance_vdev_ctrl_rx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_ctrl_rx(&ctrl->b_rx, cp_stats);

	ctrl->cs_rx_action = cp_stats->stats.cs_rx_action;
	ctrl->cs_mlme_auth_attempt = cp_stats->stats.cs_mlme_auth_attempt;
	ctrl->cs_mlme_auth_success = cp_stats->stats.cs_mlme_auth_success;
	ctrl->cs_authorize_attempt = cp_stats->stats.cs_authorize_attempt;
	ctrl->cs_authorize_success = cp_stats->stats.cs_authorize_success;
	ctrl->cs_prob_req_drops = cp_stats->stats.cs_prob_req_drops;
	ctrl->cs_oob_probe_req_count = cp_stats->stats.cs_oob_probe_req_count;
	ctrl->cs_wc_probe_req_drops = cp_stats->stats.cs_wc_probe_req_drops;
	ctrl->cs_sta_xceed_rlim = cp_stats->stats.cs_sta_xceed_rlim;
	ctrl->cs_sta_xceed_vlim = cp_stats->stats.cs_sta_xceed_vlim;

	stats->feat[INX_FEAT_RX] = ctrl;
	stats->size[INX_FEAT_RX] = sizeof(struct advance_vdev_ctrl_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_vdev_ctrl_link(struct unified_stats *stats,
					     struct wlan_objmgr_vdev *vdev)
{
	struct advance_vdev_ctrl_link *ctrl;

	if (!stats || !vdev) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct advance_vdev_ctrl_link));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_ctrl_link(&ctrl->b_link, vdev);

	stats->feat[INX_FEAT_LINK] = ctrl;
	stats->size[INX_FEAT_LINK] = sizeof(struct advance_vdev_ctrl_link);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_vdev_data(struct wlan_objmgr_psoc *psoc,
					struct wlan_objmgr_vdev *vdev,
					struct unified_stats *stats,
					uint32_t feat, bool mld_req,
					bool mld_link)
{
	struct cdp_vdev_stats *vdev_stats = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	bool stats_collected = false;

	if (!psoc || !vdev) {
		qdf_err("Invalid psoc or vdev!");
		return QDF_STATUS_E_INVAL;
	}
	vdev_stats = qdf_mem_malloc(sizeof(struct cdp_vdev_stats));
	if (!vdev_stats) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}

#ifdef CONFIG_MLO_SINGLE_DEV
	if (mld_link)
		ret = cdp_mlo_get_mld_vdev_stats(wlan_psoc_get_dp_handle(psoc),
						 wlan_vdev_get_id(vdev),
						 vdev_stats, true);
	else if (mld_req)
		ret = cdp_mlo_get_mld_vdev_stats(wlan_psoc_get_dp_handle(psoc),
						 wlan_vdev_get_id(vdev),
						 vdev_stats, false);
	else
#endif
		ret = cdp_host_get_interface_stats(
						wlan_psoc_get_dp_handle(psoc),
						wlan_vdev_get_id(vdev),
						vdev_stats);

	if (ret != QDF_STATUS_SUCCESS) {
		qdf_err("Unable to get Vdev Stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_advance_vdev_data_tx(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Advance TX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_advance_vdev_data_rx(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Advance RX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_ME) {
		ret = get_advance_vdev_data_me(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Advance ME Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RAW) {
		ret = get_advance_vdev_data_raw(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Advance RAW Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_TSO) {
		ret = get_advance_vdev_data_tso(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Advance TSO Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_IGMP) {
		ret = get_advance_vdev_data_igmp(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Advance IGMP Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_MESH) {
		ret = get_advance_vdev_data_mesh(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Advance MESH Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_NAWDS) {
		ret = get_advance_vdev_data_nawds(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Advance NAWDS Stats!");
		else
			stats_collected = true;
	}

get_failed:
	qdf_mem_free(vdev_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_advance_vdev_ctrl(struct wlan_objmgr_vdev *vdev,
					struct unified_stats *stats,
					uint32_t feat, bool mld_req)
{
	struct vdev_ic_cp_stats *cp_stats = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	bool stats_collected = false;

	if (!vdev) {
		qdf_err("Invalid vdev!");
		return QDF_STATUS_E_INVAL;
	}
	cp_stats = qdf_mem_malloc(sizeof(struct vdev_ic_cp_stats));
	if (!cp_stats) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}

#ifdef CONFIG_MLO_SINGLE_DEV
	if (mld_req) {
		uint16_t num_links = 0, i = 0;
		QDF_STATUS status = QDF_STATUS_SUCCESS;
		struct vdev_ic_cp_stats *temp_cp_stats = NULL;
		struct wlan_objmgr_vdev *vdev_list[WLAN_UMAC_MLO_MAX_VDEVS] = {NULL};

		temp_cp_stats = qdf_mem_malloc(sizeof(struct vdev_ic_cp_stats));
		if (!temp_cp_stats) {
			qdf_err("Allocation Failed!");
			qdf_mem_free(cp_stats);
			return QDF_STATUS_E_NOMEM;
		}

		mlo_ap_get_vdev_list(vdev, &num_links, vdev_list);
		for (i = 0; i < num_links; i++) {
			status = get_vdev_cp_stats(vdev_list[i], temp_cp_stats);
			mlo_release_vdev_ref(vdev_list[i]);
			if (status != QDF_STATUS_SUCCESS) {
				ret = status;
				continue;
			}
			aggregate_mld_vdev_stats(cp_stats, temp_cp_stats);
		}
		qdf_mem_free(temp_cp_stats);
	} else
#endif
	{
		ret = get_vdev_cp_stats(vdev, cp_stats);
	}

	if (QDF_STATUS_SUCCESS != ret) {
		qdf_err("Unable to get Vdev Control stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_advance_vdev_ctrl_tx(stats, cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Advance TX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_advance_vdev_ctrl_rx(stats, cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Advance RX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_LINK) {
		ret = get_advance_vdev_ctrl_link(stats, vdev);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Advance LINK Stats!");
		else
			stats_collected = true;
	}

get_failed:
	qdf_mem_free(cp_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_advance_pdev_data_tx(struct unified_stats *stats,
					   struct cdp_pdev_stats *pdev_stats)
{
	uint8_t inx;
	uint8_t loop_cnt;
	struct advance_pdev_data_tx *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;
	struct cdp_hist_tx_comp *hist = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &pdev_stats->tx_i;
	hist = &pdev_stats->tx_comp_histogram;
	data = qdf_mem_malloc(sizeof(struct advance_pdev_data_tx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_data_tx(&data->b_tx, pdev_stats);
	fill_advance_data_tx_stats(&data->adv_tx, &pdev_stats->tx);

	data->reinject_pkts.num = tx_i->reinject_pkts.num;
	data->reinject_pkts.bytes = tx_i->reinject_pkts.bytes;
	data->inspect_pkts.num = tx_i->inspect_pkts.num;
	data->inspect_pkts.bytes = tx_i->inspect_pkts.bytes;
	data->cce_classified = tx_i->cce_classified;
	data->rcvd_in_fast_xmit_flow = tx_i->rcvd_in_fast_xmit_flow;
	data->sniffer_rcvd.num = tx_i->sniffer_rcvd.num;
	data->sniffer_rcvd.bytes = tx_i->sniffer_rcvd.bytes;
	data->tx_hist.pkts_1 = hist->pkts_1;
	data->tx_hist.pkts_2_20 = hist->pkts_2_20;
	data->tx_hist.pkts_21_40 = hist->pkts_21_40;
	data->tx_hist.pkts_41_60 = hist->pkts_41_60;
	data->tx_hist.pkts_61_80 = hist->pkts_61_80;
	data->tx_hist.pkts_81_100 = hist->pkts_81_100;
	data->tx_hist.pkts_101_200 = hist->pkts_101_200;
	data->tx_hist.pkts_201_plus = hist->pkts_201_plus;

	loop_cnt = qdf_min((uint8_t)CDP_MAX_TX_DATA_RINGS,
			   (uint8_t)STATS_IF_MAX_TX_DATA_RINGS);
	for (inx = 0; inx < loop_cnt; inx++)
		data->rcvd_per_core[inx] = tx_i->rcvd_per_core[inx];

	stats->feat[INX_FEAT_TX] = data;
	stats->size[INX_FEAT_TX] = sizeof(struct advance_pdev_data_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_pdev_data_rx(struct unified_stats *stats,
					   struct cdp_pdev_stats *pdev_stats)
{
	struct advance_pdev_data_rx *data = NULL;
	struct cdp_hist_rx_ind *hist = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	hist = &pdev_stats->rx_ind_histogram;
	data = qdf_mem_malloc(sizeof(struct advance_pdev_data_rx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_data_rx(&data->b_rx, pdev_stats);
	fill_advance_data_rx_stats(&data->adv_rx, &pdev_stats->rx);

	data->rx_hist.pkts_1 = hist->pkts_1;
	data->rx_hist.pkts_2_20 = hist->pkts_2_20;
	data->rx_hist.pkts_21_40 = hist->pkts_21_40;
	data->rx_hist.pkts_41_60 = hist->pkts_41_60;
	data->rx_hist.pkts_61_80 = hist->pkts_61_80;
	data->rx_hist.pkts_81_100 = hist->pkts_81_100;
	data->rx_hist.pkts_101_200 = hist->pkts_101_200;
	data->rx_hist.pkts_201_plus = hist->pkts_201_plus;

	stats->feat[INX_FEAT_RX] = data;
	stats->size[INX_FEAT_RX] = sizeof(struct advance_pdev_data_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_pdev_data_me(struct unified_stats *stats,
					   struct cdp_pdev_stats *pdev_stats)
{
	struct advance_pdev_data_me *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &pdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct advance_pdev_data_me));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->mcast_pkt.num = tx_i->mcast_en.mcast_pkt.num;
	data->mcast_pkt.bytes = tx_i->mcast_en.mcast_pkt.bytes;
	data->ucast = tx_i->mcast_en.ucast;

	stats->feat[INX_FEAT_ME] = data;
	stats->size[INX_FEAT_ME] = sizeof(struct advance_pdev_data_me);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_pdev_data_raw(struct unified_stats *stats,
					    struct cdp_pdev_stats *pdev_stats)
{
	struct advance_pdev_data_raw *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;
	struct cdp_rx_stats *rx = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &pdev_stats->tx_i;
	rx = &pdev_stats->rx;
	data = qdf_mem_malloc(sizeof(struct advance_pdev_data_raw));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->rx_raw.num = rx->raw.num;
	data->rx_raw.bytes = rx->raw.bytes;
	data->tx_raw_pkt.num = tx_i->raw.raw_pkt.num;
	data->tx_raw_pkt.bytes = tx_i->raw.raw_pkt.bytes;
	data->cce_classified_raw = tx_i->cce_classified_raw;
	data->rx_raw_pkts = pdev_stats->rx_raw_pkts;

	stats->feat[INX_FEAT_RAW] = data;
	stats->size[INX_FEAT_RAW] = sizeof(struct advance_pdev_data_raw);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_pdev_data_tso(struct unified_stats *stats,
					    struct cdp_pdev_stats *pdev_stats)
{
	struct advance_pdev_data_tso *data = NULL;
	struct cdp_tso_stats *tso = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tso = &pdev_stats->tso_stats;
	tx_i = &pdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct advance_pdev_data_tso));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->sg_pkt.num = tx_i->sg.sg_pkt.num;
	data->sg_pkt.bytes = tx_i->sg.sg_pkt.bytes;
	data->non_sg_pkts.num = tx_i->sg.non_sg_pkts.num;
	data->non_sg_pkts.bytes = tx_i->sg.non_sg_pkts.bytes;
	data->dropped_host.num = tx_i->sg.dropped_host.num;
	data->dropped_host.bytes = tx_i->sg.dropped_host.bytes;
	data->dropped_target = tx_i->sg.dropped_target;
	data->dma_map_error = tx_i->sg.dma_map_error;
	data->num_tso_pkts.num = tso->num_tso_pkts.num;
	data->num_tso_pkts.bytes = tso->num_tso_pkts.bytes;
	data->tso_comp = tso->tso_comp;
#if FEATURE_TSO_STATS
	data->segs_1 = tso->seg_histogram.segs_1;
	data->segs_2_5 = tso->seg_histogram.segs_2_5;
	data->segs_6_10 = tso->seg_histogram.segs_6_10;
	data->segs_11_15 = tso->seg_histogram.segs_11_15;
	data->segs_16_20 = tso->seg_histogram.segs_16_20;
	data->segs_20_plus = tso->seg_histogram.segs_20_plus;
	data->tso_desc_cnt = pdev_stats->tso_desc_cnt;
#endif /* FEATURE_TSO_STATS */

	stats->feat[INX_FEAT_TSO] = data;
	stats->size[INX_FEAT_TSO] = sizeof(struct advance_pdev_data_tso);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_pdev_data_vow(struct unified_stats *stats,
					    struct cdp_tid_stats_intf *vow)
{
	uint8_t tidx;
	uint8_t tids_count;
	uint8_t bk_max;
	uint8_t tx_drop;
	uint8_t rx_drop;
	uint8_t tqm_status;
	uint8_t htt_status;
	uint8_t reo_code;
	uint8_t dma_code;
	struct advance_pdev_data_vow *data;
	struct stats_if_tid_tx_stats *tx_stats;
	struct stats_if_tid_rx_stats *rx_stats;
	struct cdp_tid_tx_stats *tx_total;
	struct cdp_tid_rx_stats *rx_total;

	if (!stats || !vow) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct advance_pdev_data_vow));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}

	tids_count = qdf_min((uint8_t)STATS_IF_MAX_DATA_TIDS,
			     (uint8_t)CDP_MAX_DATA_TIDS);
	bk_max = qdf_min((uint8_t)STATS_IF_DELAY_BUCKET_MAX,
			 (uint8_t)CDP_DELAY_BUCKET_MAX);
	tqm_status = qdf_min((uint8_t)STATS_IF_MAX_TX_TQM_STATUS,
			     (uint8_t)CDP_MAX_TX_TQM_STATUS);
	htt_status = qdf_min((uint8_t)STATS_IF_MAX_TX_HTT_STATUS,
			     (uint8_t)CDP_MAX_TX_HTT_STATUS);
	reo_code = qdf_min((uint8_t)STATS_IF_REO_ERR_MAX,
			   (uint8_t)CDP_REO_CODE_MAX);
	dma_code = qdf_min((uint8_t)STATS_IF_RXDMA_ERR_MAX,
			   (uint8_t)CDP_DMA_CODE_MAX);
	tx_drop = qdf_min((uint8_t)TX_MAX_DROP,
			  (uint8_t)STATS_IF_TX_MAX_DROP);
	rx_drop = qdf_min((uint8_t)RX_MAX_DROP,
			  (uint8_t)STATS_IF_RX_MAX_DROP);

	data->ingress_stack = vow->ingress_stack;
	data->osif_drop = vow->osif_drop;
	for (tidx = 0; tidx < tids_count; tidx++) {
		tx_stats = &data->tx_stats[tidx];
		rx_stats = &data->rx_stats[tidx];
		tx_total = &vow->tx_total[tidx];
		rx_total = &vow->rx_total[tidx];

		tx_stats->swq_delay.min_delay = tx_total->swq_delay.min_delay;
		tx_stats->swq_delay.max_delay = tx_total->swq_delay.max_delay;
		tx_stats->swq_delay.avg_delay = tx_total->swq_delay.avg_delay;
		qdf_mem_copy(&tx_stats->swq_delay.delay_bucket,
			     &tx_total->swq_delay.delay_bucket,
			     sizeof(tx_stats->swq_delay.delay_bucket[0]) *
			     bk_max);

		tx_stats->hwtx_delay.min_delay = tx_total->hwtx_delay.min_delay;
		tx_stats->hwtx_delay.max_delay = tx_total->hwtx_delay.max_delay;
		tx_stats->hwtx_delay.avg_delay = tx_total->hwtx_delay.avg_delay;
		qdf_mem_copy(&tx_stats->hwtx_delay.delay_bucket,
			     &tx_total->hwtx_delay.delay_bucket,
			     sizeof(tx_stats->hwtx_delay.delay_bucket[0]) *
			     bk_max);

		tx_stats->intfrm_delay.min_delay =
			tx_total->intfrm_delay.min_delay;
		tx_stats->intfrm_delay.max_delay =
			tx_total->intfrm_delay.max_delay;
		tx_stats->intfrm_delay.avg_delay =
			tx_total->intfrm_delay.avg_delay;
		qdf_mem_copy(&tx_stats->intfrm_delay.delay_bucket,
			     &tx_total->intfrm_delay.delay_bucket,
			     sizeof(tx_stats->intfrm_delay.delay_bucket[0]) *
			     bk_max);

		tx_stats->success_cnt = tx_total->success_cnt;
		tx_stats->comp_fail_cnt = tx_total->comp_fail_cnt;
		qdf_mem_copy(&tx_stats->swdrop_cnt, &tx_total->swdrop_cnt,
			     sizeof(tx_stats->swdrop_cnt[0]) * tx_drop);

		qdf_mem_copy(&tx_stats->tqm_status_cnt,
			     &tx_total->tqm_status_cnt,
			     sizeof(tx_stats->tqm_status_cnt[0]) * tqm_status);
		qdf_mem_copy(&tx_stats->htt_status_cnt,
			     &tx_total->htt_status_cnt,
			     sizeof(tx_stats->htt_status_cnt[0]) * htt_status);

		rx_stats->delivered_to_stack = rx_total->delivered_to_stack;
		rx_stats->intrabss_cnt = rx_total->intrabss_cnt;
		rx_stats->msdu_cnt = rx_total->msdu_cnt;
		rx_stats->mcast_msdu_cnt = rx_total->mcast_msdu_cnt;
		rx_stats->bcast_msdu_cnt = rx_total->bcast_msdu_cnt;
		qdf_mem_copy(&rx_stats->fail_cnt, &rx_total->fail_cnt,
			     sizeof(rx_stats->fail_cnt[0]) * rx_drop);

		rx_stats->to_stack_delay.min_delay =
			rx_total->to_stack_delay.min_delay;
		rx_stats->to_stack_delay.max_delay =
			rx_total->to_stack_delay.max_delay;
		rx_stats->to_stack_delay.avg_delay =
			rx_total->to_stack_delay.avg_delay;
		qdf_mem_copy(&rx_stats->to_stack_delay.delay_bucket,
			     &rx_total->to_stack_delay.delay_bucket,
			     sizeof(rx_stats->to_stack_delay.delay_bucket[0]) *
			     bk_max);

		rx_stats->intfrm_delay.min_delay =
			rx_total->intfrm_delay.min_delay;
		rx_stats->intfrm_delay.max_delay =
			rx_total->intfrm_delay.max_delay;
		rx_stats->intfrm_delay.avg_delay =
			rx_total->intfrm_delay.avg_delay;
		qdf_mem_copy(&rx_stats->intfrm_delay.delay_bucket,
			     &rx_total->intfrm_delay.delay_bucket,
			     sizeof(rx_stats->intfrm_delay.delay_bucket[0]) *
			     bk_max);

		rx_stats->reo_err.err_src_reo_code_inv =
			rx_total->reo_err.err_src_reo_code_inv;
		qdf_mem_copy(&rx_stats->reo_err.err_reo_codes,
			     &rx_total->reo_err.err_reo_codes,
			     sizeof(rx_stats->reo_err.err_reo_codes[0]) *
			     reo_code);

		rx_stats->rxdma_err.err_src_rxdma_code_inv =
			rx_total->rxdma_err.err_src_rxdma_code_inv;
		qdf_mem_copy(&rx_stats->rxdma_err.err_dma_codes,
			     &rx_total->rxdma_err.err_dma_codes,
			     sizeof(rx_stats->rxdma_err.err_dma_codes[0]) *
			     dma_code);
	}

	stats->feat[INX_FEAT_VOW] = data;
	stats->size[INX_FEAT_VOW] = sizeof(struct advance_pdev_data_vow);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_pdev_data_igmp(struct unified_stats *stats,
					     struct cdp_pdev_stats *pdev_stats)
{
	struct advance_pdev_data_igmp *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &pdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct advance_pdev_data_igmp));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->igmp_rcvd = tx_i->igmp_mcast_en.igmp_rcvd;
	data->igmp_ucast_converted = tx_i->igmp_mcast_en.igmp_ucast_converted;

	stats->feat[INX_FEAT_IGMP] = data;
	stats->size[INX_FEAT_IGMP] = sizeof(struct advance_pdev_data_igmp);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_pdev_data_mesh(struct unified_stats *stats,
					     struct cdp_pdev_stats *pdev_stats)
{
	struct advance_pdev_data_mesh *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &pdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct advance_pdev_data_mesh));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->exception_fw = tx_i->mesh.exception_fw;
	data->completion_fw = tx_i->mesh.completion_fw;

	stats->feat[INX_FEAT_MESH] = data;
	stats->size[INX_FEAT_MESH] = sizeof(struct advance_pdev_data_mesh);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_pdev_data_nawds(struct unified_stats *stats,
					      struct cdp_pdev_stats *pdev_stats)
{
	struct advance_pdev_data_nawds *data = NULL;
	struct cdp_rx_stats *rx = NULL;
	struct cdp_tx_stats *tx = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	rx = &pdev_stats->rx;
	tx = &pdev_stats->tx;
	data = qdf_mem_malloc(sizeof(struct advance_pdev_data_nawds));
	if (!data) {
		qdf_err("Failed Allocation");
		return QDF_STATUS_E_NOMEM;
	}
	data->tx_nawds_mcast.num = tx->nawds_mcast.num;
	data->tx_nawds_mcast.bytes = tx->nawds_mcast.bytes;
	data->nawds_mcast_tx_drop = tx->nawds_mcast_drop;
	data->nawds_mcast_rx_drop = rx->nawds_mcast_drop;

	stats->feat[INX_FEAT_NAWDS] = data;
	stats->size[INX_FEAT_NAWDS] = sizeof(struct advance_pdev_data_nawds);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_pdev_ctrl_tx(struct unified_stats *stats,
					   struct pdev_ic_cp_stats *cp_stats)
{
	struct advance_pdev_ctrl_tx *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct advance_pdev_ctrl_tx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_ctrl_tx(&ctrl->b_tx, cp_stats);

	ctrl->cs_tx_beacon = cp_stats->stats.cs_tx_beacon;

	stats->feat[INX_FEAT_TX] = ctrl;
	stats->size[INX_FEAT_TX] = sizeof(struct advance_pdev_ctrl_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_pdev_ctrl_rx(struct unified_stats *stats,
					   struct pdev_ic_cp_stats *cp_stats)
{
	struct advance_pdev_ctrl_rx *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct advance_pdev_ctrl_rx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_ctrl_rx(&ctrl->b_rx, cp_stats);

	ctrl->cs_rx_mgmt_rssi_drop = cp_stats->stats.cs_rx_mgmt_rssi_drop;

	stats->feat[INX_FEAT_RX] = ctrl;
	stats->size[INX_FEAT_RX] = sizeof(struct advance_pdev_ctrl_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_pdev_ctrl_link(struct unified_stats *stats,
					     struct pdev_ic_cp_stats *cp_stats,
					     struct wlan_objmgr_pdev *pdev)
{
	struct advance_pdev_ctrl_link *ctrl = NULL;

	if (!stats || !cp_stats || !pdev) {
		qdf_err("Invalid Input");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct advance_pdev_ctrl_link));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_ctrl_link(&ctrl->b_link, cp_stats, pdev);

	ctrl->dcs_total_util = cp_stats->stats.chan_stats.dcs_total_util;
	ctrl->dcs_ap_tx_util = cp_stats->stats.chan_stats.dcs_ap_tx_util;
	ctrl->dcs_ap_rx_util = cp_stats->stats.chan_stats.dcs_ap_rx_util;
	ctrl->dcs_self_bss_util = cp_stats->stats.chan_stats.dcs_self_bss_util;
	ctrl->dcs_obss_util = cp_stats->stats.chan_stats.dcs_obss_util;
	ctrl->dcs_obss_rx_util = cp_stats->stats.chan_stats.dcs_obss_rx_util;
	ctrl->dcs_free_medium = cp_stats->stats.chan_stats.dcs_free_medium;
	ctrl->dcs_non_wifi_util = cp_stats->stats.chan_stats.dcs_non_wifi_util;
	ctrl->dcs_ss_under_util = cp_stats->stats.chan_stats.dcs_ss_under_util;

	stats->feat[INX_FEAT_LINK] = ctrl;
	stats->size[INX_FEAT_LINK] = sizeof(struct advance_pdev_ctrl_link);

	return QDF_STATUS_SUCCESS;
}

static void aggregate_advance_pdev_ctrl_tx(struct advance_pdev_ctrl_tx *tx,
					   struct vdev_ic_cp_stats *cp_stats)
{
	aggregate_basic_pdev_ctrl_tx(&tx->b_tx, cp_stats);
	tx->cs_tx_beacon += cp_stats->stats.cs_tx_bcn_success +
			    cp_stats->stats.cs_tx_bcn_outage;
}

static void aggregate_advance_pdev_ctrl_rx(struct advance_pdev_ctrl_rx *rx,
					   struct vdev_ic_cp_stats *cp_stats)
{
	aggregate_basic_pdev_ctrl_rx(&rx->b_rx, cp_stats);
}

static void aggr_advance_pdev_ctrl_stats(struct wlan_objmgr_pdev *pdev,
					 void *object, void *arg)
{
	struct wlan_objmgr_vdev *vdev = object;
	struct iterator_ctx *ctx = arg;
	struct unified_stats *stats;
	struct vdev_ic_cp_stats *cp_stats;

	if (!vdev || !ctx || !ctx->pvt || !ctx->stats)
		return;
	if (wlan_vdev_mlme_get_opmode(vdev) != QDF_SAP_MODE)
		return;
	cp_stats = ctx->pvt;
	stats = ctx->stats;
	qdf_mem_zero(cp_stats, sizeof(struct vdev_ic_cp_stats));
	get_vdev_cp_stats(vdev, cp_stats);

	if (stats->feat[INX_FEAT_TX])
		aggregate_advance_pdev_ctrl_tx(stats->feat[INX_FEAT_TX],
					       cp_stats);
	if (stats->feat[INX_FEAT_RX])
		aggregate_advance_pdev_ctrl_rx(stats->feat[INX_FEAT_RX],
					       cp_stats);
}

static void aggregate_advance_pdev_stats(struct wlan_objmgr_pdev *pdev,
					 struct unified_stats *stats,
					 enum stats_type_e type)
{
	struct iterator_ctx ctx = {0};
	struct vdev_ic_cp_stats *cp_stats = NULL;

	switch (type) {
	case STATS_TYPE_DATA:
		break;
	case STATS_TYPE_CTRL:
		cp_stats = qdf_mem_malloc(sizeof(struct vdev_ic_cp_stats));
		if (!cp_stats) {
			qdf_debug("Allocation Failed!");
			return;
		}
		ctx.pvt = cp_stats;
		ctx.stats = stats;
		wlan_objmgr_pdev_iterate_obj_list(pdev, WLAN_VDEV_OP,
						  aggr_advance_pdev_ctrl_stats,
						  &ctx, 1, WLAN_MLME_SB_ID);
		break;
	default:
		qdf_err("Invalid type %d!", type);
	}

	if (cp_stats)
		qdf_mem_free(cp_stats);
}

static QDF_STATUS get_advance_pdev_data(struct wlan_objmgr_psoc *psoc,
					struct wlan_objmgr_pdev *pdev,
					struct unified_stats *stats,
					uint32_t feat)
{
	struct cdp_pdev_stats *pdev_stats = NULL;
	struct cdp_tid_stats_intf *vow = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	bool stats_collected = false;
	void *dp_soc = NULL;
	uint8_t pdev_id = 0;
	uint32_t size;

	if (!psoc || !pdev) {
		qdf_err("Invalid pdev and psoc!");
		return QDF_STATUS_E_INVAL;
	}
	pdev_stats = qdf_mem_malloc(sizeof(struct cdp_pdev_stats));
	if (!pdev_stats) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}

	dp_soc = wlan_psoc_get_dp_handle(psoc);
	pdev_id = wlan_objmgr_pdev_get_pdev_id(pdev);

	ret = cdp_host_get_pdev_stats(dp_soc, pdev_id, pdev_stats);
	if (ret != QDF_STATUS_SUCCESS) {
		qdf_err("Unable to get Pdev stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_advance_pdev_data_tx(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Advance TX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_advance_pdev_data_rx(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Advance RX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_ME) {
		ret = get_advance_pdev_data_me(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Advance ME Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RAW) {
		ret = get_advance_pdev_data_raw(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Advance RAW Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_TSO) {
		ret = get_advance_pdev_data_tso(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Advance TSO Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_VOW) {
		size = sizeof(struct cdp_tid_stats_intf);
		vow = qdf_mem_malloc(size);
		if (!vow) {
			qdf_err("Allocation failed");
			ret = QDF_STATUS_E_NOMEM;
			goto get_failed;
		}
		ret = cdp_get_pdev_tid_stats(dp_soc, pdev_id, vow);
		if (ret == QDF_STATUS_SUCCESS)
			ret = get_advance_pdev_data_vow(stats, vow);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Advance VOW Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_IGMP) {
		ret = get_advance_pdev_data_igmp(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Advance IGMP Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_MESH) {
		ret = get_advance_pdev_data_mesh(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Advance MESH Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_NAWDS) {
		ret = get_advance_pdev_data_nawds(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Advance NAWDS Stats!");
		else
			stats_collected = true;
	}

get_failed:
	qdf_mem_free(pdev_stats);
	if (vow)
		qdf_mem_free(vow);

	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_advance_pdev_ctrl(struct wlan_objmgr_pdev *pdev,
					struct unified_stats *stats,
					uint32_t feat, bool aggregate)
{
	struct pdev_ic_cp_stats *pdev_cp_stats = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	bool stats_collected = false;
	bool aggr_required = false;

	if (!pdev) {
		qdf_err("Invalid pdev!");
		return QDF_STATUS_E_INVAL;
	}
	pdev_cp_stats = qdf_mem_malloc(sizeof(struct pdev_ic_cp_stats));
	if (!pdev_cp_stats) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	ret = wlan_cfg80211_get_pdev_cp_stats(pdev, pdev_cp_stats);
	if (QDF_STATUS_SUCCESS != ret) {
		qdf_err("Unbale to get pdev control stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_advance_pdev_ctrl_tx(stats, pdev_cp_stats);
		if (ret != QDF_STATUS_SUCCESS) {
			qdf_err("Unable to fetch pdev Advance TX Stats!");
		} else {
			stats_collected = true;
			aggr_required = aggregate;
		}
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_advance_pdev_ctrl_rx(stats, pdev_cp_stats);
		if (ret != QDF_STATUS_SUCCESS) {
			qdf_err("Unable to fetch pdev Advance RX Stats!");
		} else {
			stats_collected = true;
			aggr_required = aggregate;
		}
	}
	if (feat & STATS_FEAT_FLG_LINK) {
		ret = get_advance_pdev_ctrl_link(stats, pdev_cp_stats, pdev);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Advance LINK Stats!");
		else
			stats_collected = true;
	}

	if (aggr_required)
		aggregate_advance_pdev_stats(pdev, stats, STATS_TYPE_CTRL);

get_failed:
	qdf_mem_free(pdev_cp_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_advance_psoc_data_tx(struct unified_stats *stats,
					   struct cdp_soc_stats *soc_stats)
{
	struct advance_psoc_data_tx *data = NULL;

	if (!stats || !soc_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct advance_psoc_data_tx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_psoc_data_tx(&data->b_tx, soc_stats);

	stats->feat[INX_FEAT_TX] = data;
	stats->size[INX_FEAT_TX] = sizeof(struct advance_psoc_data_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_psoc_data_rx(struct unified_stats *stats,
					   struct cdp_soc_stats *soc_stats)
{
	struct advance_psoc_data_rx *data = NULL;

	if (!stats || !soc_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct advance_psoc_data_rx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_psoc_data_rx(&data->b_rx, soc_stats);

	data->err_ring_pkts = soc_stats->rx.err_ring_pkts;
	data->rx_frags = soc_stats->rx.rx_frags;
	data->rx_hw_reinject = soc_stats->rx.rx_hw_reinject;
	data->bar_frame = soc_stats->rx.bar_frame;
	data->rejected = soc_stats->rx.err.rx_rejected;
	data->raw_frm_drop = soc_stats->rx.err.rx_raw_frm_drop;

	stats->feat[INX_FEAT_RX] = data;
	stats->size[INX_FEAT_RX] = sizeof(struct advance_psoc_data_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_advance_psoc_data(struct wlan_objmgr_psoc *psoc,
					struct unified_stats *stats,
					uint32_t feat)
{
	struct cdp_soc_stats *psoc_stats = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	bool stats_collected = false;

	if (!psoc) {
		qdf_err("Invalid psoc!");
		return QDF_STATUS_E_INVAL;
	}
	psoc_stats = qdf_mem_malloc(sizeof(struct cdp_soc_stats));
	if (!psoc_stats) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	ret = cdp_host_get_soc_stats(wlan_psoc_get_dp_handle(psoc), psoc_stats);
	if (QDF_STATUS_SUCCESS != ret) {
		qdf_err("Unable to get Psoc stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_advance_psoc_data_tx(stats, psoc_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch psoc Advance TX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_advance_psoc_data_rx(stats, psoc_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch psoc Advance RX Stats!");
		else
			stats_collected = true;
	}

get_failed:
	qdf_mem_free(psoc_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}
#endif /* WLAN_ADVANCE_TELEMETRY */

#if WLAN_DEBUG_TELEMETRY
#ifdef VDEV_PEER_PROTOCOL_COUNT
static void fill_tx_protocol_trace(struct debug_data_tx_stats *tx,
				   struct cdp_tx_stats *cdp_tx)
{
	uint8_t inx, max;

	max = qdf_min((uint8_t)CDP_TRACE_MAX, (uint8_t)STATS_IF_TRACE_MAX);
	for (inx = 0; inx < max; inx++) {
		tx->protocol_trace_cnt[inx].egress_cnt =
				cdp_tx->protocol_trace_cnt[inx].egress_cnt;
		tx->protocol_trace_cnt[inx].ingress_cnt =
				cdp_tx->protocol_trace_cnt[inx].ingress_cnt;
	}
}

static void fill_rx_protocol_trace(struct debug_data_rx_stats *rx,
				   struct cdp_rx_stats *cdp_rx)
{
	uint8_t inx, max;

	max = qdf_min((uint8_t)CDP_TRACE_MAX, (uint8_t)STATS_IF_TRACE_MAX);
	for (inx = 0; inx < max; inx++) {
		rx->protocol_trace_cnt[inx].egress_cnt =
				cdp_rx->protocol_trace_cnt[inx].egress_cnt;
		rx->protocol_trace_cnt[inx].ingress_cnt =
				cdp_rx->protocol_trace_cnt[inx].ingress_cnt;
	}
}
#else
static void fill_tx_protocol_trace(struct debug_data_tx_stats *tx,
				   struct cdp_tx_stats *cdp_tx)
{
}

static void fill_rx_protocol_trace(struct debug_data_rx_stats *rx,
				   struct cdp_rx_stats *cdp_rx)
{
}
#endif /* VDEV_PEER_PROTOCOL_COUNT */

static void fill_debug_data_tx_stats(struct debug_data_tx_stats *tx,
				     struct cdp_tx_stats *cdp_tx)
{
	uint8_t inx = 0;
	uint8_t loop_cnt, mcs_cnt;

	tx->inactive_time = cdp_tx->inactive_time;
	tx->ofdma = cdp_tx->ofdma;
	tx->stbc = cdp_tx->stbc;
	tx->ldpc = cdp_tx->ldpc;
	tx->pream_punct_cnt = cdp_tx->pream_punct_cnt;
	tx->num_ppdu_cookie_valid = cdp_tx->num_ppdu_cookie_valid;
	tx->fw_rem_notx = cdp_tx->dropped.fw_rem_notx;
	tx->fw_rem_tx.num = cdp_tx->dropped.fw_rem_tx.num;
	tx->age_out = cdp_tx->dropped.age_out;
	tx->fw_reason1 = cdp_tx->dropped.fw_reason1;
	tx->fw_reason2 = cdp_tx->dropped.fw_reason2;
	tx->fw_reason3 = cdp_tx->dropped.fw_reason3;
	tx->drop_threshold = cdp_tx->dropped.drop_threshold;
	tx->drop_link_desc_na = cdp_tx->dropped.drop_link_desc_na;
	tx->invalid_drop = cdp_tx->dropped.invalid_drop;
	tx->mcast_vdev_drop = cdp_tx->dropped.mcast_vdev_drop;
	tx->invalid_rr = cdp_tx->dropped.invalid_rr;
	tx->fw_rem.num = cdp_tx->dropped.fw_rem.num;
	tx->fw_rem.bytes = cdp_tx->dropped.fw_rem.bytes;
	tx->ru_start = cdp_tx->ru_start;
	tx->ru_tones = cdp_tx->ru_tones;
	tx->last_tx_ts = cdp_tx->last_tx_ts;
	loop_cnt = qdf_min((uint8_t)WME_AC_MAX, (uint8_t)STATS_IF_WME_AC_MAX);
	for (inx = 0; inx < loop_cnt; inx++) {
		tx->wme_ac_type[inx] = cdp_tx->wme_ac_type[inx];
		tx->excess_retries_per_ac[inx] =
					cdp_tx->excess_retries_per_ac[inx];
	}
	loop_cnt = qdf_min((uint8_t)MAX_MU_GROUP_ID,
			   (uint8_t)STATS_IF_MAX_MU_GROUP_ID);
	for (inx = 0; inx < loop_cnt; inx++)
		tx->mu_group_id[inx] = cdp_tx->mu_group_id[inx];
	for (inx = 0; inx < QDF_PROTO_SUBTYPE_MAX; inx++)
		tx->no_ack_count[inx] = cdp_tx->no_ack_count[inx];
	loop_cnt = qdf_min((uint8_t)DOT11_MAX, (uint8_t)STATS_IF_DOT11_MAX);
	mcs_cnt = qdf_min((uint8_t)MAX_MCS, (uint8_t)STATS_IF_MAX_MCS);
	for (inx = 0; inx < loop_cnt; inx++)
		qdf_mem_copy(tx->pkt_type[inx].mcs_count,
			     cdp_tx->pkt_type[inx].mcs_count,
			     (sizeof(uint32_t) * mcs_cnt));
	loop_cnt = qdf_min((uint8_t)MAX_TRANSMIT_TYPES,
			   (uint8_t)STATS_IF_MAX_TRANSMIT_TYPES);
	for (inx = 0; inx < loop_cnt; inx++) {
		tx->transmit_type[inx].num_msdu =
					cdp_tx->transmit_type[inx].num_msdu;
		tx->transmit_type[inx].num_mpdu =
					cdp_tx->transmit_type[inx].num_mpdu;
		tx->transmit_type[inx].mpdu_tried =
					cdp_tx->transmit_type[inx].mpdu_tried;
	}
	loop_cnt = qdf_min((uint8_t)MAX_RU_LOCATIONS,
			   (uint8_t)STATS_IF_MAX_RU_LOCATIONS);
	for (inx = 0; inx < loop_cnt; inx++) {
		tx->ru_loc[inx].num_msdu = cdp_tx->ru_loc[inx].num_msdu;
		tx->ru_loc[inx].num_mpdu = cdp_tx->ru_loc[inx].num_mpdu;
		tx->ru_loc[inx].mpdu_tried = cdp_tx->ru_loc[inx].mpdu_tried;
	}
	loop_cnt = qdf_min((uint8_t)CDP_RSSI_CHAIN_LEN,
			   (uint8_t)STATS_IF_RSSI_CHAIN_MAX);
	for (inx = 0; inx < loop_cnt; inx++)
		tx->rssi_chain[inx] = cdp_tx->rssi_chain[inx];

	fill_tx_protocol_trace(tx, cdp_tx);
}

static void fill_debug_data_rx_stats(struct debug_data_rx_stats *rx,
				     struct cdp_rx_stats *cdp_rx)
{
	uint8_t inx;
	uint8_t loop_cnt, max_ss, cnt, mcs_cnt;

	rx->rx_discard = cdp_rx->rx_discard;
	rx->mic_err = cdp_rx->err.mic_err;
	rx->decrypt_err = cdp_rx->err.decrypt_err;
	rx->fcserr = cdp_rx->err.fcserr;
	rx->pn_err = cdp_rx->err.pn_err;
	rx->oor_err = cdp_rx->err.oor_err;
	rx->jump_2k_err = cdp_rx->err.jump_2k_err;
	rx->rxdma_wifi_parse_err = cdp_rx->err.rxdma_wifi_parse_err;
	rx->mec_drop.num = cdp_rx->mec_drop.num;
	rx->mec_drop.bytes = cdp_rx->mec_drop.bytes;
	rx->ppeds_drop.num = cdp_rx->ppeds_drop.num;
	rx->ppeds_drop.bytes = cdp_rx->ppeds_drop.bytes;
	rx->last_rx_ts = cdp_rx->last_rx_ts;
	rx->rx_mpdus = cdp_rx->rx_mpdus;
	rx->rx_ppdus = cdp_rx->rx_ppdus;
	loop_cnt = qdf_min((uint8_t)MAX_RECEPTION_TYPES,
			   (uint8_t)STATS_IF_MAX_RECEPTION_TYPES);
	for (inx = 0; inx < loop_cnt; inx++) {
		rx->ppdu_cnt[inx] = cdp_rx->ppdu_cnt[inx];
		rx->reception_type[inx] = cdp_rx->reception_type[inx];
	}
	loop_cnt = qdf_min((uint8_t)DOT11_MAX, (uint8_t)STATS_IF_DOT11_MAX);
	mcs_cnt = qdf_min((uint8_t)MAX_MCS, (uint8_t)STATS_IF_MAX_MCS);
	for (inx = 0; inx < loop_cnt; inx++)
		qdf_mem_copy(rx->pkt_type[inx].mcs_count,
			     cdp_rx->pkt_type[inx].mcs_count,
			     (sizeof(uint32_t) * mcs_cnt));
	loop_cnt = qdf_min((uint8_t)TXRX_TYPE_MU_MAX,
			   (uint8_t)STATS_IF_TXRX_TYPE_MU_MAX);
	max_ss = qdf_min((uint8_t)SS_COUNT, (uint8_t)STATS_IF_SS_COUNT);
	for (inx = 0; inx < loop_cnt; inx++) {
		for (cnt = 0; cnt < max_ss; cnt++)
			rx->rx_mu[inx].ppdu_nss[cnt] =
					cdp_rx->rx_mu[inx].ppdu_nss[cnt];
		rx->rx_mu[inx].mpdu_cnt_fcs_ok =
					cdp_rx->rx_mu[inx].mpdu_cnt_fcs_ok;
		rx->rx_mu[inx].mpdu_cnt_fcs_err =
					cdp_rx->rx_mu[inx].mpdu_cnt_fcs_err;
		qdf_mem_copy(rx->rx_mu[inx].ppdu.mcs_count,
			     cdp_rx->rx_mu[inx].ppdu.mcs_count,
			     (sizeof(uint32_t) * mcs_cnt));
	}
	loop_cnt = qdf_min((uint8_t)CDP_MAX_RX_RINGS,
			   (uint8_t)STATS_IF_MAX_RX_RINGS);
	for (inx = 0; inx < loop_cnt; inx++) {
		rx->rcvd_reo[inx].num = cdp_rx->rcvd_reo[inx].num;
		rx->rcvd_reo[inx].bytes = cdp_rx->rcvd_reo[inx].bytes;
	}
	fill_rx_protocol_trace(rx, cdp_rx);
}

static QDF_STATUS get_debug_peer_data_tx(struct unified_stats *stats,
					 struct cdp_peer_stats *peer_stats)
{
	struct debug_peer_data_tx *data = NULL;
	struct cdp_tx_stats *cdp_tx = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_peer_data_tx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	cdp_tx = &peer_stats->tx;
	fill_basic_peer_data_tx(&data->b_tx, cdp_tx);
	fill_debug_data_tx_stats(&data->dbg_tx, cdp_tx);
	data->last_per = cdp_tx->last_per;
	data->tx_bytes_success_last = cdp_tx->tx_bytes_success_last;
	data->tx_data_success_last = cdp_tx->tx_data_success_last;
	data->tx_byte_rate = cdp_tx->tx_byte_rate;
	data->tx_data_rate = cdp_tx->tx_data_rate;
	data->tx_data_ucast_last = cdp_tx->tx_data_ucast_last;
	data->tx_data_ucast_rate = cdp_tx->tx_data_ucast_rate;

	stats->feat[INX_FEAT_TX] = data;
	stats->size[INX_FEAT_TX] = sizeof(struct debug_peer_data_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_peer_data_rx(struct unified_stats *stats,
					 struct cdp_peer_stats *peer_stats)
{
	struct debug_peer_data_rx *data = NULL;
	struct cdp_rx_stats *cdp_rx = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_peer_data_rx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	cdp_rx = &peer_stats->rx;
	fill_basic_peer_data_rx(&data->b_rx, cdp_rx);
	fill_debug_data_rx_stats(&data->dbg_rx, cdp_rx);
	data->rx_bytes_success_last = cdp_rx->rx_bytes_success_last;
	data->rx_data_success_last = cdp_rx->rx_data_success_last;
	data->rx_byte_rate = cdp_rx->rx_byte_rate;
	data->rx_data_rate = cdp_rx->rx_data_rate;

	stats->feat[INX_FEAT_RX] = data;
	stats->size[INX_FEAT_RX] = sizeof(struct debug_peer_data_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_peer_data_link(struct unified_stats *stats,
					   struct cdp_peer_stats *peer_stats)
{
	struct debug_peer_data_link *data = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_peer_data_link));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_data_link(&data->b_link, &peer_stats->rx);
	data->last_ack_rssi = peer_stats->tx.last_ack_rssi;
	data->avg_ack_rssi = CDP_SNR_OUT(peer_stats->tx.avg_ack_rssi);

	stats->feat[INX_FEAT_LINK] = data;
	stats->size[INX_FEAT_LINK] = sizeof(struct debug_peer_data_link);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_peer_data_rate(struct unified_stats *stats,
					   struct cdp_peer_stats *peer_stats)
{
	struct debug_peer_data_rate *data = NULL;

	if (!stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_peer_data_rate));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_data_rate(&data->b_rate, peer_stats);
	data->last_tx_rate_mcs = peer_stats->tx.last_tx_rate_mcs;
	data->mcast_last_tx_rate = peer_stats->tx.mcast_last_tx_rate;
	data->mcast_last_tx_rate_mcs = peer_stats->tx.mcast_last_tx_rate_mcs;

	stats->feat[INX_FEAT_RATE] = data;
	stats->size[INX_FEAT_RATE] = sizeof(struct debug_peer_data_rate);

	return QDF_STATUS_SUCCESS;
}

#ifdef WLAN_TX_PKT_CAPTURE_ENH_DEBUG
static void fill_mpdu_msdu_for_txcap(struct debug_peer_data_txcap *data,
				     struct cdp_peer_tx_capture_stats *cap)
{
	uint8_t inx, loop_cnt;

	loop_cnt = qdf_min((uint8_t)PEER_MPDU_DESC_MAX,
			   (uint8_t)STATS_IF_MPDU_MAX);
	for (inx = 0; inx < loop_cnt; inx++)
		data->mpdu[inx] = cap->mpdu[inx];
	loop_cnt = qdf_min((uint8_t)PEER_MSDU_DESC_MAX,
			   (uint8_t)STATS_IF_MSDU_MAX);
	for (inx = 0; inx < loop_cnt; inx++)
		data->msdu[inx] = cap->msdu[inx];
}
#else
static void fill_mpdu_msdu_for_txcap(struct debug_peer_data_txcap *data,
				     struct cdp_peer_tx_capture_stats *cap)
{
}
#endif /* WLAN_TX_PKT_CAPTURE_ENH_DEBUG */

static QDF_STATUS
get_debug_peer_data_txcap(struct unified_stats *stats,
			  struct cdp_peer_tx_capture_stats *cap)
{
	struct debug_peer_data_txcap *data = NULL;
	uint8_t inx, loop_cnt;

	if (!stats || !cap) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_peer_data_txcap));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	loop_cnt = qdf_min((uint8_t)CDP_MAX_TIDS, (uint8_t)STATS_IF_MAX_TIDS);
	for (inx = 0; inx < loop_cnt; inx++) {
		data->defer_msdu_len[inx] = cap->len_stats[inx].defer_msdu_len;
		data->tasklet_msdu_len[inx] =
					cap->len_stats[inx].tasklet_msdu_len;
		data->pending_q_len[inx] = cap->len_stats[inx].pending_q_len;
	}
	fill_mpdu_msdu_for_txcap(data, cap);

	stats->feat[INX_FEAT_TXCAP] = data;
	stats->size[INX_FEAT_TXCAP] = sizeof(struct debug_peer_data_txcap);

	return QDF_STATUS_SUCCESS;
}

#if WLAN_TX_PKT_CAPTURE_ENH
static QDF_STATUS
get_peer_tx_capture_stats(void *dp_soc_handle,
			  uint8_t vdev_id, uint8_t *stamac,
			  struct cdp_peer_tx_capture_stats *cap)
{
	return cdp_get_peer_tx_capture_stats(dp_soc_handle, vdev_id,
					     stamac, cap);
}

static QDF_STATUS
get_pdev_tx_capture_stats(void *dp_soc_handle, uint8_t pdev_id,
			  struct cdp_pdev_tx_capture_stats *cap)
{
	return cdp_get_pdev_tx_capture_stats(dp_soc_handle, pdev_id, cap);
}
#else
static QDF_STATUS
get_peer_tx_capture_stats(void *dp_soc_handle,
			  uint8_t vdev_id, uint8_t *stamac,
			  struct cdp_peer_tx_capture_stats *cap)
{
	return QDF_STATUS_E_FAILURE;
}

static QDF_STATUS
get_pdev_tx_capture_stats(void *dp_soc_handle, uint8_t pdev_id,
			  struct cdp_pdev_tx_capture_stats *cap)
{
	return QDF_STATUS_E_FAILURE;
}
#endif /* WLAN_TX_PKT_CAPTURE_ENH */

#ifdef WLAN_CONFIG_TELEMETRY_AGENT
static QDF_STATUS get_peer_deter_stats(
				ol_txrx_soc_handle soc,
				uint8_t vdev_id,
				uint8_t *addr,
				struct cdp_peer_deter_stats *deter)
{
	return cdp_get_peer_deter_stats(soc, vdev_id,
					addr, deter);
}
static QDF_STATUS get_pdev_deter_stats(
				ol_txrx_soc_handle soc,
				uint8_t pdev_id,
				struct cdp_pdev_deter_stats *deter)
{
	return cdp_get_pdev_deter_stats(soc, pdev_id, deter);
}

static QDF_STATUS
get_debug_peer_deter_stats(struct unified_stats *stats,
			   struct cdp_peer_deter_stats *deter)
{
	struct debug_peer_data_deter *data = NULL;
	uint8_t inx;

	if (!stats || !deter) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_peer_data_deter));

	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	for (inx = 0; inx < STATS_IF_DATA_TID_MAX; inx++) {
		qdf_mem_copy(data->deter_stats[inx].dl, deter[inx].dl_det,
			     sizeof(data->deter_stats[inx].dl));
		qdf_mem_copy(data->deter_stats[inx].ul, deter[inx].ul_det,
			     sizeof(data->deter_stats[inx].ul));
		qdf_mem_copy(&data->deter_stats[inx].rx_det, &deter[inx].rx_det,
			     sizeof(data->deter_stats[inx].rx_det));
	}

	stats->feat[INX_FEAT_DETER] = data;
	stats->size[INX_FEAT_DETER] = sizeof(struct debug_peer_data_deter);

	return QDF_STATUS_SUCCESS;
}
#else
static QDF_STATUS get_peer_deter_stats(
				ol_txrx_soc_handle soc,
				uint8_t vdev_id,
				uint8_t *addr,
				struct cdp_peer_deter_stats *deter)
{
	return QDF_STATUS_E_FAILURE;
}

static QDF_STATUS get_pdev_deter_stats(
				ol_txrx_soc_handle soc,
				uint8_t pdev_id,
				struct cdp_pdev_deter_stats *stats)
{
	return QDF_STATUS_E_FAILURE;
}

static QDF_STATUS
get_debug_peer_deter_stats(struct unified_stats *stats,
			   struct cdp_peer_deter_stats *deter)
{
	return QDF_STATUS_E_FAILURE;
}
#endif

static QDF_STATUS get_debug_peer_data(struct wlan_objmgr_psoc *psoc,
				      struct wlan_objmgr_vdev *vdev,
				      uint8_t *peer_mac,
				      struct unified_stats *stats,
				      uint32_t feat,
				      enum stats_peer_type peer_type)
{
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	struct cdp_peer_stats *peer_stats = NULL;
	struct cdp_peer_tx_capture_stats *cap = NULL;
	struct cdp_peer_deter_stats *deter = NULL;
	uint8_t vdev_id = 0;
	void *dp_soc = NULL;
	bool stats_collected = false;

	vdev_id = wlan_vdev_get_id(vdev);
	dp_soc = wlan_psoc_get_dp_handle(psoc);
	if (feat & ~STATS_FEAT_FLG_TXCAP) {
		peer_stats = qdf_mem_malloc(sizeof(struct cdp_peer_stats));
		if (!peer_stats) {
			qdf_err("Failed allocation!");
			return QDF_STATUS_E_NOMEM;
		}
		ret = cdp_host_get_peer_stats_based_on_peer_type(dp_soc,
						vdev_id, peer_mac,
						peer_stats, peer_type);
		if (ret != QDF_STATUS_SUCCESS) {
			qdf_err("Unable to fetch stats!");
			goto get_failed;
		}
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_debug_peer_data_tx(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Debug TX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_debug_peer_data_rx(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Debug RX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_LINK) {
		ret = get_debug_peer_data_link(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Debug LINK Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RATE) {
		ret = get_debug_peer_data_rate(stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Debug RATE Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_TXCAP) {
		cap = qdf_mem_malloc(sizeof(struct cdp_peer_tx_capture_stats));
		if (!cap) {
			ret = QDF_STATUS_E_NOMEM;
			goto get_failed;
		}
		ret = get_peer_tx_capture_stats(dp_soc, vdev_id, peer_mac, cap);
		if (ret == QDF_STATUS_SUCCESS)
			ret = get_debug_peer_data_txcap(stats, cap);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Debug TXCAP Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_DETER) {
		deter = qdf_mem_malloc(sizeof(struct cdp_peer_deter_stats) *
				       CDP_DATA_TID_MAX);
		if (!deter) {
			ret = QDF_STATUS_E_NOMEM;
			goto get_failed;
		}
		ret = get_peer_deter_stats(dp_soc, vdev_id,
					   peer_mac, deter);
		if (ret == QDF_STATUS_SUCCESS)
			ret = get_debug_peer_deter_stats(stats, deter);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Debug deter Stats!");
		else
			stats_collected = true;
	}

get_failed:
	if (deter)
		qdf_mem_free(deter);
	if (cap)
		qdf_mem_free(cap);
	if (peer_stats)
		qdf_mem_free(peer_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_debug_peer_ctrl_tx(struct unified_stats *stats,
					 struct peer_ic_cp_stats *cp_stats,
					 struct cdp_peer_stats *peer_stats)
{
	struct debug_peer_ctrl_tx *ctrl = NULL;

	if (!stats || !cp_stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct debug_peer_ctrl_tx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_ctrl_tx(&ctrl->b_tx, cp_stats);
	ctrl->cs_ps_discard = cp_stats->cs_ps_discard;
	ctrl->cs_psq_drops = cp_stats->cs_psq_drops;
	ctrl->cs_tx_dropblock = cp_stats->cs_tx_dropblock;
	ctrl->cs_is_tx_nobuf = cp_stats->cs_is_tx_nobuf;
	ctrl->rts_success = peer_stats->tx.rts_success;
	ctrl->rts_failure = peer_stats->tx.rts_failure;
	ctrl->bar_cnt = peer_stats->tx.bar_cnt;
	ctrl->ndpa_cnt = peer_stats->tx.ndpa_cnt;

	stats->feat[INX_FEAT_TX] = ctrl;
	stats->size[INX_FEAT_TX] = sizeof(struct debug_peer_ctrl_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_peer_ctrl_rx(struct unified_stats *stats,
					 struct peer_ic_cp_stats *cp_stats,
					 struct cdp_peer_stats *peer_stats)
{
	struct debug_peer_ctrl_rx *ctrl = NULL;

	if (!stats || !cp_stats || !peer_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct debug_peer_ctrl_rx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_ctrl_rx(&ctrl->b_rx, cp_stats);
	ctrl->cs_rx_noprivacy = cp_stats->cs_rx_noprivacy;
	ctrl->cs_rx_wepfail = cp_stats->cs_rx_wepfail;
	ctrl->cs_rx_ccmpmic = cp_stats->cs_rx_ccmpmic;
	ctrl->cs_rx_wpimic = cp_stats->cs_rx_wpimic;
	ctrl->cs_rx_tkipicv = cp_stats->cs_rx_tkipicv;
	ctrl->bar_cnt = peer_stats->rx.bar_cnt;
	ctrl->ndpa_cnt = peer_stats->rx.ndpa_cnt;

	stats->feat[INX_FEAT_RX] = ctrl;
	stats->size[INX_FEAT_RX] = sizeof(struct debug_peer_ctrl_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_peer_ctrl_link(struct unified_stats *stats,
					   struct peer_ic_cp_stats *cp_stats)
{
	struct debug_peer_ctrl_link *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct debug_peer_ctrl_link));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_ctrl_link(&ctrl->b_link, cp_stats);

	stats->feat[INX_FEAT_LINK] = ctrl;
	stats->size[INX_FEAT_LINK] = sizeof(struct debug_peer_ctrl_link);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_peer_ctrl_rate(struct unified_stats *stats,
					   struct peer_ic_cp_stats *cp_stats)
{
	struct debug_peer_ctrl_rate *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct debug_peer_ctrl_rate));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_peer_ctrl_rate(&ctrl->b_rate, cp_stats);

	stats->feat[INX_FEAT_RATE] = ctrl;
	stats->size[INX_FEAT_RATE] = sizeof(struct debug_peer_ctrl_rate);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_peer_ctrl(struct wlan_objmgr_psoc *psoc,
				      uint8_t *peer_mac,
				      struct unified_stats *stats,
				      uint32_t feat)
{
	struct wlan_objmgr_peer *peer;
	struct wlan_objmgr_vdev *vdev;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	struct peer_ic_cp_stats *peer_cp_stats = NULL;
	bool stats_collected = false;
	struct cdp_peer_stats *peer_stats = NULL;
	void *dp_soc = NULL;
	uint8_t vdev_id = 0;

	peer = wlan_objmgr_get_peer_by_mac(psoc, peer_mac, WLAN_MLME_SB_ID);
	if (!peer) {
		qdf_err("Peer (%s) not found!", ether_sprintf(peer_mac));
		return QDF_STATUS_E_FAILURE;
	}
	vdev = wlan_peer_get_vdev(peer);
	if (!vdev) {
		qdf_err("Vdev is NULL!");
		ret = QDF_STATUS_E_FAILURE;
		goto get_failed;
	}

	peer_cp_stats = qdf_mem_malloc(sizeof(struct peer_ic_cp_stats));
	if (!peer_cp_stats) {
		qdf_err("Allocation Failed!");
		ret = QDF_STATUS_E_NOMEM;
		goto get_failed;
	}
	peer_stats = qdf_mem_malloc(sizeof(struct cdp_peer_stats));
	if (!peer_stats) {
		qdf_err("Allocation Failed!");
		ret = QDF_STATUS_E_NOMEM;
		goto get_failed;
	}

	ret = wlan_cfg80211_get_peer_cp_stats(peer, peer_cp_stats);
	if (QDF_STATUS_SUCCESS != ret) {
		qdf_err("Failed to get Peer Control Stats!");
		goto get_failed;
	}
	vdev_id = wlan_vdev_get_id(vdev);
	dp_soc = wlan_psoc_get_dp_handle(psoc);
	ret = cdp_host_get_peer_stats(dp_soc, vdev_id, peer_mac, peer_stats);
	if (ret != QDF_STATUS_SUCCESS) {
		qdf_err("Unable to fetch stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_debug_peer_ctrl_tx(stats, peer_cp_stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Debug TX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_debug_peer_ctrl_rx(stats, peer_cp_stats, peer_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Debug RX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_LINK) {
		ret = get_debug_peer_ctrl_link(stats, peer_cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Debug LINK Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RATE) {
		ret = get_debug_peer_ctrl_rate(stats, peer_cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch peer Debug RATE Stats!");
		else
			stats_collected = true;
	}

get_failed:
	wlan_objmgr_peer_release_ref(peer, WLAN_MLME_SB_ID);
	if (peer_cp_stats)
		qdf_mem_free(peer_cp_stats);
	if (peer_stats)
		qdf_mem_free(peer_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_debug_vdev_data_tx(struct unified_stats *stats,
					 struct cdp_vdev_stats *vdev_stats)
{
	struct debug_vdev_data_tx *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &vdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct debug_vdev_data_tx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_data_tx(&data->b_tx, vdev_stats);
	fill_debug_data_tx_stats(&data->dbg_tx, &vdev_stats->tx);
	data->desc_na.num = tx_i->dropped.desc_na.num;
	data->desc_na.bytes = tx_i->dropped.desc_na.bytes;
	data->desc_na_exc_alloc_fail.num =
				tx_i->dropped.desc_na_exc_alloc_fail.num;
	data->desc_na_exc_alloc_fail.bytes =
				tx_i->dropped.desc_na_exc_alloc_fail.bytes;
	data->desc_na_exc_outstand.num = tx_i->dropped.desc_na_exc_outstand.num;
	data->desc_na_exc_outstand.bytes =
				tx_i->dropped.desc_na_exc_outstand.bytes;
	data->exc_desc_na.num = tx_i->dropped.exc_desc_na.num;
	data->exc_desc_na.bytes = tx_i->dropped.exc_desc_na.bytes;
	data->sniffer_rcvd.num = tx_i->sniffer_rcvd.num;
	data->sniffer_rcvd.bytes = tx_i->sniffer_rcvd.bytes;
	data->ring_full = tx_i->dropped.ring_full;
	data->enqueue_fail = tx_i->dropped.enqueue_fail;
	data->dma_error = tx_i->dropped.dma_error;
	data->res_full = tx_i->dropped.res_full;
	data->headroom_insufficient = tx_i->dropped.headroom_insufficient;
	data->fail_per_pkt_vdev_id_check =
				tx_i->dropped.fail_per_pkt_vdev_id_check;
	data->drop_ingress = tx_i->dropped.drop_ingress;
	data->invalid_peer_id_in_exc_path =
				tx_i->dropped.invalid_peer_id_in_exc_path;
	data->tx_mcast_drop = tx_i->dropped.tx_mcast_drop;
	data->fw2wbm_tx_drop = tx_i->dropped.fw2wbm_tx_drop;

	stats->feat[INX_FEAT_TX] = data;
	stats->size[INX_FEAT_TX] = sizeof(struct debug_vdev_data_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_vdev_data_rx(struct unified_stats *stats,
					 struct cdp_vdev_stats *vdev_stats)
{
	struct debug_vdev_data_rx *data = NULL;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_vdev_data_rx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_data_rx(&data->b_rx, &vdev_stats->rx);
	fill_debug_data_rx_stats(&data->dbg_rx, &vdev_stats->rx);

	stats->feat[INX_FEAT_RX] = data;
	stats->size[INX_FEAT_RX] = sizeof(struct debug_vdev_data_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_vdev_data_me(struct unified_stats *stats,
					 struct cdp_vdev_stats *vdev_stats)
{
	struct debug_vdev_data_me *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &vdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct debug_vdev_data_me));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->dropped_map_error = tx_i->mcast_en.dropped_map_error;
	data->dropped_self_mac = tx_i->mcast_en.dropped_self_mac;
	data->dropped_send_fail = tx_i->mcast_en.dropped_send_fail;
	data->fail_seg_alloc = tx_i->mcast_en.fail_seg_alloc;
	data->clone_fail = tx_i->mcast_en.clone_fail;

	stats->feat[INX_FEAT_ME] = data;
	stats->size[INX_FEAT_ME] = sizeof(struct debug_vdev_data_me);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_vdev_data_raw(struct unified_stats *stats,
					  struct cdp_vdev_stats *vdev_stats)
{
	struct debug_vdev_data_raw *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &vdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct debug_vdev_data_raw));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->dma_map_error = tx_i->raw.dma_map_error;
	data->invalid_raw_pkt_datatype = tx_i->raw.invalid_raw_pkt_datatype;
	data->num_frags_overflow_err = tx_i->raw.num_frags_overflow_err;

	stats->feat[INX_FEAT_RAW] = data;
	stats->size[INX_FEAT_RAW] = sizeof(struct debug_vdev_data_raw);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_vdev_data_tso(struct unified_stats *stats,
					  struct cdp_vdev_stats *vdev_stats)
{
	struct debug_vdev_data_tso *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !vdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &vdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct debug_vdev_data_tso));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->dma_map_error = tx_i->sg.dma_map_error;
	data->dropped_target = tx_i->sg.dropped_target;
	data->dropped_host.num = tx_i->sg.dropped_host.num;
	data->dropped_host.bytes = tx_i->sg.dropped_host.bytes;

	stats->feat[INX_FEAT_TSO] = data;
	stats->size[INX_FEAT_TSO] = sizeof(struct debug_vdev_data_tso);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_vdev_data(struct wlan_objmgr_psoc *psoc,
				      struct wlan_objmgr_vdev *vdev,
				      struct unified_stats *stats,
				      uint32_t feat, bool mld_req,
				      bool mld_link)
{
	struct cdp_vdev_stats *vdev_stats = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	bool stats_collected = false;

	if (!psoc || !vdev) {
		qdf_err("Invalid psoc or vdev!");
		return QDF_STATUS_E_INVAL;
	}
	vdev_stats = qdf_mem_malloc(sizeof(struct cdp_vdev_stats));
	if (!vdev_stats) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}

#ifdef CONFIG_MLO_SINGLE_DEV
	if (mld_link)
		ret = cdp_mlo_get_mld_vdev_stats(wlan_psoc_get_dp_handle(psoc),
						 wlan_vdev_get_id(vdev),
						 vdev_stats, true);
	else if (mld_req)
		ret = cdp_mlo_get_mld_vdev_stats(wlan_psoc_get_dp_handle(psoc),
						 wlan_vdev_get_id(vdev),
						 vdev_stats, false);
	else
#endif
		ret = cdp_host_get_interface_stats(
						wlan_psoc_get_dp_handle(psoc),
						wlan_vdev_get_id(vdev),
						vdev_stats);

	if (ret != QDF_STATUS_SUCCESS) {
		qdf_err("Unable to get Vdev Stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_debug_vdev_data_tx(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Debug TX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_debug_vdev_data_rx(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Debug RX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_ME) {
		ret = get_debug_vdev_data_me(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Debug ME Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RAW) {
		ret = get_debug_vdev_data_raw(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Debug RAW Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_TSO) {
		ret = get_debug_vdev_data_tso(stats, vdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Debug TSO Stats!");
		else
			stats_collected = true;
	}

get_failed:
	qdf_mem_free(vdev_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_debug_vdev_ctrl_tx(struct unified_stats *stats,
					 struct vdev_ic_cp_stats *cp_stats)
{
	struct debug_vdev_ctrl_tx *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct debug_vdev_ctrl_tx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_ctrl_tx(&ctrl->b_tx, cp_stats);

	ctrl->cs_tx_bcn_swba = cp_stats->stats.cs_tx_bcn_swba;
	ctrl->cs_tx_nodefkey = cp_stats->stats.cs_tx_nodefkey;
	ctrl->cs_tx_noheadroom = cp_stats->stats.cs_tx_noheadroom;
	ctrl->cs_tx_nobuf = cp_stats->stats.cs_tx_nobuf;
	ctrl->cs_tx_nonode = cp_stats->stats.cs_tx_nonode;
	ctrl->cs_tx_cipher_err = cp_stats->stats.cs_tx_cipher_err;
	ctrl->cs_tx_not_ok = cp_stats->stats.cs_tx_not_ok;

	stats->feat[INX_FEAT_TX] = ctrl;
	stats->size[INX_FEAT_TX] = sizeof(struct debug_vdev_ctrl_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_vdev_ctrl_rx(struct unified_stats *stats,
					 struct vdev_ic_cp_stats *cp_stats)
{
	struct debug_vdev_ctrl_rx *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct debug_vdev_ctrl_rx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_ctrl_rx(&ctrl->b_rx, cp_stats);

	ctrl->cs_invalid_macaddr_nodealloc_fail =
			cp_stats->stats.cs_invalid_macaddr_nodealloc_fail;
	ctrl->cs_rx_wrongdir = cp_stats->stats.cs_rx_wrongdir;
	ctrl->cs_rx_not_assoc = cp_stats->stats.cs_rx_not_assoc;
	ctrl->cs_rx_rs_too_big = cp_stats->stats.cs_rx_rs_too_big;
	ctrl->cs_rx_elem_missing = cp_stats->stats.cs_rx_elem_missing;
	ctrl->cs_rx_elem_too_big = cp_stats->stats.cs_rx_elem_too_big;
	ctrl->cs_rx_chan_err = cp_stats->stats.cs_rx_chan_err;
	ctrl->cs_rx_node_alloc = cp_stats->stats.cs_rx_node_alloc;
	ctrl->cs_rx_auth_unsupported = cp_stats->stats.cs_rx_auth_unsupported;
	ctrl->cs_rx_auth_fail = cp_stats->stats.cs_rx_auth_fail;
	ctrl->cs_rx_auth_countermeasures =
				cp_stats->stats.cs_rx_auth_countermeasures;
	ctrl->cs_rx_assoc_bss = cp_stats->stats.cs_rx_assoc_bss;
	ctrl->cs_rx_assoc_notauth = cp_stats->stats.cs_rx_assoc_notauth;
	ctrl->cs_rx_assoc_cap_mismatch =
				cp_stats->stats.cs_rx_assoc_cap_mismatch;
	ctrl->cs_rx_assoc_norate = cp_stats->stats.cs_rx_assoc_norate;
	ctrl->cs_rx_assoc_wpaie_err = cp_stats->stats.cs_rx_assoc_wpaie_err;
	ctrl->cs_rx_auth_err = cp_stats->stats.cs_rx_auth_err;
	ctrl->cs_rx_acl = cp_stats->stats.cs_rx_acl;
	ctrl->cs_rx_nowds = cp_stats->stats.cs_rx_nowds;
	ctrl->cs_rx_wrongbss = cp_stats->stats.cs_rx_wrongbss;
	ctrl->cs_rx_tooshort = cp_stats->stats.cs_rx_tooshort;
	ctrl->cs_rx_ssid_mismatch = cp_stats->stats.cs_rx_ssid_mismatch;
	ctrl->cs_rx_decryptok_u = cp_stats->ucast_stats.cs_rx_decryptok;
	ctrl->cs_rx_decryptok_m = cp_stats->mcast_stats.cs_rx_decryptok;

	stats->feat[INX_FEAT_RX] = ctrl;
	stats->size[INX_FEAT_RX] = sizeof(struct debug_vdev_ctrl_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_vdev_ctrl_wmi(struct unified_stats *stats,
					  struct vdev_ic_cp_stats *cp_stats)
{
	struct debug_vdev_ctrl_wmi *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct debug_vdev_ctrl_wmi));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	ctrl->cs_peer_delete_req = cp_stats->stats.cs_peer_delete_req;
	ctrl->cs_peer_delete_resp = cp_stats->stats.cs_peer_delete_resp;
	ctrl->cs_peer_delete_all_req = cp_stats->stats.cs_peer_delete_all_req;
	ctrl->cs_peer_delete_all_resp = cp_stats->stats.cs_peer_delete_all_resp;

	stats->feat[INX_FEAT_WMI] = ctrl;
	stats->size[INX_FEAT_WMI] = sizeof(struct debug_vdev_ctrl_wmi);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_vdev_ctrl_link(struct unified_stats *stats,
					   struct wlan_objmgr_vdev *vdev)
{
	struct debug_vdev_ctrl_link *ctrl;

	if (!stats || !vdev) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct debug_vdev_ctrl_link));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_vdev_ctrl_link(&ctrl->b_link, vdev);

	stats->feat[INX_FEAT_LINK] = ctrl;
	stats->size[INX_FEAT_LINK] = sizeof(struct debug_vdev_ctrl_link);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_vdev_ctrl(struct wlan_objmgr_vdev *vdev,
				      struct unified_stats *stats,
				      uint32_t feat, bool mld_req)
{
	struct vdev_ic_cp_stats *cp_stats = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	bool stats_collected = false;

	if (!vdev) {
		qdf_err("Invalid vdev!");
		return QDF_STATUS_E_INVAL;
	}
	cp_stats = qdf_mem_malloc(sizeof(struct vdev_ic_cp_stats));
	if (!cp_stats) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}

#ifdef CONFIG_MLO_SINGLE_DEV
	if (mld_req) {
		uint16_t num_links = 0, i = 0;
		QDF_STATUS status = QDF_STATUS_SUCCESS;
		struct vdev_ic_cp_stats *temp_cp_stats = NULL;
		struct wlan_objmgr_vdev *vdev_list[WLAN_UMAC_MLO_MAX_VDEVS] = {NULL};

		temp_cp_stats = qdf_mem_malloc(sizeof(struct vdev_ic_cp_stats));
		if (!temp_cp_stats) {
			qdf_err("Allocation Failed!");
			qdf_mem_free(cp_stats);
			return QDF_STATUS_E_NOMEM;
		}
		mlo_ap_get_vdev_list(vdev, &num_links, vdev_list);
		for (i = 0; i < num_links; i++) {
			status = get_vdev_cp_stats(vdev_list[i], temp_cp_stats);
			mlo_release_vdev_ref(vdev_list[i]);
			if (status != QDF_STATUS_SUCCESS) {
				ret = status;
				continue;
			}
			aggregate_mld_vdev_stats(cp_stats, temp_cp_stats);
		}
		qdf_mem_free(temp_cp_stats);
	} else
#endif
	{
		ret = get_vdev_cp_stats(vdev, cp_stats);
	}

	if (QDF_STATUS_SUCCESS != ret) {
		qdf_err("Unable to get Vdev Control stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_debug_vdev_ctrl_tx(stats, cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Debug TX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_debug_vdev_ctrl_rx(stats, cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Debug RX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_WMI) {
		ret = get_debug_vdev_ctrl_wmi(stats, cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Debug WMI Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_LINK) {
		ret = get_debug_vdev_ctrl_link(stats, vdev);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch vdev Debug LINK Stats!");
		else
			stats_collected = true;
	}

get_failed:
	qdf_mem_free(cp_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_debug_pdev_data_tx(struct unified_stats *stats,
					 struct cdp_pdev_stats *pdev_stats)
{
	struct debug_pdev_data_tx *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &pdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct debug_pdev_data_tx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_data_tx(&data->b_tx, pdev_stats);
	fill_debug_data_tx_stats(&data->dbg_tx, &pdev_stats->tx);
	data->desc_na.num = tx_i->dropped.desc_na.num;
	data->desc_na.bytes = tx_i->dropped.desc_na.bytes;
	data->desc_na_exc_alloc_fail.num =
				tx_i->dropped.desc_na_exc_alloc_fail.num;
	data->desc_na_exc_alloc_fail.bytes =
				tx_i->dropped.desc_na_exc_alloc_fail.bytes;
	data->desc_na_exc_outstand.num = tx_i->dropped.desc_na_exc_outstand.num;
	data->desc_na_exc_outstand.bytes =
				tx_i->dropped.desc_na_exc_outstand.bytes;
	data->exc_desc_na.num = tx_i->dropped.exc_desc_na.num;
	data->exc_desc_na.bytes = tx_i->dropped.exc_desc_na.bytes;
	data->ring_full = tx_i->dropped.ring_full;
	data->enqueue_fail = tx_i->dropped.enqueue_fail;
	data->dma_error = tx_i->dropped.dma_error;
	data->res_full = tx_i->dropped.res_full;
	data->headroom_insufficient = tx_i->dropped.headroom_insufficient;
	data->fail_per_pkt_vdev_id_check =
				tx_i->dropped.fail_per_pkt_vdev_id_check;
	data->drop_ingress = tx_i->dropped.drop_ingress;
	data->invalid_peer_id_in_exc_path =
				tx_i->dropped.invalid_peer_id_in_exc_path;
	data->tx_mcast_drop = tx_i->dropped.tx_mcast_drop;
	data->fw2wbm_tx_drop = tx_i->dropped.fw2wbm_tx_drop;
	data->tx_desc_err = pdev_stats->eap_drop_stats.tx_desc_err;
	data->tx_hal_ring_access_err =
			pdev_stats->eap_drop_stats.tx_hal_ring_access_err;
	data->tx_dma_map_err = pdev_stats->eap_drop_stats.tx_dma_map_err;
	data->tx_hw_enqueue_dropped = pdev_stats->eap_drop_stats.tx_hw_enqueue;
	data->tx_sw_enqueue_dropped = pdev_stats->eap_drop_stats.tx_sw_enqueue;
	data->sg_desc_cnt = pdev_stats->sg_desc_cnt;

	stats->feat[INX_FEAT_TX] = data;
	stats->size[INX_FEAT_TX] = sizeof(struct debug_pdev_data_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_pdev_data_rx(struct unified_stats *stats,
					 struct cdp_pdev_stats *pdev_stats)
{
	struct debug_pdev_data_rx *data = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_pdev_data_rx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_data_rx(&data->b_rx, pdev_stats);
	fill_debug_data_rx_stats(&data->dbg_rx, &pdev_stats->rx);

	data->replenished_pkts.num = pdev_stats->replenish.pkts.num;
	data->replenished_pkts.bytes = pdev_stats->replenish.pkts.bytes;
	data->rxdma_err = pdev_stats->replenish.rxdma_err;
	data->nbuf_alloc_fail = pdev_stats->replenish.nbuf_alloc_fail;
	data->frag_alloc_fail = pdev_stats->replenish.frag_alloc_fail;
	data->map_err = pdev_stats->replenish.map_err;
	data->x86_fail = pdev_stats->replenish.x86_fail;
	data->low_thresh_intrs = pdev_stats->replenish.low_thresh_intrs;
	data->free_list = pdev_stats->replenish.free_list;
	data->buf_freelist = pdev_stats->buf_freelist;
	data->vlan_tag_stp_cnt = pdev_stats->vlan_tag_stp_cnt;
	data->msdu_not_done = pdev_stats->dropped.msdu_not_done;
	data->mec = pdev_stats->dropped.mec;
	data->mesh_filter = pdev_stats->dropped.mesh_filter;
	data->wifi_parse = pdev_stats->dropped.wifi_parse;
	data->mon_rx_drop = pdev_stats->dropped.mon_rx_drop;
	data->mon_radiotap_update_err =
				pdev_stats->dropped.mon_radiotap_update_err;
	data->desc_alloc_fail = pdev_stats->err.desc_alloc_fail;
	data->ip_csum_err = pdev_stats->err.ip_csum_err;
	data->tcp_udp_csum_err = pdev_stats->err.tcp_udp_csum_err;
	data->rxdma_error = pdev_stats->err.rxdma_error;
	data->reo_error = pdev_stats->err.reo_error;
	data->desc_lt_alloc_fail = pdev_stats->err.desc_lt_alloc_fail;
	data->fw_reported_rxdma_error = pdev_stats->err.fw_reported_rxdma_error;
	data->num_bufs_consumed = pdev_stats->rx_buffer_pool.num_bufs_consumed;
	data->num_pool_bufs_replenish =
			pdev_stats->rx_buffer_pool.num_pool_bufs_replenish;
	data->num_bufs_alloc_success =
			pdev_stats->rx_buffer_pool.num_bufs_alloc_success;
	data->num_bufs_refilled =
			pdev_stats->rx_refill_buff_pool.num_bufs_refilled;
	data->num_bufs_allocated =
			pdev_stats->rx_refill_buff_pool.num_bufs_allocated;

	stats->feat[INX_FEAT_RX] = data;
	stats->size[INX_FEAT_RX] = sizeof(struct debug_pdev_data_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_pdev_data_me(struct unified_stats *stats,
					 struct cdp_pdev_stats *pdev_stats)
{
	struct debug_pdev_data_me *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &pdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct debug_pdev_data_me));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->dropped_map_error = tx_i->mcast_en.dropped_map_error;
	data->dropped_self_mac = tx_i->mcast_en.dropped_self_mac;
	data->dropped_send_fail = tx_i->mcast_en.dropped_send_fail;
	data->fail_seg_alloc = tx_i->mcast_en.fail_seg_alloc;
	data->clone_fail = tx_i->mcast_en.clone_fail;

	stats->feat[INX_FEAT_ME] = data;
	stats->size[INX_FEAT_ME] = sizeof(struct debug_pdev_data_me);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_pdev_data_raw(struct unified_stats *stats,
					  struct cdp_pdev_stats *pdev_stats)
{
	struct debug_pdev_data_raw *data = NULL;
	struct cdp_tx_ingress_stats *tx_i = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tx_i = &pdev_stats->tx_i;
	data = qdf_mem_malloc(sizeof(struct debug_pdev_data_raw));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->dma_map_error = tx_i->raw.dma_map_error;
	data->invalid_raw_pkt_datatype = tx_i->raw.invalid_raw_pkt_datatype;
	data->num_frags_overflow_err = tx_i->raw.num_frags_overflow_err;

	stats->feat[INX_FEAT_RAW] = data;
	stats->size[INX_FEAT_RAW] = sizeof(struct debug_pdev_data_raw);

	return QDF_STATUS_SUCCESS;
}

#ifdef FEATURE_TSO_STATS
static void fill_debug_pdev_data_tso_info(struct debug_pdev_data_tso *data,
					  struct cdp_tso_stats *tso)
{
	uint8_t inx;

	for (inx = 0; inx < STATS_IF_TSO_PACKETS_MAX; inx++) {
		data->pkt_info[inx].num_seg
			= tso->tso_info.tso_packet_info[inx].num_seg;
		data->pkt_info[inx].tso_packet_len
			= tso->tso_info.tso_packet_info[inx].tso_packet_len;
		data->pkt_info[inx].tso_seg_idx
			= tso->tso_info.tso_packet_info[inx].tso_seg_idx;
	}
}
#else
static void fill_debug_pdev_data_tso_info(struct debug_pdev_data_tso *data,
					  struct cdp_tso_stats *tso)
{
}
#endif /* FEATURE_TSO_STATS */

static QDF_STATUS get_debug_pdev_data_tso(struct unified_stats *stats,
					  struct cdp_pdev_stats *pdev_stats)
{
	struct debug_pdev_data_tso *data = NULL;
	struct cdp_tso_stats *tso = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	tso = &pdev_stats->tso_stats;
	data = qdf_mem_malloc(sizeof(struct debug_pdev_data_tso));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->dropped_host.num = tso->dropped_host.num;
	data->dropped_host.bytes = tso->dropped_host.bytes;
	data->tso_no_mem_dropped.num = tso->tso_no_mem_dropped.num;
	data->tso_no_mem_dropped.bytes = tso->tso_no_mem_dropped.bytes;
	data->dropped_target = tso->dropped_target;
	fill_debug_pdev_data_tso_info(data, tso);

	stats->feat[INX_FEAT_TSO] = data;
	stats->size[INX_FEAT_TSO] = sizeof(struct debug_pdev_data_tso);

	return QDF_STATUS_SUCCESS;
}

#if defined(WLAN_CFR_ENABLE) && defined(WLAN_ENH_CFR_ENABLE)
static QDF_STATUS get_debug_pdev_data_cfr(struct unified_stats *stats,
					  struct cdp_pdev_stats *pdev_stats)
{
	struct debug_pdev_data_cfr *data = NULL;
	uint8_t inx;
	uint8_t loop_cnt;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_pdev_data_cfr));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->bb_captured_channel_cnt = pdev_stats->rcc.bb_captured_channel_cnt;
	data->bb_captured_timeout_cnt = pdev_stats->rcc.bb_captured_timeout_cnt;
	data->rx_loc_info_valid_cnt = pdev_stats->rcc.rx_loc_info_valid_cnt;
	loop_cnt = qdf_min((uint8_t)CAPTURE_MAX, (uint8_t)STATS_IF_CAPTURE_MAX);
	for (inx = 0; inx < loop_cnt; inx++)
		data->chan_capture_status[inx] =
				pdev_stats->rcc.chan_capture_status[inx];
	loop_cnt = qdf_min((uint8_t)FREEZE_REASON_MAX,
			   (uint8_t)STATS_IF_FREEZE_REASON_MAX);
	for (inx = 0; inx < loop_cnt; inx++)
		data->reason_cnt[inx] = pdev_stats->rcc.reason_cnt[inx];

	stats->feat[INX_FEAT_CFR] = data;
	stats->size[INX_FEAT_CFR] = sizeof(struct debug_pdev_data_cfr);

	return QDF_STATUS_SUCCESS;
}
#else
static QDF_STATUS get_debug_pdev_data_cfr(struct unified_stats *stats,
					  struct cdp_pdev_stats *pdev_stats)
{
	return QDF_STATUS_SUCCESS;
}
#endif

static QDF_STATUS get_debug_pdev_data_wdi(struct unified_stats *stats,
					  struct cdp_pdev_stats *pdev_stats)
{
	struct debug_pdev_data_wdi *data = NULL;
	uint8_t inx;
	uint16_t loop_cnt;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_pdev_data_wdi));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	loop_cnt = qdf_min((uint16_t)(WDI_NUM_EVENTS),
			   (uint16_t)(STATS_IF_WDI_EVENT_LAST));
	for (inx = 0; inx < loop_cnt; inx++)
		data->wdi_event[inx] = pdev_stats->wdi_event[inx];

	stats->feat[INX_FEAT_WDI] = data;
	stats->size[INX_FEAT_WDI] = sizeof(struct debug_pdev_data_wdi);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_pdev_data_mesh(struct unified_stats *stats,
					   struct cdp_pdev_stats *pdev_stats)
{
	struct debug_pdev_data_mesh *data = NULL;

	if (!stats || !pdev_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_pdev_data_mesh));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->mesh_mem_alloc = pdev_stats->mesh_mem_alloc;

	stats->feat[INX_FEAT_MESH] = data;
	stats->size[INX_FEAT_MESH] = sizeof(struct debug_pdev_data_mesh);

	return QDF_STATUS_SUCCESS;
}

static void get_debug_pdev_data_txcap_2_0(struct debug_pdev_data_txcap *data,
					 struct cdp_pdev_tx_capture_stats *cap)
{
	data->stats_2_0.ppdu_id = cap->ppdu_id;
	data->stats_2_0.mode = cap->mode;
	data->stats_2_0.ppdu_drop_cnt = cap->ppdu_drop_cnt;
	data->stats_2_0.mpdu_drop_cnt = cap->mpdu_drop_cnt;
	data->stats_2_0.tlv_drop_cnt = cap->tlv_drop_cnt;
	data->stats_2_0.pkt_buf_recv = cap->pkt_buf_recv;
	data->stats_2_0.pkt_buf_free = cap->pkt_buf_free;
	data->stats_2_0.pkt_buf_processed = cap->pkt_buf_processed;
	data->stats_2_0.pkt_buf_to_stack = cap->pkt_buf_to_stack;
	data->stats_2_0.status_buf_recv = cap->status_buf_recv;
	data->stats_2_0.status_buf_free = cap->status_buf_free;
	data->stats_2_0.totat_tx_mon_replenish_cnt =
				cap->totat_tx_mon_replenish_cnt;
	data->stats_2_0.total_tx_mon_reap_cnt =
				cap->total_tx_mon_reap_cnt;
	data->stats_2_0.tx_mon_stuck = cap->tx_mon_stuck;
	data->stats_2_0.total_tx_mon_stuck = cap->total_tx_mon_stuck;
	data->stats_2_0.ppdu_info_drop_th = cap->ppdu_info_drop_th;
	data->stats_2_0.ppdu_info_drop_flush =
				cap->ppdu_info_drop_flush;
	data->stats_2_0.ppdu_info_drop_trunc =
				cap->ppdu_info_drop_trunc;
	data->stats_2_0.ppdu_drop_sw_filter = cap->ppdu_drop_sw_filter;
	data->stats_2_0.dp_tx_pkt_cap_stats[STATS_IF_TX_PKT_TYPE_ARP] =
		cap->dp_tx_pkt_cap_stats[TX_PKT_TYPE_ARP];
	data->stats_2_0.dp_tx_pkt_cap_stats[STATS_IF_TX_PKT_TYPE_EAPOL] =
		cap->dp_tx_pkt_cap_stats[TX_PKT_TYPE_EAPOL];
	data->stats_2_0.dp_tx_pkt_cap_stats[STATS_IF_TX_PKT_TYPE_DHCP] =
		cap->dp_tx_pkt_cap_stats[TX_PKT_TYPE_DHCP];
	data->stats_2_0.dp_tx_pkt_cap_stats[STATS_IF_TX_PKT_TYPE_ICMP] =
		cap->dp_tx_pkt_cap_stats[TX_PKT_TYPE_ICMP];
	data->stats_2_0.dp_tx_pkt_cap_stats[STATS_IF_TX_PKT_TYPE_DNS] =
		cap->dp_tx_pkt_cap_stats[TX_PKT_TYPE_DNS];
}

static QDF_STATUS
get_debug_pdev_data_txcap(struct unified_stats *stats,
			  struct cdp_pdev_stats *pdev_stats,
			  struct cdp_pdev_tx_capture_stats *cap,
			  uint8_t monitor_version)
{
	struct debug_pdev_data_txcap *data = NULL;
	uint8_t inx, j;
	uint8_t loop_cnt, max_type, max_subtype;

	if (!stats || !cap) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_pdev_data_txcap));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}

	if (monitor_version == STATS_IF_TX_MON_VER_2) {
		get_debug_pdev_data_txcap_2_0(data, cap);
		data->monitor_version = monitor_version;
		goto done;
	}

	data->delayed_ba_not_recev = pdev_stats->cdp_delayed_ba_not_recev;
	data->tx_ppdu_proc = pdev_stats->tx_ppdu_proc;
	data->ack_ba_comes_twice = pdev_stats->ack_ba_comes_twice;
	data->ppdu_drop = pdev_stats->ppdu_drop;
	data->ppdu_wrap_drop = pdev_stats->ppdu_wrap_drop;
	data->last_rcv_ppdu = cap->last_rcv_ppdu;
	data->ppdu_stats_queue_depth = cap->ppdu_stats_queue_depth;
	data->ppdu_stats_defer_queue_depth = cap->ppdu_stats_defer_queue_depth;
	data->ppdu_dropped = cap->ppdu_dropped;
	data->pend_ppdu_dropped = cap->pend_ppdu_dropped;
	data->ppdu_flush_count = cap->ppdu_flush_count;
	data->msdu_threshold_drop = cap->msdu_threshold_drop;
	data->peer_mismatch = cap->peer_mismatch;
	data->defer_msdu_len = cap->len_stats.defer_msdu_len;
	data->tasklet_msdu_len = cap->len_stats.tasklet_msdu_len;
	data->pending_q_len = cap->len_stats.pending_q_len;
	loop_cnt = qdf_min((uint8_t)CDP_PPDU_STATS_MAX_TAG,
			   (uint8_t)STATS_IF_PPDU_STATS_MAX_TAG);
	for (inx = 0; inx < loop_cnt; inx++)
		data->ppdu_stats_counter[inx] =
					pdev_stats->ppdu_stats_counter[inx];
	loop_cnt = qdf_min((uint8_t)CDP_TX_CAP_HTT_MAX_FTYPE,
			   (uint8_t)STATS_IF_TX_CAP_HTT_MAX_FTYPE);
	for (inx = 0; inx < loop_cnt; inx++)
		data->htt_frame_type[inx] = cap->htt_frame_type[inx];
	max_type = qdf_min((uint8_t)CDP_TXCAP_MAX_TYPE,
			   (uint8_t)STATS_IF_TXCAP_MAX_TYPE);
	max_subtype = qdf_min((uint8_t)CDP_TXCAP_MAX_SUBTYPE,
			      (uint8_t)STATS_IF_TXCAP_MAX_SUBTYPE);
	for (inx = 0; inx < max_type; inx++) {
		for (j = 0; j < max_subtype; j++) {
			data->ctl_mgmt_q_len[inx][j] =
						cap->ctl_mgmt_q_len[inx][j];
			data->retries_ctl_mgmt_q_len[inx][j] =
					cap->retries_ctl_mgmt_q_len[inx][j];
		}
	}

done:
	stats->feat[INX_FEAT_TXCAP] = data;
	stats->size[INX_FEAT_TXCAP] = sizeof(struct debug_pdev_data_txcap);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_pdev_data_monitor(struct unified_stats *stats,
					      struct cdp_pdev_stats *pdev_stats,
					      struct cdp_pdev_mon_stats *mon)
{
	struct debug_pdev_data_monitor *data = NULL;
	uint8_t inx;
	uint8_t loop_cnt;

	if (!stats || !pdev_stats || !mon) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_pdev_data_monitor));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	loop_cnt = qdf_min((uint8_t)OFDMA_NUM_RU_SIZE,
			   (uint8_t)STATS_IF_OFDMA_NUM_RU_SIZE);
	for (inx = 0; inx < loop_cnt; inx++) {
		data->data_rx_ru_size[inx] =
				pdev_stats->ul_ofdma.data_rx_ru_size[inx];
		data->nondata_rx_ru_size[inx] =
				pdev_stats->ul_ofdma.nondata_rx_ru_size[inx];
	}
	data->data_rx_ppdu = pdev_stats->ul_ofdma.data_rx_ppdu;
	loop_cnt = qdf_min((uint8_t)OFDMA_NUM_USERS,
			   (uint8_t)STATS_IF_OFDMA_NUM_USERS);
	for (inx = 0; inx < loop_cnt; inx++)
		data->data_users[inx] = pdev_stats->ul_ofdma.data_users[inx];
	data->status_ppdu_state = mon->status_ppdu_state;
	data->status_ppdu_start = mon->status_ppdu_start;
	data->status_ppdu_end = mon->status_ppdu_end;
	data->status_ppdu_compl = mon->status_ppdu_compl;
	data->status_ppdu_start_mis = mon->status_ppdu_start_mis;
	data->status_ppdu_end_mis = mon->status_ppdu_end_mis;
	data->status_ppdu_done = mon->status_ppdu_done;
	data->dest_ppdu_done = mon->dest_ppdu_done;
	data->dest_mpdu_done = mon->dest_mpdu_done;
	data->dest_mpdu_drop = mon->dest_mpdu_drop;
	data->dup_mon_linkdesc_cnt = mon->dup_mon_linkdesc_cnt;
	data->dup_mon_buf_cnt = mon->dup_mon_buf_cnt;
	data->ppdu_id_hist_idx = mon->ppdu_id_hist_idx;
	data->mon_rx_dest_stuck = mon->mon_rx_dest_stuck;
	data->tlv_tag_status_err = mon->tlv_tag_status_err;
	data->status_buf_done_war = mon->status_buf_done_war;
	data->mon_rx_bufs_replenished_dest = mon->mon_rx_bufs_replenished_dest;
	data->mon_rx_bufs_reaped_dest = mon->mon_rx_bufs_reaped_dest;
	data->ppdu_id_mismatch = mon->ppdu_id_mismatch;
	data->ppdu_id_match = mon->ppdu_id_match;
	data->status_ppdu_drop = mon->status_ppdu_drop;
	data->dest_ppdu_drop = mon->dest_ppdu_drop;
	data->mon_link_desc_invalid = mon->mon_link_desc_invalid;
	data->mon_rx_desc_invalid = mon->mon_rx_desc_invalid;
	data->mon_nbuf_sanity_err = mon->mon_nbuf_sanity_err;
	data->mpdu_ppdu_id_mismatch_drop = mon->mpdu_ppdu_id_mismatch_drop;
	loop_cnt = qdf_min((uint8_t)MAX_PPDU_ID_HIST,
			   (uint8_t)STATS_IF_MAX_PPDU_ID_HIST);
	for (inx = 0; inx < loop_cnt; inx++) {
		data->stat_ring_ppdu_id_hist[inx] =
					mon->stat_ring_ppdu_id_hist[inx];
		data->dest_ring_ppdu_id_hist[inx] =
					mon->dest_ring_ppdu_id_hist[inx];
	}

	stats->feat[INX_FEAT_MONITOR] = data;
	stats->size[INX_FEAT_MONITOR] = sizeof(struct debug_pdev_data_monitor);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_pdev_ctrl_tx(struct unified_stats *stats,
					 struct pdev_ic_cp_stats *cp_stats)
{
	struct debug_pdev_ctrl_tx *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct debug_pdev_ctrl_tx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_ctrl_tx(&ctrl->b_tx, cp_stats);
	ctrl->cs_be_nobuf = cp_stats->stats.cs_be_nobuf;
	ctrl->cs_tx_buf_count = cp_stats->stats.cs_tx_buf_count;
	ctrl->cs_ap_stats_tx_cal_enable =
				cp_stats->stats.cs_ap_stats_tx_cal_enable;
	ctrl->cs_tx_hw_retries = cp_stats->stats.hw_stats.tx_hw_retries;
	ctrl->cs_tx_hw_failures = cp_stats->stats.hw_stats.tx_hw_failures;

	stats->feat[INX_FEAT_TX] = ctrl;
	stats->size[INX_FEAT_TX] = sizeof(struct debug_pdev_ctrl_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_pdev_ctrl_rx(struct unified_stats *stats,
					 struct pdev_ic_cp_stats *cp_stats)
{
	struct debug_pdev_ctrl_rx *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct debug_pdev_ctrl_rx));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_ctrl_rx(&ctrl->b_rx, cp_stats);
	ctrl->cs_rx_rts_success = cp_stats->stats.cs_rx_rts_success;
	ctrl->cs_rx_clear_count = cp_stats->stats.cs_rx_clear_count;
	ctrl->cs_rx_overrun = cp_stats->stats.cs_rx_overrun;
	ctrl->cs_rx_phy_err = cp_stats->stats.cs_rx_phy_err;
	ctrl->cs_rx_ack_err = cp_stats->stats.cs_rx_ack_err;
	ctrl->cs_rx_rts_err = cp_stats->stats.cs_rx_rts_err;
	ctrl->cs_no_beacons = cp_stats->stats.cs_no_beacons;
	ctrl->cs_phy_err_count = cp_stats->stats.cs_phy_err_count;
	ctrl->cs_fcsbad = cp_stats->stats.cs_fcsbad;
	ctrl->cs_rx_looplimit_start = cp_stats->stats.cs_rx_looplimit_start;
	ctrl->cs_rx_looplimit_end = cp_stats->stats.cs_rx_looplimit_end;

	stats->feat[INX_FEAT_RX] = ctrl;
	stats->size[INX_FEAT_RX] = sizeof(struct debug_pdev_ctrl_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_pdev_ctrl_wmi(struct unified_stats *stats,
					  struct pdev_ic_cp_stats *cp_stats)
{
	struct debug_pdev_ctrl_wmi *ctrl = NULL;

	if (!stats || !cp_stats) {
		qdf_err("Invalid Input");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct debug_pdev_ctrl_wmi));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	ctrl->cs_wmi_tx_mgmt = cp_stats->stats.cs_wmi_tx_mgmt;
	ctrl->cs_wmi_tx_mgmt_completions =
				cp_stats->stats.cs_wmi_tx_mgmt_completions;
	ctrl->cs_wmi_tx_mgmt_completion_err =
				cp_stats->stats.cs_wmi_tx_mgmt_completion_err;

	stats->feat[INX_FEAT_WMI] = ctrl;
	stats->size[INX_FEAT_WMI] = sizeof(struct debug_pdev_ctrl_wmi);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_pdev_ctrl_link(struct unified_stats *stats,
					   struct pdev_ic_cp_stats *cp_stats,
					   struct wlan_objmgr_pdev *pdev)
{
	struct debug_pdev_ctrl_link *ctrl = NULL;

	if (!stats || !cp_stats || !pdev) {
		qdf_err("Invalid Input");
		return QDF_STATUS_E_INVAL;
	}
	ctrl = qdf_mem_malloc(sizeof(struct debug_pdev_ctrl_link));
	if (!ctrl) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_pdev_ctrl_link(&ctrl->b_link, cp_stats, pdev);

	stats->feat[INX_FEAT_LINK] = ctrl;
	stats->size[INX_FEAT_LINK] = sizeof(struct debug_pdev_ctrl_link);

	return QDF_STATUS_SUCCESS;
}

#ifdef WLAN_CONFIG_TELEMETRY_AGENT
static QDF_STATUS
get_debug_pdev_data_wmm(struct unified_stats *stats,
			struct cdp_pdev_telemetry_stats *cdp_wmm)
{
	struct debug_pdev_data_wmm *data = NULL;

	if (!stats || !cdp_wmm) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}

	data = qdf_mem_malloc(sizeof(struct debug_pdev_data_wmm));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}

	qdf_mem_copy(data->tx_mpdu_failed,
		     cdp_wmm->tx_mpdu_failed,
		     sizeof(data->tx_mpdu_failed));
	qdf_mem_copy(data->tx_mpdu_total,
		     cdp_wmm->tx_mpdu_total,
		     sizeof(data->tx_mpdu_total));
	qdf_mem_copy(data->link_airtime,
		     cdp_wmm->link_airtime,
		     sizeof(data->link_airtime));

	stats->feat[INX_FEAT_WMM] = data;
	stats->size[INX_FEAT_WMM] = sizeof(struct debug_pdev_data_wmm);

	return QDF_STATUS_SUCCESS;
}
#endif

#ifdef WLAN_CONFIG_TELEMETRY_AGENT
static QDF_STATUS
get_debug_pdev_deter_stats(struct unified_stats *stats,
			   struct cdp_pdev_deter_stats *deter)
{
	struct debug_pdev_data_deter *data = NULL;

	if (!stats || !deter) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_pdev_data_deter));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}

	qdf_mem_copy(data->dl_ofdma_usr,
		     deter->dl_ofdma_usr,
		     sizeof(data->dl_ofdma_usr));
	qdf_mem_copy(data->ul_ofdma_usr,
		     deter->ul_ofdma_usr,
		     sizeof(data->ul_ofdma_usr));
	qdf_mem_copy(data->dl_mimo_usr,
		     deter->dl_mimo_usr,
		     sizeof(data->dl_mimo_usr));
	qdf_mem_copy(data->ul_mimo_usr,
		     deter->ul_mimo_usr,
		     sizeof(data->ul_mimo_usr));

	qdf_mem_copy(data->ul_mode_cnt,
		     deter->ul_mode_cnt,
		     sizeof(data->ul_mode_cnt));
	qdf_mem_copy(data->dl_mode_cnt,
		     deter->dl_mode_cnt,
		     sizeof(data->dl_mode_cnt));
	qdf_mem_copy(data->ch_access_delay,
		     deter->ch_access_delay,
		     sizeof(data->ch_access_delay));

	qdf_mem_copy(data->ts,
		     deter->ts,
		     sizeof(data->ts));

	data->ch_util.ap_tx_util = deter->ch_util.ap_tx_util;
	data->ch_util.ap_rx_util = deter->ch_util.ap_rx_util;
	data->ch_util.ap_chan_util = deter->ch_util.ap_chan_util;

	data->rx_su_cnt = deter->rx_su_cnt;

	stats->feat[INX_FEAT_DETER] = data;
	stats->size[INX_FEAT_DETER] = sizeof(struct debug_pdev_data_deter);

	return QDF_STATUS_SUCCESS;
}
#else
static QDF_STATUS
get_debug_pdev_deter_stats(struct unified_stats *stats,
			   struct cdp_pdev_deter_stats *deter)
{
	return QDF_STATUS_E_FAILURE;
}
#endif

static void aggregate_debug_pdev_ctrl_tx(struct debug_pdev_ctrl_tx *tx,
					 struct vdev_ic_cp_stats *cp_stats)
{
	aggregate_basic_pdev_ctrl_tx(&tx->b_tx, cp_stats);
}

static void aggregate_debug_pdev_ctrl_rx(struct debug_pdev_ctrl_rx *rx,
					 struct vdev_ic_cp_stats *cp_stats)
{
	aggregate_basic_pdev_ctrl_rx(&rx->b_rx, cp_stats);
}

static void aggr_debug_pdev_ctrl_stats(struct wlan_objmgr_pdev *pdev,
				       void *object, void *arg)
{
	struct wlan_objmgr_vdev *vdev = object;
	struct iterator_ctx *ctx = arg;
	struct unified_stats *stats;
	struct vdev_ic_cp_stats *cp_stats;

	if (!vdev || !ctx || !ctx->pvt || !ctx->stats)
		return;
	if (wlan_vdev_mlme_get_opmode(vdev) != QDF_SAP_MODE)
		return;
	cp_stats = ctx->pvt;
	stats = ctx->stats;
	qdf_mem_zero(cp_stats, sizeof(struct vdev_ic_cp_stats));
	get_vdev_cp_stats(vdev, cp_stats);

	if (stats->feat[INX_FEAT_TX])
		aggregate_debug_pdev_ctrl_tx(stats->feat[INX_FEAT_TX],
					     cp_stats);
	if (stats->feat[INX_FEAT_RX])
		aggregate_debug_pdev_ctrl_rx(stats->feat[INX_FEAT_RX],
					     cp_stats);
}

static void aggregate_debug_pdev_stats(struct wlan_objmgr_pdev *pdev,
				       struct unified_stats *stats,
				       enum stats_type_e type)
{
	struct iterator_ctx ctx = {0};
	struct vdev_ic_cp_stats *cp_stats = NULL;

	switch (type) {
	case STATS_TYPE_DATA:
		break;
	case STATS_TYPE_CTRL:
		cp_stats = qdf_mem_malloc(sizeof(struct vdev_ic_cp_stats));
		if (!cp_stats) {
			qdf_debug("Allocation Failed!");
			return;
		}
		ctx.pvt = cp_stats;
		ctx.stats = stats;
		wlan_objmgr_pdev_iterate_obj_list(pdev, WLAN_VDEV_OP,
						  aggr_debug_pdev_ctrl_stats,
						  &ctx, 1, WLAN_MLME_SB_ID);
		break;
	default:
		qdf_err("Invalid type %d!", type);
	}

	if (cp_stats)
		qdf_mem_free(cp_stats);
}

static QDF_STATUS get_debug_pdev_data(struct wlan_objmgr_psoc *psoc,
				      struct wlan_objmgr_pdev *pdev,
				      struct unified_stats *stats,
				      uint32_t feat)
{
	struct cdp_pdev_stats *pdev_stats = NULL;
	struct cdp_pdev_mon_stats *mon = NULL;
	struct cdp_pdev_tx_capture_stats *cap = NULL;
	struct cdp_pdev_deter_stats *deter = NULL;
	struct cdp_pdev_telemetry_stats *wmm = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	void *dp_soc = NULL;
	uint8_t pdev_id;
	bool stats_collected = false;
	uint8_t monitor_version = 0;

	if (!psoc || !pdev) {
		qdf_err("Invalid pdev and psoc!");
		return QDF_STATUS_E_INVAL;
	}

	pdev_stats = qdf_mem_malloc(sizeof(struct cdp_pdev_stats));
	if (!pdev_stats) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}

	dp_soc = wlan_psoc_get_dp_handle(psoc);
	pdev_id = wlan_objmgr_pdev_get_pdev_id(pdev);
	ret = cdp_host_get_pdev_stats(dp_soc, pdev_id, pdev_stats);
	if (ret != QDF_STATUS_SUCCESS) {
		qdf_err("Unable to get Pdev stats!");
		goto get_failed;
	}

	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_debug_pdev_data_tx(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Debug TX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_debug_pdev_data_rx(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Debug RX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_ME) {
		ret = get_debug_pdev_data_me(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Debug ME Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RAW) {
		ret = get_debug_pdev_data_raw(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Debug RAW Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_TSO) {
		ret = get_debug_pdev_data_tso(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Debug TSO Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_WDI) {
		ret = get_debug_pdev_data_wdi(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Debug WDI Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_CFR) {
		ret = get_debug_pdev_data_cfr(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Debug CFR Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_MESH) {
		ret = get_debug_pdev_data_mesh(stats, pdev_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Debug MESH Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_TXCAP) {
		cap = qdf_mem_malloc(sizeof(struct cdp_pdev_tx_capture_stats));
		if (!cap) {
			ret = QDF_STATUS_E_NOMEM;
			goto get_failed;
		}
		ret = get_pdev_tx_capture_stats(dp_soc, pdev_id, cap);
		monitor_version = get_monitor_version(pdev);
		if (ret == QDF_STATUS_SUCCESS)
			ret = get_debug_pdev_data_txcap(stats, pdev_stats, cap,
							monitor_version);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Debug TXCAP Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_MONITOR) {
		mon = qdf_mem_malloc(sizeof(struct cdp_pdev_mon_stats));
		if (!mon) {
			ret = QDF_STATUS_E_NOMEM;
			goto get_failed;
		}
		ret = cdp_mon_pdev_get_rx_stats(dp_soc, pdev_id, mon);
		if (ret == QDF_STATUS_SUCCESS)
			ret = get_debug_pdev_data_monitor(stats,
							  pdev_stats, mon);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Debug MONITOR Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_DETER) {
		deter = qdf_mem_malloc(sizeof(struct cdp_pdev_deter_stats));
		if (!deter) {
			ret = QDF_STATUS_E_NOMEM;
			goto get_failed;
		}
		ret = get_pdev_deter_stats(dp_soc, pdev_id, deter);
		if (ret == QDF_STATUS_SUCCESS)
			ret = get_debug_pdev_deter_stats(stats, deter);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Debug deter Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_WMM) {
		wmm =
			qdf_mem_malloc(sizeof(struct cdp_pdev_telemetry_stats));
		if (!wmm) {
			ret = QDF_STATUS_E_NOMEM;
			goto get_failed;
		}
		ret = cdp_get_pdev_telemetry_stats(dp_soc, pdev_id,
						   wmm);
		if (ret == QDF_STATUS_SUCCESS)
			ret = get_debug_pdev_data_wmm(stats, wmm);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Debug TELEMETRY Stats!");
		else
			stats_collected = true;
	}
get_failed:
	if (deter)
		qdf_mem_free(deter);
	if (cap)
		qdf_mem_free(cap);
	if (mon)
		qdf_mem_free(mon);
	if (wmm)
		qdf_mem_free(wmm);
	qdf_mem_free(pdev_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_debug_pdev_ctrl(struct wlan_objmgr_pdev *pdev,
				      struct unified_stats *stats,
				      uint32_t feat, bool aggregate)
{
	struct pdev_ic_cp_stats *pdev_cp_stats = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	bool stats_collected = false;
	bool aggr_required = false;

	if (!pdev) {
		qdf_err("Invalid pdev!");
		return QDF_STATUS_E_INVAL;
	}
	pdev_cp_stats = qdf_mem_malloc(sizeof(struct pdev_ic_cp_stats));
	if (!pdev_cp_stats) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}

	ret = wlan_cfg80211_get_pdev_cp_stats(pdev, pdev_cp_stats);
	if (QDF_STATUS_SUCCESS != ret) {
		qdf_err("Unbale to get pdev control stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_debug_pdev_ctrl_tx(stats, pdev_cp_stats);
		if (ret != QDF_STATUS_SUCCESS) {
			qdf_err("Unable to fetch pdev Debug TX Stats!");
		} else {
			stats_collected = true;
			aggr_required =  aggregate;
		}
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_debug_pdev_ctrl_rx(stats, pdev_cp_stats);
		if (ret != QDF_STATUS_SUCCESS) {
			qdf_err("Unable to fetch pdev Debug RX Stats!");
		} else {
			stats_collected = true;
			aggr_required =  aggregate;
		}
	}
	if (feat & STATS_FEAT_FLG_WMI) {
		ret = get_debug_pdev_ctrl_wmi(stats, pdev_cp_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Debug WMI Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_LINK) {
		ret = get_debug_pdev_ctrl_link(stats, pdev_cp_stats, pdev);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch pdev Debug LINK Stats!");
		else
			stats_collected = true;
	}

	if (aggr_required)
		aggregate_debug_pdev_stats(pdev, stats, STATS_TYPE_CTRL);

get_failed:
	qdf_mem_free(pdev_cp_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}

static QDF_STATUS get_debug_psoc_data_tx(struct unified_stats *stats,
					 struct cdp_soc_stats *soc_stats)
{
	struct debug_psoc_data_tx *data = NULL;
	uint8_t inx;
	uint8_t loop_cnt;

	if (!stats || !soc_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_psoc_data_tx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_psoc_data_tx(&data->b_tx, soc_stats);
	data->tx_invalid_peer.num = soc_stats->tx.tx_invalid_peer.num;
	data->tx_invalid_peer.bytes = soc_stats->tx.tx_invalid_peer.bytes;
	loop_cnt = qdf_min((uint8_t)CDP_MAX_TX_DATA_RINGS,
			   (uint8_t)STATS_IF_MAX_TX_DATA_RINGS);
	for (inx = 0; inx < loop_cnt; inx++) {
		data->tx_hw_enq[inx] = soc_stats->tx.tx_hw_enq[inx];
		data->tx_hw_ring_full[inx] = soc_stats->tx.tx_hw_ring_full[inx];
	}
	loop_cnt = qdf_min((uint8_t)CDP_MAX_WIFI_INT_ERROR_REASONS,
			   (uint8_t)STATS_IF_MAX_WIFI_INT_ERROR_REASONS);
	for (inx = 0; inx < loop_cnt; inx++)
		data->wifi_internal_error[inx] =
					soc_stats->tx.wifi_internal_error[inx];
	data->desc_in_use = soc_stats->tx.desc_in_use;
	data->dropped_fw_removed = soc_stats->tx.dropped_fw_removed;
	data->invalid_release_source = soc_stats->tx.invalid_release_source;
	data->non_wifi_internal_err = soc_stats->tx.non_wifi_internal_err;
	data->tx_comp_loop_pkt_limit_hit =
				soc_stats->tx.tx_comp_loop_pkt_limit_hit;
	data->hp_oos2 = soc_stats->tx.hp_oos2;
	data->tx_comp_exception = soc_stats->tx.tx_comp_exception;

	stats->feat[INX_FEAT_TX] = data;
	stats->size[INX_FEAT_TX] = sizeof(struct debug_psoc_data_tx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_psoc_data_rx(struct unified_stats *stats,
					 struct cdp_soc_stats *soc_stats)
{
	struct debug_psoc_data_rx *data = NULL;
	uint8_t cpus;
	uint8_t loop_cnt;
	uint8_t inx;

	if (!stats || !soc_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_psoc_data_rx));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	fill_basic_psoc_data_rx(&data->b_rx, soc_stats);
	data->rx_packets.num_cpus = soc_stats->rx.rx_packets.num_cpus;
	if (data->rx_packets.num_cpus > STATS_IF_NR_CPUS)
		data->rx_packets.num_cpus = STATS_IF_NR_CPUS;
	loop_cnt = qdf_min((uint8_t)CDP_MAX_RX_DEST_RINGS,
			   (uint8_t)STATS_IF_MAX_RX_DEST_RINGS);
	for (cpus = 0; cpus < data->rx_packets.num_cpus; cpus++)
		for (inx = 0; inx < loop_cnt; inx++)
			data->rx_packets.pkts[cpus][inx] =
				soc_stats->rx.rx_packets.pkts[cpus][inx];

	data->rx_invalid_peer.num = soc_stats->rx.err.rx_invalid_peer.num;
	data->rx_invalid_peer.bytes = soc_stats->rx.err.rx_invalid_peer.bytes;
	data->rx_invalid_peer_id.num =
				soc_stats->rx.err.rx_invalid_peer_id.num;
	data->rx_invalid_peer_id.bytes =
				soc_stats->rx.err.rx_invalid_peer_id.bytes;
	data->rx_invalid_pkt_len.num = soc_stats->rx.err.rx_invalid_pkt_len.num;
	data->rx_invalid_pkt_len.bytes =
				soc_stats->rx.err.rx_invalid_pkt_len.bytes;
	data->rx_frag_err_len_error = soc_stats->rx.rx_frag_err_len_error;
	data->rx_frag_err_no_peer = soc_stats->rx.rx_frag_err_no_peer;
	data->rx_frag_wait = soc_stats->rx.rx_frag_wait;
	data->rx_frag_err = soc_stats->rx.rx_frag_err;
	data->rx_frag_oor = soc_stats->rx.rx_frag_oor;
	data->reap_loop_pkt_limit_hit = soc_stats->rx.reap_loop_pkt_limit_hit;
	data->hp_oos2 = soc_stats->rx.hp_oos2;
	data->near_full = soc_stats->rx.near_full;
	data->msdu_scatter_wait_break = soc_stats->rx.msdu_scatter_wait_break;
	data->rx_sw_route_drop = soc_stats->rx.rx_sw_route_drop;
	data->rx_hw_route_drop = soc_stats->rx.rx_hw_route_drop;
	data->phy_ring_access_fail = soc_stats->rx.err.phy_ring_access_fail;
	data->phy_ring_access_full_fail =
				soc_stats->rx.err.phy_ring_access_full_fail;
	loop_cnt = qdf_min((uint8_t)CDP_MAX_RX_DEST_RINGS,
			   (uint8_t)STATS_IF_MAX_RX_DEST_RINGS);
	for (inx = 0; inx < loop_cnt; inx++)
		data->phy_rx_hw_error[inx] =
					soc_stats->rx.err.phy_rx_hw_error[inx];
	data->phy_rx_hw_dest_dup = soc_stats->rx.err.phy_rx_hw_dest_dup;
	data->phy_wifi_rel_dup = soc_stats->rx.err.phy_wifi_rel_dup;
	data->phy_rx_sw_err_dup = soc_stats->rx.err.phy_rx_sw_err_dup;
	data->invalid_rbm = soc_stats->rx.err.invalid_rbm;
	data->invalid_vdev = soc_stats->rx.err.invalid_vdev;
	data->invalid_pdev = soc_stats->rx.err.invalid_pdev;
	data->pkt_delivered_no_peer = soc_stats->rx.err.pkt_delivered_no_peer;
	data->defrag_peer_uninit = soc_stats->rx.err.defrag_peer_uninit;
	data->invalid_sa_da_idx = soc_stats->rx.err.invalid_sa_da_idx;
	data->msdu_done_fail = soc_stats->rx.err.msdu_done_fail;
	loop_cnt = qdf_min((uint8_t)CDP_WIFI_ERR_MAX,
			   (uint8_t)STATS_IF_WIFI_ERR_MAX);
	for (inx = 0; inx < loop_cnt; inx++)
		data->rx_sw_error[inx] = soc_stats->rx.err.rx_sw_error[inx];
	loop_cnt = qdf_min((uint8_t)CDP_RX_ERR_MAX,
			   (uint8_t)STATS_IF_RX_ERR_MAX);
	for (inx = 0; inx < loop_cnt; inx++)
		data->rx_hw_error[inx] = soc_stats->rx.err.rx_hw_error[inx];
	data->rx_desc_invalid_magic = soc_stats->rx.err.rx_desc_invalid_magic;
	data->rx_hw_cmd_send_fail = soc_stats->rx.err.rx_hw_cmd_send_fail;
	data->rx_hw_cmd_send_drain = soc_stats->rx.err.rx_hw_cmd_send_drain;
	data->scatter_msdu = soc_stats->rx.err.scatter_msdu;
	data->invalid_cookie = soc_stats->rx.err.invalid_cookie;
	data->stale_cookie = soc_stats->rx.err.stale_cookie;
	data->rx_2k_jump_delba_sent = soc_stats->rx.err.rx_2k_jump_delba_sent;
	data->rx_2k_jump_to_stack = soc_stats->rx.err.rx_2k_jump_to_stack;
	data->rx_2k_jump_drop = soc_stats->rx.err.rx_2k_jump_drop;
	data->rx_hw_err_msdu_buf_rcved =
				soc_stats->rx.err.rx_hw_err_msdu_buf_rcved;
	data->rx_hw_err_msdu_buf_invalid_cookie =
			soc_stats->rx.err.rx_hw_err_msdu_buf_invalid_cookie;
	data->rx_hw_err_oor_drop = soc_stats->rx.err.rx_hw_err_oor_drop;
	data->rx_hw_err_oor_to_stack = soc_stats->rx.err.rx_hw_err_oor_to_stack;
	data->rx_hw_err_oor_sg_count = soc_stats->rx.err.rx_hw_err_oor_sg_count;
	data->msdu_count_mismatch = soc_stats->rx.err.msdu_count_mismatch;
	data->invalid_link_cookie = soc_stats->rx.err.invalid_link_cookie;
	data->nbuf_sanity_fail = soc_stats->rx.err.nbuf_sanity_fail;
	data->dup_refill_link_desc = soc_stats->rx.err.dup_refill_link_desc;
	data->msdu_continuation_err = soc_stats->rx.err.msdu_continuation_err;
	data->ssn_update_count = soc_stats->rx.err.ssn_update_count;
	data->bar_handle_fail_count = soc_stats->rx.err.bar_handle_fail_count;
	data->intrabss_eapol_drop = soc_stats->rx.err.intrabss_eapol_drop;
	data->pn_in_dest_check_fail = soc_stats->rx.err.pn_in_dest_check_fail;
	data->msdu_len_err = soc_stats->rx.err.msdu_len_err;
	data->rx_flush_count = soc_stats->rx.err.rx_flush_count;

	stats->feat[INX_FEAT_RX] = data;
	stats->size[INX_FEAT_RX] = sizeof(struct debug_psoc_data_rx);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_psoc_data_ast(struct unified_stats *stats,
					  struct cdp_soc_stats *soc_stats)
{
	struct debug_psoc_data_ast *data = NULL;

	if (!stats || !soc_stats) {
		qdf_err("Invalid Input!");
		return QDF_STATUS_E_INVAL;
	}
	data = qdf_mem_malloc(sizeof(struct debug_psoc_data_ast));
	if (!data) {
		qdf_err("Allocation Failed!");
		return QDF_STATUS_E_NOMEM;
	}
	data->ast_added = soc_stats->ast.added;
	data->ast_deleted = soc_stats->ast.deleted;
	data->ast_aged_out = soc_stats->ast.aged_out;
	data->ast_map_err = soc_stats->ast.map_err;
	data->ast_mismatch = soc_stats->ast.ast_mismatch;
	data->mec_added = soc_stats->mec.added;
	data->mec_deleted = soc_stats->mec.deleted;

	stats->feat[INX_FEAT_AST] = data;
	stats->size[INX_FEAT_AST] = sizeof(struct debug_psoc_data_ast);

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS get_debug_psoc_data(struct wlan_objmgr_psoc *psoc,
				      struct unified_stats *stats,
				      uint32_t feat)
{
	struct cdp_soc_stats *psoc_stats = NULL;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	bool stats_collected = false;

	if (!psoc) {
		qdf_err("Invalid psoc!");
		return QDF_STATUS_E_INVAL;
	}
	psoc_stats = qdf_mem_malloc(sizeof(struct cdp_soc_stats));
	if (!psoc_stats) {
		qdf_err("Allocation failed");
		return QDF_STATUS_E_NOMEM;
	}
	ret = cdp_host_get_soc_stats(wlan_psoc_get_dp_handle(psoc),
				     psoc_stats);
	if (QDF_STATUS_SUCCESS != ret) {
		qdf_err("Unable to get Psoc stats!");
		goto get_failed;
	}
	if (feat & STATS_FEAT_FLG_TX) {
		ret = get_debug_psoc_data_tx(stats, psoc_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch psoc Debug TX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_RX) {
		ret = get_debug_psoc_data_rx(stats, psoc_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch psoc Debug RX Stats!");
		else
			stats_collected = true;
	}
	if (feat & STATS_FEAT_FLG_AST) {
		ret = get_debug_psoc_data_ast(stats, psoc_stats);
		if (ret != QDF_STATUS_SUCCESS)
			qdf_err("Unable to fetch psoc Debug AST Stats!");
		else
			stats_collected = true;
	}

get_failed:
	qdf_mem_free(psoc_stats);
	if (stats_collected)
		ret = QDF_STATUS_SUCCESS;

	return ret;
}
#endif /* WLAN_DEBUG_TELEMETRY */

/* Public APIs */
QDF_STATUS wlan_stats_get_peer_stats(struct wlan_objmgr_vdev *vdev,
				     uint8_t *peer_mac,
				     struct stats_config *cfg,
				     struct unified_stats *stats)
{
	QDF_STATUS ret = QDF_STATUS_SUCCESS;
	struct wlan_objmgr_psoc *psoc;

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc) {
		qdf_err("Invalid Psoc!");
		return QDF_STATUS_E_INVAL;
	}

	switch (cfg->lvl) {
	case STATS_LVL_BASIC:
		if (cfg->type == STATS_TYPE_DATA)
			ret = get_basic_peer_data(psoc, vdev, peer_mac, stats,
						  cfg->feat, cfg->peer_type);
		else
			ret = get_basic_peer_ctrl(psoc, peer_mac, stats,
						  cfg->feat);
		break;
#if WLAN_ADVANCE_TELEMETRY
	case STATS_LVL_ADVANCE:
		if (cfg->type == STATS_TYPE_DATA)
			ret = get_advance_peer_data(psoc, vdev, peer_mac, stats,
						    cfg);
		else
			ret = get_advance_peer_ctrl(psoc, peer_mac, stats,
						    cfg->feat);
		break;
#endif /* WLAN_ADVANCE_TELEMETRY */
#if WLAN_DEBUG_TELEMETRY
	case STATS_LVL_DEBUG:
		if (cfg->type == STATS_TYPE_DATA)
			ret = get_debug_peer_data(psoc, vdev, peer_mac, stats,
						  cfg->feat, cfg->peer_type);
		else
			ret = get_debug_peer_ctrl(psoc, peer_mac, stats,
						  cfg->feat);
		break;
#endif /* WLAN_DEBUG_TELEMETRY */
	default:
		qdf_err("Unexpected Level %d!\n", cfg->lvl);
		ret = QDF_STATUS_E_INVAL;
	}

	return ret;
}

QDF_STATUS wlan_stats_get_vdev_stats(struct wlan_objmgr_psoc *psoc,
				     struct wlan_objmgr_vdev *vdev,
				     struct stats_config *cfg,
				     struct unified_stats *stats)
{
	QDF_STATUS ret = QDF_STATUS_SUCCESS;

	switch (cfg->lvl) {
	case STATS_LVL_BASIC:
		if (cfg->type == STATS_TYPE_DATA)
			ret = get_basic_vdev_data(psoc, vdev, stats, cfg->feat,
						  cfg->mld_req, cfg->mld_link);
		else
			ret = get_basic_vdev_ctrl(vdev, stats, cfg->feat,
						  cfg->mld_req);
		break;
#if WLAN_ADVANCE_TELEMETRY
	case STATS_LVL_ADVANCE:
		if (cfg->type == STATS_TYPE_DATA)
			ret = get_advance_vdev_data(psoc, vdev,
						    stats, cfg->feat, cfg->mld_req,
						    cfg->mld_link);
		else
			ret = get_advance_vdev_ctrl(vdev, stats, cfg->feat,
						    cfg->mld_req);
		break;
#endif /* WLAN_ADVANCE_TELEMETRY */
#if WLAN_DEBUG_TELEMETRY
	case STATS_LVL_DEBUG:
		if (cfg->type == STATS_TYPE_DATA)
			ret = get_debug_vdev_data(psoc, vdev, stats, cfg->feat,
						  cfg->mld_req, cfg->mld_link);
		else
			ret = get_debug_vdev_ctrl(vdev, stats, cfg->feat,
						  cfg->mld_req);
		break;
#endif /* WLAN_DEBUG_TELEMETRY */
	default:
		qdf_err("Unexpected Level %d!\n", cfg->lvl);
		ret = QDF_STATUS_E_INVAL;
	}

	return ret;
}

QDF_STATUS wlan_stats_get_pdev_stats(struct wlan_objmgr_psoc *psoc,
				     struct wlan_objmgr_pdev *pdev,
				     struct stats_config *cfg,
				     struct unified_stats *stats)
{
	QDF_STATUS ret = QDF_STATUS_SUCCESS;

	switch (cfg->lvl) {
	case STATS_LVL_BASIC:
		if (cfg->type == STATS_TYPE_DATA)
			ret = get_basic_pdev_data(psoc, pdev, stats, cfg->feat);
		else
			ret = get_basic_pdev_ctrl(pdev, stats, cfg->feat,
						  cfg->aggregate);
		break;
#if WLAN_ADVANCE_TELEMETRY
	case STATS_LVL_ADVANCE:
		if (cfg->type == STATS_TYPE_DATA)
			ret = get_advance_pdev_data(psoc, pdev,
						    stats, cfg->feat);
		else
			ret = get_advance_pdev_ctrl(pdev, stats, cfg->feat,
						    cfg->aggregate);
		break;
#endif /* WLAN_ADVANCE_TELEMETRY */
#if WLAN_DEBUG_TELEMETRY
	case STATS_LVL_DEBUG:
		if (cfg->type == STATS_TYPE_DATA)
			ret = get_debug_pdev_data(psoc, pdev, stats, cfg->feat);
		else
			ret = get_debug_pdev_ctrl(pdev, stats, cfg->feat,
						  cfg->aggregate);
		break;
#endif /* WLAN_DEBUG_TELEMETRY */
	default:
		qdf_err("Unexpected Level %d!\n", cfg->lvl);
		ret = QDF_STATUS_E_INVAL;
	}

	return ret;
}

QDF_STATUS wlan_stats_get_psoc_stats(struct wlan_objmgr_psoc *psoc,
				     struct stats_config *cfg,
				     struct unified_stats *stats)
{
	QDF_STATUS ret = QDF_STATUS_SUCCESS;

	switch (cfg->lvl) {
	case STATS_LVL_BASIC:
		if (cfg->type == STATS_TYPE_DATA)
			ret = get_basic_psoc_data(psoc, stats, cfg->feat);
		break;
#if WLAN_ADVANCE_TELEMETRY
	case STATS_LVL_ADVANCE:
		if (cfg->type == STATS_TYPE_DATA)
			ret = get_advance_psoc_data(psoc, stats, cfg->feat);
		break;
#endif /* WLAN_ADVANCE_TELEMETRY */
#if WLAN_DEBUG_TELEMETRY
	case STATS_LVL_DEBUG:
		if (cfg->type == STATS_TYPE_DATA)
			ret = get_debug_psoc_data(psoc, stats, cfg->feat);
		break;
#endif /* WLAN_DEBUG_TELEMETRY */
	default:
		qdf_err("Unexpected Level %d!\n", cfg->lvl);
		ret = QDF_STATUS_E_INVAL;
	}

	return ret;
}

void wlan_stats_free_unified_stats(struct unified_stats *stats)
{
	u_int8_t inx = 0;

	for (inx = 0; inx < INX_FEAT_MAX; inx++) {
		if (stats->feat[inx])
			qdf_mem_free(stats->feat[inx]);
		stats->feat[inx] = NULL;
		stats->size[inx] = 0;
	}
}

struct wlan_objmgr_vdev *wlan_stats_get_vdev_from_sta_mac(uint8_t *mac)
{
#ifdef WLAN_FEATURE_11BE_MLO
	struct wlan_mlo_dev_context *mld_dev = NULL;
	struct wlan_mlo_peer_context *ml_peer = NULL;
#endif
	struct qdf_mac_addr mac_addr = QDF_MAC_ADDR_ZERO_INIT;
	struct wlan_objmgr_peer *peer = NULL;
	struct wlan_objmgr_vdev *vdev = NULL;
	uint8_t idx;
	struct wlan_objmgr_psoc *psoc;

	if (!mac) {
		qdf_err("mac is NULL!");
		return NULL;
	}

	qdf_mem_copy(mac_addr.bytes, mac, QDF_MAC_ADDR_SIZE);
#ifdef WLAN_FEATURE_11BE_MLO
	ml_peer = wlan_mlo_get_mlpeer_by_peer_mladdr(&mac_addr, &mld_dev);
	if (mld_dev && ml_peer)
		peer = wlan_mlo_peer_get_assoc_peer(ml_peer);
#endif

	if (peer) {
		vdev = wlan_peer_get_vdev(peer);
	} else {
		for (idx = 0; idx < WLAN_OBJMGR_MAX_DEVICES; idx++) {
			psoc = g_umac_glb_obj->psoc[idx];
			if (!psoc)
				continue;
			peer = wlan_objmgr_get_peer_by_mac(psoc, mac,
							   WLAN_MLME_SB_ID);
			if (peer) {
				vdev = wlan_peer_get_vdev(peer);
				wlan_objmgr_peer_release_ref(peer,
							     WLAN_MLME_SB_ID);
				break;
			}
		}
	}
	/* Discard BSS Peer */
	if (vdev && (wlan_vdev_get_selfpeer(vdev) == peer))
		vdev = NULL;

	return vdev;
}

uint32_t wlan_stats_get_tlv_counts_and_total_length(struct unified_stats *stats,
						    uint8_t *tlv_count)
{
	uint32_t total_len = 0;
	uint8_t inx;

	for (inx = 0; inx < INX_FEAT_MAX; inx++) {
		if (!stats->feat[inx] || !stats->size[inx])
			continue;
		total_len += stats->size[inx];
		(*tlv_count)++;
	}

	return total_len;
}
