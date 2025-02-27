/*
 * Copyright (c) 2016-2018,2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "wmi_unified_priv.h"
#include "wmi_unified_param.h"
#include "wmi_unified_ap_api.h"
#include "qdf_module.h"

QDF_STATUS wmi_unified_peer_add_wds_entry_cmd_send(
		wmi_unified_t wmi_handle,
		struct peer_add_wds_entry_params *param)
{
	if (wmi_handle->ops->send_peer_add_wds_entry_cmd)
		return wmi_handle->ops->send_peer_add_wds_entry_cmd(wmi_handle,
				param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_peer_del_wds_entry_cmd_send(
		wmi_unified_t wmi_handle,
		struct peer_del_wds_entry_params *param)
{
	if (wmi_handle->ops->send_peer_del_wds_entry_cmd)
		return wmi_handle->ops->send_peer_del_wds_entry_cmd(wmi_handle,
				param);

	return QDF_STATUS_E_FAILURE;
}

#ifdef WLAN_FEATURE_MULTI_AST_DEL
QDF_STATUS wmi_unified_peer_del_multi_wds_entries_cmd_send(
		wmi_unified_t wmi_handle,
		struct peer_del_multi_wds_entry_params *param)
{
	if (wmi_handle->ops->send_peer_del_multi_wds_entries_cmd)
		return wmi_handle->ops->send_peer_del_multi_wds_entries_cmd(
				wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}
#endif /* WLAN_FEATURE_MULTI_AST_DEL */

