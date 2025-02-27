/*
 * Copyright (c) 2013-2018,2020-2021 The Linux Foundation. All rights reserved.
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

/*
 * This file contains the API definitions for the generic AP WMIs
 */

#ifndef _WMI_UNIFIED_AP_API_H_
#define _WMI_UNIFIED_AP_API_H_

#include "wmi_unified_param.h"
#include <wmi_unified_ap_params.h>

/**
 *  wmi_unified_beacon_send_cmd() - WMI beacon send function
 *  @wmi_handle: handle to WMI.
 *  @macaddr: MAC address
 *  @param: pointer to hold beacon send cmd parameter
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_beacon_send_cmd(wmi_unified_t wmi_handle,
				       struct beacon_params *param);

/**
 *  wmi_unified_pdev_get_tpc_config_cmd_send() - WMI get tpc config function
 *  @wmi_handle: handle to WMI.
 *  @param: tpc config param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_pdev_get_tpc_config_cmd_send(wmi_unified_t wmi_handle,
						    uint32_t param);

/**
 * wmi_send_pdev_caldata_version_check_cmd() - send reset peer mumimo
 *                                             tx count to fw
 * @wmi_handle: wmi handle
 * @value: value
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_send_pdev_caldata_version_check_cmd(wmi_unified_t wmi_handle,
						   uint32_t value);

/**
 *  wmi_unified_set_ht_ie_cmd_send() - WMI set channel cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold channel param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_set_ht_ie_cmd_send(wmi_unified_t wmi_handle,
					  struct ht_ie_params *param);

/**
 *  wmi_unified_set_vht_ie_cmd_send() - WMI set channel cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold channel param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_set_vht_ie_cmd_send(wmi_unified_t wmi_handle,
					   struct vht_ie_params *param);

/**
 *  wmi_unified_set_ctl_table_cmd_send() - WMI ctl table cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold ctl table param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_set_ctl_table_cmd_send(wmi_unified_t wmi_handle,
					      struct ctl_table_params *param);

/**
 *  wmi_unified_set_sta_max_pwr_table_cmd_send() - WMI sta max Tx pwr cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold sta max table param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_set_sta_max_pwr_table_cmd_send(
		wmi_unified_t wmi_handle,
		struct sta_max_pwr_table_params *param);

/**
 *  wmi_unified_set_power_table_cmd_send() - WMI rate2power table set cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold rate2power table params
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_set_power_table_cmd_send(
					wmi_unified_t wmi_handle,
					struct rate2power_table_params *param);

/**
 *  wmi_unified_set_mimogain_table_cmd_send() - WMI set mimogain cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold mimogain param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_set_mimogain_table_cmd_send(
		wmi_unified_t wmi_handle,
		struct mimogain_table_params *param);

/**
 *  wmi_unified_peer_add_wds_entry_cmd_send() - WMI add wds entry cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold wds entry param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_peer_add_wds_entry_cmd_send(
		wmi_unified_t wmi_handle,
		struct peer_add_wds_entry_params *param);

/**
 *  wmi_unified_peer_del_wds_entry_cmd_send() - WMI del wds entry cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold wds entry param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_peer_del_wds_entry_cmd_send(
		wmi_unified_t wmi_handle,
		struct peer_del_wds_entry_params *param);

#ifdef WLAN_FEATURE_MULTI_AST_DEL
/**
 *  wmi_unified_peer_del_multi_wds_entries_cmd_send() -
 *  WMI del multi wds entry cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold wds entry param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_peer_del_multi_wds_entries_cmd_send(
		wmi_unified_t wmi_handle,
		struct peer_del_multi_wds_entry_params *param);
#endif /* WLAN_FEATURE_MULTI_AST_DEL */

/**
 *  wmi_unified_peer_update_wds_entry_cmd_send() - WMI update wds entry
 *  cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold wds entry param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_peer_update_wds_entry_cmd_send(
		wmi_unified_t wmi_handle,
		struct peer_update_wds_entry_params *param);


/**
 * wmi_unified_peer_ft_roam_send() - reset BA window in fw
 * @wmi_handle: wmi handle
 * @peer_addr: peer mac address
 * @vdev_id: vdev id
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS
wmi_unified_peer_ft_roam_send(wmi_unified_t wmi_handle,
			      uint8_t peer_addr[QDF_MAC_ADDR_SIZE],
			      uint8_t vdev_id);


/**
 *  wmi_unified_peer_filter_set_tx_cmd_send() - WMI set tx peer filter command
 *  @wmi_handle: handle to WMI.
 *  @macaddr: MAC address
 *  @param: pointer to hold tx_peer_filter parameter
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS
wmi_unified_peer_filter_set_tx_cmd_send(struct wmi_unified *wmi_handle,
					uint8_t macaddr[],
					struct set_tx_peer_filter *param);
/**
 *  wmi_unified_vdev_set_neighbour_rx_cmd_send() - WMI set neighbour rx function
 *  @wmi_handle: handle to WMI.
 *  @macaddr: MAC address
 *  @param: pointer to hold neighbour rx parameter
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_vdev_set_neighbour_rx_cmd_send(
		wmi_unified_t wmi_handle,
		uint8_t macaddr[QDF_MAC_ADDR_SIZE],
		struct set_neighbour_rx_params *param);

/**
 *  wmi_unified_vdev_config_ratemask_cmd_send() - WMI config ratemask function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold config ratemask param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_vdev_config_ratemask_cmd_send(
		wmi_unified_t wmi_handle,
		struct config_ratemask_params *param);

/**
 *  wmi_unified_set_quiet_mode_cmd_send() - WMI set quiet mode function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold quiet mode param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_set_quiet_mode_cmd_send(
		wmi_unified_t wmi_handle,
		struct set_quiet_mode_params *param);

/**
 *  wmi_unified_set_bcn_offload_quiet_mode_cmd_send - WMI set quiet mode
 *      function in beacon offload case
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold quiet mode param in bcn offload
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_set_bcn_offload_quiet_mode_cmd_send(
		wmi_unified_t wmi_handle,
		struct set_bcn_offload_quiet_mode_params *param);

/**
 *  wmi_unified_nf_dbr_dbm_info_get_cmd_send() - WMI request nf info function
 *  @wmi_handle: handle to WMI.
 *  @mac_id: mac_id
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_nf_dbr_dbm_info_get_cmd_send(wmi_unified_t wmi_handle,
						    uint8_t mac_id);

/**
 *  wmi_unified_packet_power_info_get_cmd_send() - WMI get packet power
 *	info function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold packet power info param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_packet_power_info_get_cmd_send(
		wmi_unified_t wmi_handle,
		struct packet_power_info_params *param);

/**
 * wmi_unified_halphy_cal_status_get_cmd_send() - WMI get halphy cal
 * status function
 * @wmi_handle: handle to WMI.
 * @param: pointer to hold halphy cal status param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_halphy_cal_status_get_cmd_send(
		wmi_unified_t wmi_handle,
		struct halphy_cal_status_params *param);

/**
 *  wmi_extract_wds_addr_event - Extract WDS addr WMI event
 *  @wmi_handle: handle to WMI.
 *  @evt_buf: pointer to event buffer
 *  @len: length of the event buffer
 *  @wds_ev: pointer to strct to extract
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_wds_addr_event(
		wmi_unified_t wmi_handle,
		void *evt_buf, uint16_t len, wds_addr_event_t *wds_ev);

/**
 * wmi_extract_dcs_interference_type() - extract dcs interference type
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @param: Pointer to hold dcs interference param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_dcs_interference_type(
		wmi_unified_t wmi_handle,
		void *evt_buf, struct wmi_host_dcs_interference_param *param);

/*
 * wmi_extract_dcs_awgn_info() - extract DCS AWGN interference info from event
 * @wmi_handle: WMI handle
 * @evt_buf   : Pointer to event buffer
 * @awgn_info : Pointer to hold AWGN interference info
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_dcs_awgn_info(wmi_unified_t wmi_handle,
				     void *evt_buf,
				     struct wmi_host_dcs_awgn_info *awgn_info);

/**
 * wmi_extract_dcs_obss_intf_info() - extract DCS OBSS interference info from event
 * @wmi_hdl: WMI handle
 * @evt_buf: Pointer to event buffer
 * @obss_intf_info: Pointer to hold OBSS interference info
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS
wmi_extract_dcs_obss_intf_info(wmi_unified_t wmi_hdl, void *evt_buf,
			  wmi_host_dcs_obss_intf_info *obss_intf_info);

/*
 * wmi_extract_dcs_cw_int() - extract dcs cw interference from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @cw_int: Pointer to hold cw interference
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_dcs_cw_int(wmi_unified_t wmi_handle, void *evt_buf,
				  wmi_host_ath_dcs_cw_int *cw_int);

/**
 * wmi_extract_dcs_im_tgt_stats() - extract dcs im target stats from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @wlan_stat: Pointer to hold wlan stats
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_dcs_im_tgt_stats(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_dcs_im_tgt_stats_t *wlan_stat);

/**
 * wmi_extract_tbttoffset_update_params() - extract tbtt offset update param
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @idx: Index referring to a vdev
 * @tbtt_param: Pointer to tbttoffset event param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_tbttoffset_update_params(
		wmi_unified_t wmi_handle, void *evt_buf,
		uint8_t idx, struct tbttoffset_params *tbtt_param);

/**
 * wmi_extract_ext_tbttoffset_update_params() - extract tbtt offset update param
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @idx: Index referring to a vdev
 * @tbtt_param: Pointer to tbttoffset event param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_ext_tbttoffset_update_params(
		wmi_unified_t wmi_handle,
		void *evt_buf, uint8_t idx,
		struct tbttoffset_params *tbtt_param);

/**
 * wmi_extract_tbttoffset_num_vdevs() - extract tbtt offset num vdev
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @vdev_map: Pointer to hold num vdev
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_tbttoffset_num_vdevs(wmi_unified_t wmi_handle,
					    void *evt_buf,
					    uint32_t *num_vdevs);

/**
 * wmi_extract_ext_tbttoffset_num_vdevs() - extract ext tbtt offset num vdev
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @vdev_map: Pointer to hold num vdev
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_ext_tbttoffset_num_vdevs(wmi_unified_t wmi_handle,
						void *evt_buf,
						uint32_t *num_vdevs);

/**
 * wmi_extract_pdev_caldata_version_check_ev_param() - extract caldata
 *                                                     from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @param: Pointer to hold caldata version data
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_pdev_caldata_version_check_ev_param(
		wmi_unified_t wmi_handle,
		void *evt_buf, wmi_host_pdev_check_cal_version_event *param);

/**
 * wmi_extract_pdev_tpc_config_ev_param() - extract pdev tpc configuration
 * param from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @param: Pointer to hold tpc configuration
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_pdev_tpc_config_ev_param(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_pdev_tpc_config_event *param);

#ifdef QCA_RSSI_DB2DBM
/**
 * wmi_extract_pdev_rssi_dbm_conv_ev_param() - extract rssi_dbm evt params
 *
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @param: Pointer to hold tpc configuration
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_pdev_rssi_dbm_conv_ev_param(
		wmi_unified_t wmi_handle, void *evt_buf,
		struct rssi_db2dbm_param *param);
#endif

/**
 * wmi_extract_nfcal_power_ev_param() - extract noise floor calibration
 * power param from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @param: Pointer to hold nf cal power param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_nfcal_power_ev_param(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_pdev_nfcal_power_all_channels_event *param);

/**
 * wmi_extract_pdev_tpc_ev_param() - extract tpc param from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @param: Pointer to hold tpc param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_pdev_tpc_ev_param(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_pdev_tpc_event *param);

/**
 * wmi_extract_offchan_data_tx_compl_param() -
 *          extract offchan data tx completion param from event
 * @wmi_hdl: wmi handle
 * @evt_buf: pointer to event buffer
 * @param: Pointer to offchan data tx completion param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_offchan_data_tx_compl_param(
		wmi_unified_t wmi_handle, void *evt_buf,
		struct wmi_host_offchan_data_tx_compl_event *param);

/**
 * wmi_extract_swba_num_vdevs() - extract swba num vdevs from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @num_vdevs: Pointer to hold num vdevs
 * @param num_quiet_active_vdevs: Pointer to hold number of Quiet active vdevs
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_swba_num_vdevs(wmi_unified_t wmi_handle, void *evt_buf,
				      uint32_t *num_vdevs,
				      uint32_t *num_quiet_active_vdevs);

/**
 * wmi_extract_swba_tim_info() - extract swba tim info from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @idx: Index to bcn info
 * @tim_info: Pointer to hold tim info
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_swba_tim_info(
		wmi_unified_t wmi_handle, void *evt_buf,
		uint32_t idx, wmi_host_tim_info *tim_info);

/**
 * wmi_extract_swba_quiet_info() - extract swba quiet info from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @idx: Index to bcn info
 * @quiet_info: Pointer to hold quiet info
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_swba_quiet_info(wmi_unified_t wmi_handle, void *evt_buf,
				       uint32_t idx,
				       wmi_host_quiet_info *quiet_info);

/**
 * wmi_extract_swba_noa_info() - extract swba NoA information from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @idx: Index to bcn info
 * @p2p_desc: Pointer to hold p2p NoA info
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_swba_noa_info(wmi_unified_t wmi_handle, void *evt_buf,
				     uint32_t idx,
				     wmi_host_p2p_noa_info *p2p_desc);

/**
 * wmi_extract_peer_sta_ps_statechange_ev() - extract peer sta ps state
 * from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @ev: Pointer to hold peer param and ps state
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_peer_sta_ps_statechange_ev(
		wmi_unified_t wmi_handle,
		void *evt_buf, wmi_host_peer_sta_ps_statechange_event *ev);

/**
 * wmi_extract_peer_sta_kickout_ev() - extract peer sta kickout event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @ev: Pointer to hold peer param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_peer_sta_kickout_ev(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_peer_sta_kickout_event *ev);

/**
 * wmi_extract_inst_rssi_stats_event() - extract inst rssi stats from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @inst_rssi_resp: Pointer to hold inst rssi response
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_inst_rssi_stats_event(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_inst_stats_resp *inst_rssi_resp);

/**
 * wmi_unified_send_multiple_vdev_restart_req_cmd() - send multiple vdev restart
 * @wmi_handle: wmi handle
 * @param: multiple vdev restart parameter
 *
 * Send WMI_PDEV_MULTIPLE_VDEV_RESTART_REQUEST_CMDID parameters to fw.
 *
 * Return: QDF_STATUS_SUCCESS on success, QDF_STATUS_E_** on error
 */