QDF_STATUS wmi_unified_peer_update_wds_entry_cmd_send(
		wmi_unified_t wmi_handle,
		struct peer_update_wds_entry_params *param)
{
	if (wmi_handle->ops->send_peer_update_wds_entry_cmd)
		return wmi_handle->ops->send_peer_update_wds_entry_cmd(
							wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_pdev_get_tpc_config_cmd_send(wmi_unified_t wmi_handle,
						    uint32_t param)
{
	if (wmi_handle->ops->send_pdev_get_tpc_config_cmd)
		return wmi_handle->ops->send_pdev_get_tpc_config_cmd(wmi_handle,
				param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_set_ctl_table_cmd_send(
		wmi_unified_t wmi_handle,
		struct ctl_table_params *param)
{
	if (wmi_handle->ops->send_set_ctl_table_cmd)
		return wmi_handle->ops->send_set_ctl_table_cmd(wmi_handle,
				param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_set_sta_max_pwr_table_cmd_send(
		wmi_unified_t wmi_handle,
		struct sta_max_pwr_table_params *param)
{
	if (wmi_handle->ops->send_set_sta_max_pwr_table_cmd)
		return wmi_handle->ops->send_set_sta_max_pwr_table_cmd(
								wmi_handle,
								param);
	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_set_power_table_cmd_send(
		wmi_unified_t wmi_handle,
		struct rate2power_table_params *param)
{
	if (wmi_handle->ops->send_set_power_table_cmd)
		return wmi_handle->ops->send_set_power_table_cmd(wmi_handle,
				param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_set_mimogain_table_cmd_send(
		wmi_unified_t wmi_handle,
		struct mimogain_table_params *param)
{
	if (wmi_handle->ops->send_set_mimogain_table_cmd)
		return wmi_handle->ops->send_set_mimogain_table_cmd(wmi_handle,
				param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_packet_power_info_get_cmd_send(
		wmi_unified_t wmi_handle,
		struct packet_power_info_params *param)
{
	if (wmi_handle->ops->send_packet_power_info_get_cmd)
		return wmi_handle->ops->send_packet_power_info_get_cmd(
						wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_halphy_cal_status_get_cmd_send(
		wmi_unified_t wmi_handle,
		struct halphy_cal_status_params *param)
{
	if (wmi_handle->ops->send_get_halphy_cal_status_cmd)
		return wmi_handle->ops->send_get_halphy_cal_status_cmd(
			wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_nf_dbr_dbm_info_get_cmd_send(wmi_unified_t wmi_handle,
						    uint8_t mac_id)
{
	if (wmi_handle->ops->send_nf_dbr_dbm_info_get_cmd)
		return wmi_handle->ops->send_nf_dbr_dbm_info_get_cmd(
						wmi_handle, mac_id);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_set_ht_ie_cmd_send(wmi_unified_t wmi_handle,
					  struct ht_ie_params *param)
{
	if (wmi_handle->ops->send_set_ht_ie_cmd)
		return wmi_handle->ops->send_set_ht_ie_cmd(wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_set_vht_ie_cmd_send(wmi_unified_t wmi_handle,
					   struct vht_ie_params *param)
{
	if (wmi_handle->ops->send_set_vht_ie_cmd)
		return wmi_handle->ops->send_set_vht_ie_cmd(wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_set_quiet_mode_cmd_send(
		wmi_unified_t wmi_handle,
		struct set_quiet_mode_params *param)
{
	if (wmi_handle->ops->send_set_quiet_mode_cmd)
		return wmi_handle->ops->send_set_quiet_mode_cmd(wmi_handle,
				param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_set_bcn_offload_quiet_mode_cmd_send(
		wmi_unified_t wmi_handle,
		struct set_bcn_offload_quiet_mode_params *param)
{
	if (wmi_handle->ops->send_set_bcn_offload_quiet_mode_cmd)
		return wmi_handle->ops->send_set_bcn_offload_quiet_mode_cmd(
				wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_send_bcn_offload_control_cmd(
		wmi_unified_t wmi_handle,
		struct bcn_offload_control *bcn_ctrl_param)
{
	if (wmi_handle->ops->send_bcn_offload_control_cmd)
		return wmi_handle->ops->send_bcn_offload_control_cmd(wmi_handle,
				bcn_ctrl_param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_tbttoffset_update_params(
		wmi_unified_t wmi_handle, void *evt_buf,
		uint8_t idx, struct tbttoffset_params *tbtt_param)
{
	if (wmi_handle->ops->extract_tbttoffset_update_params)
		return wmi_handle->ops->extract_tbttoffset_update_params(
					wmi_handle, evt_buf, idx, tbtt_param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_ext_tbttoffset_update_params(
		wmi_unified_t wmi_handle,
		void *evt_buf, uint8_t idx,
		struct tbttoffset_params *tbtt_param)
{
	if (wmi_handle->ops->extract_ext_tbttoffset_update_params)
		return wmi_handle->ops->extract_ext_tbttoffset_update_params(
					wmi_handle, evt_buf, idx, tbtt_param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_offchan_data_tx_compl_param(
		wmi_unified_t wmi_handle, void *evt_buf,
		struct wmi_host_offchan_data_tx_compl_event *param)
{
	if (wmi_handle->ops->extract_offchan_data_tx_compl_param)
		return wmi_handle->ops->extract_offchan_data_tx_compl_param(
				wmi_handle, evt_buf, param);


	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_swba_num_vdevs(wmi_unified_t wmi_handle, void *evt_buf,
				      uint32_t *num_vdevs,
				      uint32_t *num_quiet_active_vdevs)
{
	if (wmi_handle->ops->extract_swba_num_vdevs)
		return wmi_handle->ops->extract_swba_num_vdevs(wmi_handle,
					evt_buf, num_vdevs,
					num_quiet_active_vdevs);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_swba_tim_info(
		wmi_unified_t wmi_handle, void *evt_buf,
		uint32_t idx, wmi_host_tim_info *tim_info)
{
	if (wmi_handle->ops->extract_swba_tim_info)
		return wmi_handle->ops->extract_swba_tim_info(wmi_handle,
			evt_buf, idx, tim_info);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_swba_quiet_info(wmi_unified_t wmi_handle, void *evt_buf,
				       uint32_t idx,
				       wmi_host_quiet_info *quiet_info)
{
	if (wmi_handle->ops->extract_swba_quiet_info)
		return wmi_handle->ops->extract_swba_quiet_info(wmi_handle,
								evt_buf, idx,
								quiet_info);
	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_swba_noa_info(
		wmi_unified_t wmi_handle, void *evt_buf,
		uint32_t idx, wmi_host_p2p_noa_info *p2p_desc)
{
	if (wmi_handle->ops->extract_swba_noa_info)
		return wmi_handle->ops->extract_swba_noa_info(wmi_handle,
			evt_buf, idx, p2p_desc);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_peer_sta_kickout_ev(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_peer_sta_kickout_event *ev)
{
	if (wmi_handle->ops->extract_peer_sta_kickout_ev)
		return wmi_handle->ops->extract_peer_sta_kickout_ev(wmi_handle,
			evt_buf, ev);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_wds_addr_event(
		wmi_unified_t wmi_handle, void *evt_buf,
		uint16_t len, wds_addr_event_t *wds_ev)
{
	if (wmi_handle->ops->extract_wds_addr_event) {
		return wmi_handle->ops->extract_wds_addr_event(wmi_handle,
			evt_buf, len, wds_ev);
	}
	return QDF_STATUS_E_FAILURE;
}

qdf_export_symbol(wmi_extract_wds_addr_event);

QDF_STATUS wmi_extract_dcs_interference_type(
		wmi_unified_t wmi_handle,
		void *evt_buf, struct wmi_host_dcs_interference_param *param)
{
	if (wmi_handle->ops->extract_dcs_interference_type) {
		return wmi_handle->ops->extract_dcs_interference_type(
					wmi_handle, evt_buf, param);
	}
	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_dcs_awgn_info(wmi_unified_t wmi_handle,
				     void *evt_buf,
				     struct wmi_host_dcs_awgn_info *awgn_info)
{
	if (wmi_handle->ops->extract_dcs_awgn_info) {
		return wmi_handle->ops->extract_dcs_awgn_info(wmi_handle,
						evt_buf, awgn_info);
	}
	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_dcs_obss_intf_info(wmi_unified_t wmi_hdl, void *evt_buf,
				     wmi_host_dcs_obss_intf_info *obss_intf_info)
{
	if (wmi_hdl && wmi_hdl->ops->extract_dcs_obss_intf_info) {
		return wmi_hdl->ops->extract_dcs_obss_intf_info(wmi_hdl, evt_buf,
							   obss_intf_info);
	}

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_dcs_cw_int(wmi_unified_t wmi_handle, void *evt_buf,
				  wmi_host_ath_dcs_cw_int *cw_int)
{
	if (wmi_handle->ops->extract_dcs_cw_int) {
		return wmi_handle->ops->extract_dcs_cw_int(wmi_handle,
			evt_buf, cw_int);
	}
	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_dcs_im_tgt_stats(wmi_unified_t wmi_handle, void *evt_buf,
					wmi_host_dcs_im_tgt_stats_t *wlan_stat)
{
	if (wmi_handle->ops->extract_dcs_im_tgt_stats) {
		return wmi_handle->ops->extract_dcs_im_tgt_stats(wmi_handle,
			evt_buf, wlan_stat);
	}
	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_peer_create_response_event(
			wmi_unified_t wmi_handle,
			uint8_t *evt_buf,
			struct wmi_host_peer_create_response_event *param)
{
	if (wmi_handle->ops->extract_peer_create_response_event)
		return wmi_handle->ops->extract_peer_create_response_event(
				wmi_handle,
				evt_buf, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_peer_delete_response_event(
			wmi_unified_t wmi_handle,
			uint8_t *evt_buf,
			struct wmi_host_peer_delete_response_event *param)
{
	if (wmi_handle->ops->extract_peer_delete_response_event)
		return wmi_handle->ops->extract_peer_delete_response_event(
				wmi_handle,
				evt_buf, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_peer_ft_roam_send(wmi_unified_t wmi_handle,
					 uint8_t peer_addr[QDF_MAC_ADDR_SIZE],
					 uint8_t vdev_id)
{
	if (wmi_handle->ops->send_peer_ft_roam_cmd)
		return wmi_handle->ops->send_peer_ft_roam_cmd(wmi_handle,
					peer_addr, vdev_id);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_pdev_tpc_ev_param(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_pdev_tpc_event *param)
{
	if (wmi_handle->ops->extract_pdev_tpc_ev_param)
		return wmi_handle->ops->extract_pdev_tpc_ev_param(wmi_handle,
				evt_buf, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_pdev_tpc_config_ev_param(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_pdev_tpc_config_event *param)
{
	if (wmi_handle->ops->extract_pdev_tpc_config_ev_param)
		return wmi_handle->ops->extract_pdev_tpc_config_ev_param(
					wmi_handle, evt_buf, param);

	return QDF_STATUS_E_FAILURE;
}

#ifdef QCA_RSSI_DB2DBM
QDF_STATUS wmi_extract_pdev_rssi_dbm_conv_ev_param(
		wmi_unified_t wmi_handle, void *evt_buf,
		struct rssi_db2dbm_param *param)
{
	if (wmi_handle->ops->extract_pdev_rssi_dbm_conv_ev_param)
		return wmi_handle->ops->extract_pdev_rssi_dbm_conv_ev_param(
					wmi_handle, evt_buf, param);

	return QDF_STATUS_E_FAILURE;
}
#endif

QDF_STATUS wmi_extract_nfcal_power_ev_param(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_pdev_nfcal_power_all_channels_event *param)
{
	if (wmi_handle->ops->extract_nfcal_power_ev_param)
		return wmi_handle->ops->extract_nfcal_power_ev_param(wmi_handle,
				evt_buf, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_peer_sta_ps_statechange_ev(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_peer_sta_ps_statechange_event *ev)
{
	if (wmi_handle->ops->extract_peer_sta_ps_statechange_ev)
		return wmi_handle->ops->extract_peer_sta_ps_statechange_ev(
					wmi_handle, evt_buf, ev);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_inst_rssi_stats_event(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_inst_stats_resp *inst_rssi_resp)
{
	if (wmi_handle->ops->extract_inst_rssi_stats_event)
		return wmi_handle->ops->extract_inst_rssi_stats_event(
					wmi_handle, evt_buf, inst_rssi_resp);

	return QDF_STATUS_E_FAILURE;
}


QDF_STATUS wmi_send_pdev_caldata_version_check_cmd(wmi_unified_t wmi_handle,
						   uint32_t value)
{
	if (wmi_handle->ops->send_pdev_caldata_version_check_cmd)
		return wmi_handle->ops->send_pdev_caldata_version_check_cmd(
					wmi_handle, value);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_pdev_caldata_version_check_ev_param(
		wmi_unified_t wmi_handle,
		void *evt_buf,
		wmi_host_pdev_check_cal_version_event *param)
{
	if (wmi_handle->ops->extract_pdev_caldata_version_check_ev_param)
		return wmi_handle->ops->extract_pdev_caldata_version_check_ev_param(
			wmi_handle, evt_buf, param);

	return QDF_STATUS_E_FAILURE;
}

#ifdef WLAN_SUPPORT_FILS
QDF_STATUS
wmi_unified_fils_discovery_send_cmd(wmi_unified_t wmi_handle,
				    struct fd_params *param)
{
	if (wmi_handle->ops->send_fils_discovery_send_cmd)
		return wmi_handle->ops->send_fils_discovery_send_cmd(wmi_handle,
								     param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS
wmi_unified_fils_vdev_config_send_cmd(wmi_unified_t wmi_handle,
				      struct config_fils_params *param)
{
	if (wmi_handle->ops->send_vdev_fils_enable_cmd)
		return wmi_handle->ops->send_vdev_fils_enable_cmd(wmi_handle,
								  param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS
wmi_extract_swfda_vdev_id(wmi_unified_t wmi_handle, void *evt_buf,
			  uint32_t *vdev_id)
{
	if (wmi_handle->ops->extract_swfda_vdev_id)
		return wmi_handle->ops->extract_swfda_vdev_id(wmi_handle,
							      evt_buf, vdev_id);

	return QDF_STATUS_E_FAILURE;
}
#endif /* WLAN_SUPPORT_FILS */

QDF_STATUS wmi_unified_mcast_group_update_cmd_send(
		wmi_unified_t wmi_handle,
		struct mcast_group_update_params *param)
{
	if (wmi_handle->ops->send_mcast_group_update_cmd)
		return wmi_handle->ops->send_mcast_group_update_cmd(wmi_handle,
				param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_pdev_qvit_cmd_send(wmi_unified_t wmi_handle,
					  struct pdev_qvit_params *param)
{
	if (wmi_handle->ops->send_pdev_qvit_cmd)
		return wmi_handle->ops->send_pdev_qvit_cmd(wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_wmm_update_cmd_send(wmi_unified_t wmi_handle,
					   struct wmm_update_params *param)
{
	if (wmi_handle->ops->send_wmm_update_cmd)
		return wmi_handle->ops->send_wmm_update_cmd(wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_mgmt_tx_compl_param(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_mgmt_tx_compl_event *param)
{
	if (wmi_handle->ops->extract_mgmt_tx_compl_param)
		return wmi_handle->ops->extract_mgmt_tx_compl_param(wmi_handle,
				evt_buf, param);


	return QDF_STATUS_E_FAILURE;
}

#ifdef QCA_MANUAL_TRIGGERED_ULOFDMA
/**
 * wmi_extract_ulofdma_trigger_feedback_event - extract ulofdma trig feedback
 * @wmi_handle: wmi handle
 * @evt_buf: event buffer
 * @feedback: feedback to be extracted from event buffer
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_ulofdma_trigger_feedback_event(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_manual_ul_ofdma_trig_feedback_evt *feedback)
{
	if (wmi_handle->ops->extract_ulofdma_trigger_feedback_event)
		return wmi_handle->ops->extract_ulofdma_trigger_feedback_event(
					wmi_handle, evt_buf, feedback);

	return QDF_STATUS_E_FAILURE;
}

/**
 * wmi_extract_ul_ofdma_trig_rx_peer_userinfo - extract ulofdma trig response
 * @wmi_handle: wmi handle
 * @evt_buf: event buffer
 * @resp: Rx peer trigger response to be extracted from event buffer
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS
wmi_extract_ul_ofdma_trig_rx_peer_userinfo(
		wmi_unified_t wmi_handle, void *evt_buf,
		struct wmi_host_rx_peer_userinfo_evt_data *resp)
{
	if (wmi_handle->ops->extract_ul_ofdma_trig_rx_peer_userinfo)
		return wmi_handle->ops->extract_ul_ofdma_trig_rx_peer_userinfo(
				wmi_handle, evt_buf, resp);

	return QDF_STATUS_E_FAILURE;
}
#endif

#ifdef WLAN_FEATURE_11BE
QDF_STATUS wmi_extract_sched_mode_probe_resp_event(
		wmi_unified_t wmi_handle, void *evt_buf,
		struct wlan_host_sched_mode_probe_resp_event *resp)
{
	if (wmi_handle->ops->extract_sched_mode_probe_resp_event)
		return wmi_handle->ops->extract_sched_mode_probe_resp_event
			(wmi_handle, evt_buf, resp);

	return QDF_STATUS_E_FAILURE;
}
#endif /* WLAN_FEATURE_11BE */

QDF_STATUS wmi_extract_chan_info_event(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_chan_info_event *chan_info)
{
	if (wmi_handle->ops->extract_chan_info_event)
		return wmi_handle->ops->extract_chan_info_event(wmi_handle,
			evt_buf, chan_info);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_scan_blanking_params(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_scan_blanking_params *blanking_params)
{
	if (wmi_handle->ops->extract_scan_blanking_params)
		return wmi_handle->ops->extract_scan_blanking_params(wmi_handle,
			evt_buf, blanking_params);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_channel_hopping_event(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_pdev_channel_hopping_event *ch_hopping)
{
	if (wmi_handle->ops->extract_channel_hopping_event)
		return wmi_handle->ops->extract_channel_hopping_event(
					wmi_handle, evt_buf, ch_hopping);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_peer_chan_width_switch_cmd_send(
		wmi_unified_t wmi_handle,
		struct peer_chan_width_switch_params *param)
{
	if (wmi_handle->ops->send_peer_chan_width_switch_cmd)
		return wmi_handle->ops->send_peer_chan_width_switch_cmd(
					wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_peer_del_all_wds_entries_cmd_send(
		wmi_unified_t wmi_handle,
		struct peer_del_all_wds_entries_params *param)
{
	if (wmi_handle->ops->send_peer_del_all_wds_entries_cmd)
		return wmi_handle->ops->send_peer_del_all_wds_entries_cmd(
					wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS
wmi_unified_vdev_pcp_tid_map_cmd_send(wmi_unified_t wmi_handle,
				      struct vap_pcp_tid_map_params *param)
{
	if (wmi_handle->ops->send_vdev_pcp_tid_map_cmd)
		return wmi_handle->ops->send_vdev_pcp_tid_map_cmd(
						wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS
wmi_unified_vdev_tidmap_prec_cmd_send(wmi_unified_t wmi_handle,
				      struct vap_tidmap_prec_params *param)
{
	if (wmi_handle->ops->send_vdev_tidmap_prec_cmd)
		return wmi_handle->ops->send_vdev_tidmap_prec_cmd(
					wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}

#ifdef WLAN_SUPPORT_RX_PROTOCOL_TYPE_TAG
QDF_STATUS wmi_unified_set_rx_pkt_type_routing_tag(
		wmi_unified_t wmi_handle,
		struct wmi_rx_pkt_protocol_routing_info *param)
{
	if (wmi_handle->ops->set_rx_pkt_type_routing_tag_cmd)
		return wmi_handle->ops->set_rx_pkt_type_routing_tag_cmd(
					wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}
#endif /* WLAN_SUPPORT_RX_PROTOCOL_TYPE_TAG */

QDF_STATUS wmi_unified_peer_vlan_config_send(wmi_unified_t wmi_handle,
				uint8_t peer_addr[QDF_MAC_ADDR_SIZE],
				struct peer_vlan_config_param *param)
{
	if (wmi_handle->ops->send_peer_vlan_config_cmd)
		return wmi_handle->ops->send_peer_vlan_config_cmd(wmi_handle,
							   peer_addr,
							   param);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_extract_muedca_params_handler(
		wmi_unified_t wmi_handle,
		void *evt_buf,
		struct muedca_params *muedca_param_list)
{
	if (wmi_handle->ops->extract_muedca_params_handler)
		return wmi_handle->ops->extract_muedca_params_handler(
					wmi_handle, evt_buf, muedca_param_list);

	return QDF_STATUS_E_FAILURE;
}

/**
 * wmi_unified_set_radio_tx_mode_select_cmd_send() - WMI tx mode select cmd function
 * @wmi_handle: wmi handle
 * @param: pointer to hold tx mode selection param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_set_radio_tx_mode_select_cmd_send(
		wmi_unified_t wmi_hdl,
		struct wmi_pdev_enable_tx_mode_selection
		*tx_mode_select_param)
{
	wmi_unified_t wmi_handle = wmi_hdl;

	if (wmi_handle->ops->set_radio_tx_mode_select_cmd)
		return wmi_handle->ops->set_radio_tx_mode_select_cmd(
				wmi_handle, tx_mode_select_param);
	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_send_lcr_cmd(wmi_unified_t wmi_handle,
				    struct wmi_wifi_pos_lcr_info *lcr_info)
{
	if (wmi_handle->ops->send_lcr_cmd)
		return wmi_handle->ops->send_lcr_cmd(wmi_handle, lcr_info);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_unified_send_lci_cmd(wmi_unified_t wmi_handle,
				    struct wifi_pos_lci_info *lci_info)
{
	if (wmi_handle->ops->send_lci_cmd)
		return wmi_handle->ops->send_lci_cmd(wmi_handle, lci_info);

	return QDF_STATUS_E_FAILURE;
}

#ifdef WLAN_SUPPORT_MESH_LATENCY
/**
 * wmi_unified_config_vdev_tid_latency_info_cmd_send() - WMI vdev tid latency
 * @wmi_handle: wmi handle
 * @param: pointer to hold vdev latency config param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_config_vdev_tid_latency_info_cmd_send(
		wmi_unified_t wmi_hdl,
		struct wmi_vdev_tid_latency_config_params
		*vdev_latency_config_param)
{
	wmi_unified_t wmi_handle = wmi_hdl;

	if (wmi_handle->ops->config_vdev_tid_latency_info_cmd)
		return wmi_handle->ops->config_vdev_tid_latency_info_cmd(
				wmi_handle, vdev_latency_config_param);
	return QDF_STATUS_E_FAILURE;
}

/**
 * wmi_unified_config_peer_latency_info_cmd_send() - WMI vdev tid latency
 * @wmi_handle: wmi handle
 * @param: pointer to hold peer latency config param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_config_peer_latency_info_cmd_send(
		wmi_unified_t wmi_hdl,
		struct wmi_peer_latency_config_params
		*param)
{
	wmi_unified_t wmi_handle = wmi_hdl;

	if (wmi_handle->ops->config_peer_latency_info_cmd)
		return wmi_handle->ops->config_peer_latency_info_cmd(
				wmi_handle, param);
	return QDF_STATUS_E_FAILURE;
}

#ifdef WLAN_WSI_STATS_SUPPORT
/**
 * wmi_unified_config_wsi_stats_info_cmd_send() - WMI WSI stats info send
 * @wmi_hdl: wmi handle
 * @param: pointer to hold the ingress and egress information
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_config_wsi_stats_info_cmd_send(
		wmi_unified_t wmi_hdl,
		struct wmi_wsi_stats_info_params *param)
{
	wmi_unified_t wmi_handle = wmi_hdl;

	if (wmi_handle->ops->send_wsi_stats_info_cmd)
		return wmi_handle->ops->send_wsi_stats_info_cmd(
				wmi_handle, param);
	return QDF_STATUS_E_FAILURE;
}
#endif

#ifdef QCA_MANUAL_TRIGGERED_ULOFDMA
/**
 * wmi_unified_config_trigger_ulofdma_su_cmd_send() - WMI SU ULOFDMA trigger
 * @wmi_hdl: wmi handle
 * @param: pointer to hold peer config SU trigger info
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_config_trigger_ulofdma_su_cmd_send(
		wmi_unified_t wmi_hdl,
		struct wmi_trigger_ul_ofdma_su_params *param)
{
	wmi_unified_t wmi_handle = wmi_hdl;

	if (wmi_handle->ops->trigger_ulofdma_su_cmd)
		return wmi_handle->ops->trigger_ulofdma_su_cmd(
				wmi_handle, param);
	return QDF_STATUS_E_FAILURE;
}

/**
 * wmi_unified_config_trigger_ulofdma_mu_cmd_send() - WMI MU ULOFDMA trigger
 * @wmi_hdl: wmi handle
 * @param: pointer to hold peer config MU trigger info
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_config_trigger_ulofdma_mu_cmd_send(
		wmi_unified_t wmi_hdl,
		struct wmi_trigger_ul_ofdma_mu_params *param)
{
	wmi_unified_t wmi_handle = wmi_hdl;

	if (wmi_handle->ops->trigger_ulofdma_mu_cmd)
		return wmi_handle->ops->trigger_ulofdma_mu_cmd(
				wmi_handle, param);
	return QDF_STATUS_E_FAILURE;
}
#endif

/**
 * wmi_unified_vdev_set_intra_bss_cmd_send() - Set inta bss params
 * @wmi_handle: wmi handle
 * @param: params received in wmi_intra_bss_params
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS
wmi_unified_vdev_set_intra_bss_cmd_send(struct wmi_unified *wmi_handle,
					struct wmi_intra_bss_params *param)
{
	if (wmi_handle->ops->send_vdev_set_intra_bss_cmd)
		return wmi_handle->ops->send_vdev_set_intra_bss_cmd(wmi_handle,
								    param);

	return QDF_STATUS_E_FAILURE;
}

/**
 * wmi_unified_peer_set_intra_bss_cmd_send() - set cmd to config intra_bss
 * @wmi_handle: wmi handle
 * @param: params needed for intr_bss config
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS
wmi_unified_peer_set_intra_bss_cmd_send(struct wmi_unified *wmi_handle,
					struct wmi_intra_bss_params *param)
{
	if (wmi_handle->ops->send_peer_set_intra_bss_cmd)
		return wmi_handle->ops->send_peer_set_intra_bss_cmd(wmi_handle,
								    param);

	return QDF_STATUS_E_FAILURE;
}
#endif

/**
 * wmi_unified_soc_tqm_reset_enable_disable_cmd() - Send tqm reset command to FW
 * @wmi_handle: wmi handle
 * @enable: enable or disable configuration from user
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS
wmi_unified_soc_tqm_reset_enable_disable_cmd(wmi_unified_t wmi_handle,
					     uint32_t enable)
{
	if (wmi_handle->ops->send_soc_tqm_reset_enable_disable_cmd)
		return wmi_handle->ops->send_soc_tqm_reset_enable_disable_cmd
			(wmi_handle, enable);

	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS
wmi_unified_set_peer_disable_mode(wmi_unified_t wmi_handle,
				  uint8_t *peer_mac,
				  uint8_t pdev_id,
				  uint32_t disabled_modes)
{
	if (wmi_handle->ops->send_set_peer_disable_mode)
		return wmi_handle->ops->send_set_peer_disable_mode(wmi_handle,
								   peer_mac,
								   pdev_id,
								   disabled_modes);
	return QDF_STATUS_E_FAILURE;
}

#ifdef CONFIG_SAWF_DEF_QUEUES
QDF_STATUS
wmi_unified_set_rate_upper_cap_cmd_send(struct wmi_unified *wmi_handle,
					uint8_t pdev_id,
					struct wmi_rc_params *param)
{
	if (wmi_handle->ops->send_set_rate_upper_cap_cmd)
		return wmi_handle->ops->send_set_rate_upper_cap_cmd(wmi_handle,
								    pdev_id,
								    param);
	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS
wmi_unified_set_rate_retry_mcs_drop_cmd_send(struct wmi_unified *wmi_handle,
					     uint8_t pdev_id,
					     struct wmi_rc_params *param)
{
	if (wmi_handle->ops->send_set_rate_retry_mcs_drop_cmd)
		return wmi_handle->ops->send_set_rate_retry_mcs_drop_cmd
			(wmi_handle, pdev_id, param);
	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS
wmi_unified_set_mcs_probe_intvl_cmd_send(struct wmi_unified *wmi_handle,
					 uint8_t pdev_id,
					 struct wmi_rc_params *param)
{
	if (wmi_handle->ops->send_set_mcs_probe_intvl_cmd)
		return wmi_handle->ops->send_set_mcs_probe_intvl_cmd(wmi_handle,
								     pdev_id,
								     param);
	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS
wmi_unified_set_nss_probe_intvl_cmd_send(struct wmi_unified *wmi_handle,
					 uint8_t pdev_id,
					 struct wmi_rc_params *param)
{
	if (wmi_handle->ops->send_set_nss_probe_intvl_cmd)
		return wmi_handle->ops->send_set_nss_probe_intvl_cmd(wmi_handle,
								     pdev_id,
								     param);
	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_sawf_create_send(struct wmi_unified *wmi_handle,
				struct wmi_sawf_params *param)
{
	if (wmi_handle->ops->send_sawf_create_cmd) {
		return wmi_handle->ops->send_sawf_create_cmd(wmi_handle,
							     param);
	}
	return QDF_STATUS_E_FAILURE;
}

QDF_STATUS wmi_sawf_disable_send(struct wmi_unified *wmi_handle,
				 uint8_t svc_id)
{
	if (wmi_handle->ops->send_sawf_disable_cmd) {
		return wmi_handle->ops->send_sawf_disable_cmd(wmi_handle,
							      svc_id);
	}
	return QDF_STATUS_E_FAILURE;
}
#endif

#ifdef QCA_STANDALONE_SOUNDING_TRIGGER
QDF_STATUS wmi_extract_standalone_sounding_evt_params(
		wmi_unified_t wmi_handle, void *evt_buf,
		struct wmi_host_standalone_sounding_evt_params *ss_params)
{
	if (wmi_handle->ops->extract_standalone_sounding_evt_params) {
		return wmi_handle->ops->extract_standalone_sounding_evt_params(
				wmi_handle, evt_buf, ss_params);
	}

	return QDF_STATUS_E_FAILURE;
}
#endif /* QCA_STANDALONE_SOUNDING_TRIGGER */

QDF_STATUS wmi_unified_tdma_schedule_send(
		struct wmi_unified *wmi_handle,
		struct wlan_tdma_sched_cmd_param *param)
{
	if (wmi_handle->ops->send_wmi_tdma_schedule_request_cmd) {
		return wmi_handle->ops->send_wmi_tdma_schedule_request_cmd(
							wmi_handle, param);
	}
	return QDF_STATUS_E_FAILURE;
}

#ifdef WLAN_FEATURE_11BE_MLO
QDF_STATUS wmi_unified_link_recmnd_info_send(
		wmi_unified_t wmi_handle,
		struct wlan_link_recmnd_param *param)
{
	if (wmi_handle->ops->send_wmi_link_recommendation_cmd)
		return wmi_handle->ops->send_wmi_link_recommendation_cmd(
							wmi_handle, param);
	return QDF_STATUS_E_FAILURE;
}
#endif

#ifdef WLAN_FEATURE_11BE
QDF_STATUS
wmi_unified_send_mu_on_off_cmd(wmi_unified_t wmi_handle,
			       struct wmi_host_mu_on_off_params *params)
{
	if (wmi_handle->ops->send_mu_on_off_cmd)
		return wmi_handle->ops->send_mu_on_off_cmd(wmi_handle, params);

	return QDF_STATUS_E_FAILURE;
}
#endif /* WLAN_FEATURE_11BE */

#ifdef QCA_STANDALONE_SOUNDING_TRIGGER
QDF_STATUS wmi_unified_txbf_sounding_trig_info_cmd_send(struct wmi_unified *wmi_handle,
							struct wmi_txbf_sounding_trig_param *sounding_params)
{
	if (wmi_handle->ops->config_txbf_sounding_trig_info_cmd) {
	    return wmi_handle->ops->config_txbf_sounding_trig_info_cmd(wmi_handle,
								       sounding_params);
	}
	return QDF_STATUS_E_FAILURE;
}
#endif
#ifdef WLAN_SUPPORT_TX_PKT_CAP_CUSTOM_CLASSIFY
QDF_STATUS wmi_unified_set_tx_pkt_cap_custom_classify(
		wmi_unified_t wmi_handle,
		struct wmi_tx_pkt_cap_custom_classify_info *param)
{
	if (wmi_handle->ops->set_tx_pkt_cap_custom_classify)
		return wmi_handle->ops->set_tx_pkt_cap_custom_classify(
							wmi_handle, param);

	return QDF_STATUS_E_FAILURE;
}
#endif /* WLAN_SUPPORT_TX_PKT_CAP_CUSTOM_CLASSIFY */