QDF_STATUS wmi_unified_send_multiple_vdev_restart_req_cmd(
		wmi_unified_t wmi_handle,
		struct multiple_vdev_restart_params *param);

/**
 * wmi_unified_send_multiple_vdev_set_param_cmd() - send multiple vdev set param
 * @wmi_handle: wmi handle
 * @param: multiple vdev set parameter
 *
 * Send WMI_PDEV_MULTIPLE_VDEV_SET_PARAM_CMDID parameters to fw.
 *
 * Return: QDF_STATUS_SUCCESS on success, QDF_STATUS_E_** on error
 */
QDF_STATUS wmi_unified_send_multiple_vdev_set_param_cmd(
				struct wmi_unified *wmi_handle,
				struct multiple_vdev_set_param *param);
/**
 * wmi_extract_peer_create_response_event() -
 * extract vdev id and peer mac address and status from peer create
 * response event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @param: Pointer to hold evt buf
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS wmi_extract_peer_create_response_event(
		wmi_unified_t wmi_handle,
		uint8_t *evt_buf,
		struct wmi_host_peer_create_response_event *param);

/**
 * wmi_extract_peer_delete_response_event() -
 *       extract vdev id and peer mac addresse from peer delete response event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @param: Pointer to hold evt buf
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS wmi_extract_peer_delete_response_event(
		wmi_unified_t wmi_handle,
		uint8_t *evt_buf,
		struct wmi_host_peer_delete_response_event *param);

/**
 * wmi_send_bcn_offload_control_cmd - send beacon ofload control cmd to fw
 * @wmi_hdl: wmi handle
 * @bcn_ctrl_param: pointer to bcn_offload_control param
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS wmi_send_bcn_offload_control_cmd(
		wmi_unified_t wmi_handle,
		struct bcn_offload_control *bcn_ctrl_param);

#ifdef WLAN_SUPPORT_FILS
/**
 * wmi_unified_fils_vdev_config_send_cmd() - send FILS config cmd to fw
 * @wmi_hdl: wmi handle
 * @param: fils config params
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS
wmi_unified_fils_vdev_config_send_cmd(wmi_unified_t wmi_handle,
				      struct config_fils_params *param);

/**
 * wmi_extract_swfda_vdev_id() - api to extract vdev id
 * @wmi_hdl: wmi handle
 * @evt_buf: pointer to event buffer
 * @vdev_id: pointer to vdev id
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS wmi_extract_swfda_vdev_id(wmi_unified_t wmi_handle, void *evt_buf,
				     uint32_t *vdev_id);

/**
 * wmi_unified_fils_discovery_send_cmd() - send FILS discovery cmd to fw
 * @wmi_hdl: wmi handle
 * @param: fils discovery params
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS wmi_unified_fils_discovery_send_cmd(wmi_unified_t wmi_handle,
					       struct fd_params *param);
#endif /* WLAN_SUPPORT_FILS */

/**
 *  wmi_unified_mcast_group_update_cmd_send() - WMI mcast grp update cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold mcast grp param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS
wmi_unified_mcast_group_update_cmd_send(wmi_unified_t wmi_handle,
					struct mcast_group_update_params *param);

/**
 *  wmi_unified_pdev_qvit_cmd_send() - WMI pdev qvit cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold qvit param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_pdev_qvit_cmd_send(wmi_unified_t wmi_handle,
					  struct pdev_qvit_params *param);

/**
 *  wmi_unified_wmm_update_cmd_send() - WMI wmm update cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold wmm param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_wmm_update_cmd_send(wmi_unified_t wmi_handle,
					   struct wmm_update_params *param);

/**
 * wmi_extract_vdev_start_resp() - extract vdev start response
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @vdev_rsp: Pointer to hold vdev response
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_vdev_start_resp(
		wmi_unified_t wmi_handle, void *evt_buf,
		struct vdev_start_response *vdev_rsp);

/**
 * wmi_extract_vdev_delete_resp - api to extract vdev delete
 * response event params
 * @wmi_handle: wma handle
 * @evt_buf: pointer to event buffer
 * @delele_rsp: pointer to hold delete response from firmware
 *
 * Return: QDF_STATUS_SUCCESS for successful event parse
 *         else QDF_STATUS_E_INVAL or QDF_STATUS_E_FAILURE
 */
QDF_STATUS wmi_extract_vdev_delete_resp(
		wmi_unified_t wmi_handle, void *evt_buf,
		struct vdev_delete_response *delele_rsp);

/**
 * wmi_extract_vdev_stopped_param() - extract vdev stop param from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @vdev_id: Pointer to hold vdev identifier
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_vdev_stopped_param(wmi_unified_t wmi_handle,
					  void *evt_buf,
					  uint32_t *vdev_id);

/**
 * wmi_extract_mgmt_tx_compl_param() - extract mgmt tx completion param
 * from event
 * @wmi_hdl: wmi handle
 * @evt_buf: pointer to event buffer
 * @param: Pointer to mgmt tx completion param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_mgmt_tx_compl_param(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_mgmt_tx_compl_event *param);

#ifdef QCA_MANUAL_TRIGGERED_ULOFDMA
/**
 * wmi_extract_ulofdma_trigger_feedback_event() - extract ulofdma trig feedback
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @feedback: Pointer to hold ulofdma trig feedback
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_ulofdma_trigger_feedback_event(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_manual_ul_ofdma_trig_feedback_evt *feedback);

/**
 * wmi_extract_ul_ofdma_trig_rx_peer_userinfo() - extract ulofdma
 * trigger response uder info
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @resp: Pointer to ulofdma rx trig peer response data
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS
wmi_extract_ul_ofdma_trig_rx_peer_userinfo(
		wmi_unified_t wmi_handle, void *evt_buf,
		struct wmi_host_rx_peer_userinfo_evt_data *resp);
#endif

#ifdef WLAN_FEATURE_11BE
/**
 * wmi_extract_sched_mode_probe_resp_event() - extract sched mode probe resp
 * from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @resp: Pointer to hold sched mode probe resp data
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS
wmi_extract_sched_mode_probe_resp_event(
		wmi_unified_t wmi_handle, void *evt_buf,
		struct wlan_host_sched_mode_probe_resp_event *resp);
#endif /* WLAN_FEATURE_11BE */

/**
 * wmi_extract_chan_info_event() - extract chan information from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @chan_info: Pointer to hold chan information
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_chan_info_event(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_chan_info_event *chan_info);

/**
 * wmi_extract_scan_blanking_params() - extract scan blanking params from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @chan_info: Pointer to hold blanking parameters
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_scan_blanking_params(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_scan_blanking_params *blanking_params);

/**
 * wmi_extract_channel_hopping_event() - extract channel hopping param
 * from event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @ch_hopping: Pointer to hold channel hopping param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_channel_hopping_event(
		wmi_unified_t wmi_handle, void *evt_buf,
		wmi_host_pdev_channel_hopping_event *ch_hopping);

/**
 * wmi_unified_peer_chan_width_switch_cmd_send() - WMI send peer chan width
 * @wmi_hdl: handle to WMI
 * @param: pointer to hold peer capability param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_peer_chan_width_switch_cmd_send(
		wmi_unified_t wmi_handle,
		struct peer_chan_width_switch_params *param);

/**
 * wmi_unified_peer_del_all_wds_entries_cmd_send() - send delete
 * all wds entries cmd to fw
 * @wmi_hdl: wmi handle
 * @param: delete all wds entries params
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS wmi_unified_peer_del_all_wds_entries_cmd_send(
		wmi_unified_t wmi_handle,
		struct peer_del_all_wds_entries_params *param);

/**
 *  wmi_unified_vdev_pcp_tid_map_cmd_send() - WMI set vap pcp
 *  tid map cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold pcp param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_vdev_pcp_tid_map_cmd_send(
		wmi_unified_t wmi_handle,
		struct vap_pcp_tid_map_params *param);


/**
 *  wmi_unified_vdev_tidmap_prec_cmd_send() - WMI set vap tidmap precedence
 *  cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold precedence param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_vdev_tidmap_prec_cmd_send(
		wmi_unified_t wmi_handle,
		struct vap_tidmap_prec_params *param);

#ifdef WLAN_SUPPORT_RX_PROTOCOL_TYPE_TAG
/**
 * wmi_unified_set_rx_pkt_type_routing_tag() - api to add/delete
 * the protocols to be tagged by CCE
 * @wmi_hdl: wmi handle
 * @param: Packet routing/tagging info
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS wmi_unified_set_rx_pkt_type_routing_tag(
		wmi_unified_t wmi_handle,
		struct wmi_rx_pkt_protocol_routing_info *param);
#endif /* WLAN_SUPPORT_RX_PROTOCOL_TYPE_TAG */

/**
 * wmi_unified_peer_vlan_config_send() - WMI function to send vlan command
 *
 * @wmi_hdl: WMI handle
 * @peer_addr: Peer mac address
 * @param: struct peer_vlan_config_param *
 *
 * Return: QDF_STATUS_SUCCESS if success, else returns proper error code.
 */
QDF_STATUS wmi_unified_peer_vlan_config_send(wmi_unified_t wmi_handle,
		uint8_t peer_addr[QDF_MAC_ADDR_SIZE],
		struct peer_vlan_config_param *param);

/**
 * wmi_extract_muedca_params_handler() - WMI function to extract Muedca params
 *
 * @wmi_handle: WMI handle
 * @evt_buf: Event data buffer
 * @muedca_param_list: struct muedca_params
 *
 * Return: QDF_STATUS_SUCCESS if success, else returns proper error code.
 */
QDF_STATUS wmi_extract_muedca_params_handler(wmi_unified_t wmi_handle,
		void *evt_buf, struct muedca_params *muedca_param_list);

/**
 *  wmi_unified_set_radio_tx_mode_select_cmd_send() - WMI ant switch tbl cmd function
 *  @wmi_handle: handle to WMI.
 *  @param: pointer to hold tx mode selection param
 *
 *  Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_set_radio_tx_mode_select_cmd_send(
		wmi_unified_t wmi_handle,
		struct wmi_pdev_enable_tx_mode_selection *tx_mode_select_param);

/**
 * wmi_unified_send_lcr_cmd() - Send LCR command to FW
 * @wmi_handle: WMI handle
 * @lcr_info: Pointer to LCR structure
 *
 * Return: QDF_STATUS_SUCCESS if success, else returns proper error code.
 */
QDF_STATUS wmi_unified_send_lcr_cmd(wmi_unified_t wmi_handle,
				    struct wmi_wifi_pos_lcr_info *lcr_info);

/**
 * wmi_unified_send_lci_cmd() - Send LCI command to FW
 * @wmi_handle: WMI handle
 * @lci_info: Pointer to LCI structure
 *
 * Return: QDF_STATUS_SUCCESS if success, else returns proper error code.
 */
QDF_STATUS wmi_unified_send_lci_cmd(wmi_unified_t wmi_handle,
				    struct wifi_pos_lci_info *lci_info);

#ifdef WLAN_SUPPORT_MESH_LATENCY
/**
 * wmi_unified_config_vdev_tid_latency_info_cmd_send() - WMI for vdev latency
 * @wmi_handle: wmi handle
 * @param: pointer to hold vdev tid latency config param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_config_vdev_tid_latency_info_cmd_send(
		wmi_unified_t wmi_hdl,
		struct wmi_vdev_tid_latency_config_params
		*vdev_tid_latency_config_param);

/**
 * wmi_unified_config_peer_latency_info_cmd_send() - WMI for peer latency
 * @wmi_handle: wmi handle
 * @param: pointer to hold peer latency config param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_config_peer_latency_info_cmd_send(
		wmi_unified_t wmi_hdl,
		struct wmi_peer_latency_config_params
		*param);
#endif

#ifdef QCA_STANDALONE_SOUNDING_TRIGGER
/**
 * wmi_unified_txbf_sounding_trig_info_cmd_send() - WMI for txbf sounding for peers
 * @wmi_handle: wmi handle
 * @sounding_params: pointer to hold txbf sounding config param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS
wmi_unified_txbf_sounding_trig_info_cmd_send(struct wmi_unified *wmi_handle,
					     struct wmi_txbf_sounding_trig_param
					     *sounding_params);
#endif

#ifdef WLAN_WSI_STATS_SUPPORT
/**
 * wmi_unified_config_wsi_stats_info_cmd_send() - send WSI stats info for PDEV
 * @wmi_handle: wmi_handle
 * @param: pointer to hold ingress and egress information
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_config_wsi_stats_info_cmd_send(
		wmi_unified_t wmi_hdl,
		struct wmi_wsi_stats_info_params
		*param);
#endif

#ifdef QCA_MANUAL_TRIGGERED_ULOFDMA
/**
 * wmi_unified_config_trigger_ulofdma_su_cmd_send() - trig ulofdma for SU
 * @wmi_handle: wmi handle
 * @param: pointer to hold SU trigger ulofdma param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_config_trigger_ulofdma_su_cmd_send(
		wmi_unified_t wmi_hdl,
		struct wmi_trigger_ul_ofdma_su_params
		*param);

/**
 * wmi_unified_config_trigger_ulofdma_mu_cmd_send() - trig ulofdma for MU
 * @wmi_handle: wmi handle
 * @param: pointer to hold MU trigger ulofdma param
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_config_trigger_ulofdma_mu_cmd_send(
		wmi_unified_t wmi_hdl,
		struct wmi_trigger_ul_ofdma_mu_params
		*param);
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
					struct wmi_intra_bss_params *param);

/**
 * wmi_unified_peer_set_intra_bss_cmd_send() - set cmd to config intra_bss
 * @wmi_handle: wmi handle
 * @param: params needed for intr_bss config
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS
wmi_unified_peer_set_intra_bss_cmd_send(struct wmi_unified *wmi_handle,
					struct wmi_intra_bss_params *param);

/**
 * wmi_unified_soc_tqm_reset_enable_disable_cmd() - Send tqm reset command to FW
 * @wmi_handle: wmi handle
 * @enable: enable or disable configuration from user
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS
wmi_unified_soc_tqm_reset_enable_disable_cmd(wmi_unified_t wmi_handle,
					     uint32_t enable);

/**
 * wmi_unified_set_peer_disable_mode() - set peer disabled modes
 * @wmi_handle: wmi handle
 * @peer_mac: peer mac address
 * @pdev_id: pdev id
 * @disabled_modes: disabled modes
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS
wmi_unified_set_peer_disable_mode(wmi_unified_t wmi_handle,
				  uint8_t *peer_mac,
				  uint8_t pdev_id,
				  uint32_t disabled_modes);
#ifdef CONFIG_SAWF_DEF_QUEUES
/**
 * wmi_unified_set_rate_upper_cap_cmd_send() - set rate upper cap cmd
 * @wmi_handle: wmi handle
 * @pdev_id: pdev id
 * @param: rate upper cap parameters
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS
wmi_unified_set_rate_upper_cap_cmd_send(struct wmi_unified *wmi_handle,
					uint8_t pdev_id,
					struct wmi_rc_params *param);

/**
 * wmi_unified_set_rate_retry_mcs_drop_cmd_send() - set rate retry and mcs drop
 * @wmi_handle: wmi handle
 * @pdev_id: pdev id
 * @param: rate retry and mcs drop parameters
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS
wmi_unified_set_rate_retry_mcs_drop_cmd_send(struct wmi_unified *wmi_handle,
					     uint8_t pdev_id,
					     struct wmi_rc_params *param);

/**
 * wmi_unified_set_mcs_probe_intvl_cmd_send() - set mcs probe interval
 * @wmi_handle: wmi handle
 * @pdev_id: pdev id
 * @param: mcs probe interval parameters
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS
wmi_unified_set_mcs_probe_intvl_cmd_send(struct wmi_unified *wmi_handle,
					 uint8_t pdev_id,
					 struct wmi_rc_params *param);

/**
 * wmi_unified_set_nss_probe_intvl_cmd_send() - set nss probe interval
 * @wmi_handle: wmi handle
 * @pdev_id: pdev id
 * @param: nss probe interval parameters
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS
wmi_unified_set_nss_probe_intvl_cmd_send(struct wmi_unified *wmi_handle,
					 uint8_t pdev_id,
					 struct wmi_rc_params *param);

/**
 * wmi_sawf_create_send() - Send create SAWF service class command to FW
 * @wmi_handle: wmi handle
 * @param: SAWF WMI params
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_sawf_create_send(struct wmi_unified *wmi_handle,
				struct wmi_sawf_params *param);

/**
 * wmi_sawf_disable_send() - Send disable service class command to FW
 * @wmi_handle: wmi handle
 * @svc_id: Service class identifier
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_sawf_disable_send(struct wmi_unified *wmi_handle,
				 uint8_t svc_id);
#endif

#ifdef QCA_STANDALONE_SOUNDING_TRIGGER
/*
 * wmi_extract_standalone_sounding_complete_event_params() - extract standalone
 * sounding complete event
 * @wmi_handle: wmi handle
 * @evt_buf: pointer to event buffer
 * @ss_complete: Pointer to sounding command complete event params
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_extract_standalone_sounding_evt_params(
		wmi_unified_t wmi_handle, void *evt_buf,
		struct wmi_host_standalone_sounding_evt_params *ss_params);
#endif /* QCA_STANDALONE_SOUNDING_TRIGGER */

/**
 * wmi_unified_tdma_schedule_send() - Send tdma schedule command to FW
 * @wmi_handle: wmi handle
 * @param: tdma schedule parameters
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_tdma_schedule_send(
		struct wmi_unified *wmi_handle,
		struct wlan_tdma_sched_cmd_param *param);

#ifdef WLAN_FEATURE_11BE_MLO
/**
 * wmi_unified_link_recmnd_info_send() - API to send the link recommendation
 * @wmi_handle: WMI handle
 * @param: structure to get link recommendation config
 *
 * return: QDF_STATUS
 */
QDF_STATUS wmi_unified_link_recmnd_info_send(
		wmi_unified_t wmi_handle,
		struct wlan_link_recmnd_param *param);
#else
static inline QDF_STATUS wmi_unified_link_recmnd_info_send(
		wmi_unified_t wmi_handle,
		struct wlan_link_recmnd_param *param)
{
	return QDF_STATUS_SUCCESS;
}
#endif

#ifdef WLAN_FEATURE_11BE
/**
 * wmi_unified_send_mu_on_off_cmd() - send MU toggle duration command
 * @wmi: WMI handle
 * @params: Pointer to MU toggle duration params
 *
 * Return: QDF_STATUS_SUCCESS on success and QDF_STATUS_E_FAILURE for failure
 */
QDF_STATUS wmi_unified_send_mu_on_off_cmd(wmi_unified_t wmi,
				  struct wmi_host_mu_on_off_params *params);
#endif /* WLAN_FEATURE_11BE */

#ifdef WLAN_SUPPORT_TX_PKT_CAP_CUSTOM_CLASSIFY
/**
 * wmi_unified_set_tx_pkt_cap_custom_classify() - api to add
 * the protocols to be classified by tx packet capture
 * @wmi_hdl: wmi handle
 * @param: Packet type information
 *
 * Return: QDF_STATUS_SUCCESS for success or error code
 */
QDF_STATUS wmi_unified_set_tx_pkt_cap_custom_classify(
		wmi_unified_t wmi_handle,
		struct wmi_tx_pkt_cap_custom_classify_info *param);
#endif /* WLAN_SUPPORT_TX_PKT_CAP_CUSTOM_CLASSIFY */
#endif /* _WMI_UNIFIED_AP_API_H_ */
