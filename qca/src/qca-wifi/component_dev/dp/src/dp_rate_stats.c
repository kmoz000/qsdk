/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
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

/**
 * @file: dp_rate_stats.c
 * @breief: Core peer rate statistics processing module
 */

#include "dp_rate_stats.h"
#include "dp_rate_stats_pub.h"

#ifdef QCA_SUPPORT_RDK_STATS

/* Calculate actual BW from BW ENUM as in
 * x = 0 for 20MHz
 * x = 1 for 40MHz
 * x = 2 for 80MHz
 * x = 3 for 160MHz
 */
#define GET_BW_FROM_BW_ENUM(x) ((20) * (1 << x))
#define FLUSH_OVERFLOW_CHECK (1 << 31)

static void
wlan_peer_flush_avg_rate_stats(struct wlan_soc_rate_stats_ctx *soc_stats_ctx,
			       struct wlan_peer_rate_stats_ctx *stats_ctx)
{
	struct wlan_peer_rate_stats_intf buf;
	struct wlan_peer_avg_rate_stats *avg_stats;

	if (!soc_stats_ctx)
		return;

	avg_stats = &stats_ctx->avg;
	buf.stats = (struct wlan_avg_rate_stats *)&avg_stats->stats;
	buf.buf_len = sizeof(struct wlan_avg_rate_stats);
	buf.stats_type = DP_PEER_AVG_RATE_STATS;
	buf.cookie = stats_ctx->peer_cookie;
	qdf_mem_copy(buf.peer_mac, stats_ctx->mac_addr, WLAN_MAC_ADDR_LEN);
	cdp_peer_flush_rate_stats(soc_stats_ctx->soc, stats_ctx->pdev_id, &buf);

	soc_stats_ctx->txs_cache_flush++;
	dp_info("txs_cache_flush: %d", soc_stats_ctx->txs_cache_flush);

	qdf_mem_zero(&avg_stats->stats, sizeof(avg_stats->stats));
}

static int
wlan_peer_update_avg_tx_rate_stats_user(
				struct wlan_avg_rate_stats *avg,
				struct cdp_tx_completion_ppdu *ppdu,
				struct cdp_tx_completion_ppdu_user *user)
{
	enum wlan_rate_ppdu_type type;
	uint32_t flush;
	uint32_t kbps;
	uint16_t fc = qdf_cpu_to_le16(ppdu->frame_ctrl);
	bool is_pm = fc & (QDF_IEEE80211_FC1_PM << 8); // shift by 8 to get FC1 field
	bool is_data = (fc & QDF_IEEE80211_FC0_TYPE_MASK) ==
			QDF_IEEE80211_FC0_TYPE_DATA &&
			((fc & QDF_IEEE80211_FC0_SUBTYPE_MASK) ==
			 QDF_IEEE80211_FC0_SUBTYPE_DATA ||
			(fc & QDF_IEEE80211_FC0_SUBTYPE_MASK) ==
			QDF_IEEE80211_FC0_SUBTYPE_QOS);

	switch (user->ppdu_type) {
	case WLAN_RATE_SU:
		type = WLAN_RATE_SU;
		break;
	case WLAN_RATE_MU_MIMO:
		type = WLAN_RATE_MU_MIMO;
		break;
	case WLAN_RATE_MU_OFDMA:
		type = WLAN_RATE_MU_OFDMA;
		break;
	case WLAN_RATE_MU_OFDMA_MIMO:
		type = WLAN_RATE_MU_OFDMA_MIMO;
		break;
	default:
		dp_info("Invalid ppdu type");
		return 0;
	}

	kbps = user->tx_ratekbps;
	if (kbps == 0) {
		dp_err("rate is invalid");
		return 0;
	}

	if (!is_pm && is_data) {
		avg->tx[type].num_ppdu++;
		avg->tx[type].sum_mbps += kbps / CDP_NUM_KB_IN_MB;
	}

	avg->tx[type].num_mpdu += user->mpdu_success;
	avg->tx[type].num_retry += user->mpdu_failed;

	if (!(user->is_mcast) && user->ack_rssi_valid) {
		avg->tx[type].num_snr++;
		avg->tx[type].sum_snr += user->usr_ack_rssi;
	}

	flush = 0;
	flush |= avg->tx[type].num_ppdu & FLUSH_OVERFLOW_CHECK;
	flush |= avg->tx[type].sum_mbps & FLUSH_OVERFLOW_CHECK;
	flush |= avg->tx[type].num_snr & FLUSH_OVERFLOW_CHECK;
	flush |= avg->tx[type].sum_snr & FLUSH_OVERFLOW_CHECK;

	return flush ? 1 : 0;
}

static void
wlan_peer_update_avg_tx_rate_stats(
				struct wlan_soc_rate_stats_ctx *soc_stats_ctx,
				struct cdp_tx_completion_ppdu *ppdu)
{
	struct wlan_peer_rate_stats_ctx *stats_ctx;
	struct wlan_avg_rate_stats *avg;
	struct cdp_tx_completion_ppdu_user *user;
	int i;

	if (soc_stats_ctx->stats_ver != PEER_EXT_RATE_STATS &&
	    soc_stats_ctx->stats_ver != PEER_EXT_ALL_STATS)
		return;

	for (i = 0; i < ppdu->num_users; i++) {
		user = &ppdu->user[i];
		STATS_CTX_LOCK_ACQUIRE(&soc_stats_ctx->tx_ctx_lock);
		if (user->peer_id == CDP_INVALID_PEER) {
			dp_warn("no valid peer \n");
			STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->tx_ctx_lock);
			continue;
		}

		stats_ctx = cdp_peer_get_peerstats_ctx(soc_stats_ctx->soc,
						       ppdu->vdev_id,
						       user->mac_addr);
		if (qdf_unlikely(!stats_ctx)) {
			dp_warn("peer rate stats ctx is NULL, return");
			dp_warn("peer_mac:  " QDF_MAC_ADDR_FMT,
				QDF_MAC_ADDR_REF(user->mac_addr));
			STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->tx_ctx_lock);
			continue;
		}

		avg = &stats_ctx->avg.stats;

		if (wlan_peer_update_avg_tx_rate_stats_user(avg, ppdu, user))
			wlan_peer_flush_avg_rate_stats(soc_stats_ctx,
						       stats_ctx);
		STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->tx_ctx_lock);
	}
}

static int
wlan_peer_update_avg_rx_rate_stats_user(struct wlan_avg_rate_stats *avg,
					struct cdp_rx_indication_ppdu *ppdu,
					struct cdp_rx_stats_ppdu_user *user)
{
	enum wlan_rate_ppdu_type type;
	uint32_t flush;
	uint32_t kbps;
	uint32_t nss;
	uint32_t mcs;
	uint16_t fc = qdf_cpu_to_le16(ppdu->frame_ctrl);
	bool is_pm = fc & (QDF_IEEE80211_FC1_PM << 8); // shift by 8 to get FC1 field
	bool is_data = (fc & QDF_IEEE80211_FC0_TYPE_MASK) ==
			QDF_IEEE80211_FC0_TYPE_DATA &&
			((fc & QDF_IEEE80211_FC0_SUBTYPE_MASK) ==
			 QDF_IEEE80211_FC0_SUBTYPE_DATA ||
			(fc & QDF_IEEE80211_FC0_SUBTYPE_MASK) ==
			QDF_IEEE80211_FC0_SUBTYPE_QOS);

	switch (ppdu->u.ppdu_type) {
	case WLAN_RATE_SU:
		type = WLAN_RATE_SU;
		break;
	case WLAN_RATE_MU_MIMO:
		type = WLAN_RATE_MU_MIMO;
		break;
	case WLAN_RATE_MU_OFDMA:
		type = WLAN_RATE_MU_OFDMA;
		break;
	case WLAN_RATE_MU_OFDMA_MIMO:
		type = WLAN_RATE_MU_OFDMA_MIMO;
		break;
	default:
		dp_info("Invalid ppdu type");
		return 0;
	}

	mcs = ppdu->u.mcs;
	nss = ppdu->u.nss; /* apparently ppdu->nss counts from 1 */

	if (user->mu_ul_info_valid) {
		/* apparently ppdu_type won't reflect ul ofdma properly
		 * moreover, its possible to get 1 user ul ofdma
		 * this is ambiguous, but it makes most sense to consider these
		 * as SU because it can be used to avoid interference too.
		 */
		if (ppdu->num_users > 1)
			type = WLAN_RATE_MU_OFDMA;

		mcs = user->mcs;
		nss = user->nss + 1; /* apparently user->nss counts from 0 */
	}

	kbps = user->rx_ratekbps;
	if (kbps == 0) {
		dp_err("rate is invalid");
		return 0;
	}

	if (!is_pm && is_data) {
		avg->rx[type].num_ppdu++;
		avg->rx[type].sum_mbps += kbps / CDP_NUM_KB_IN_MB;
	}

	avg->rx[type].num_snr++;
	avg->rx[type].sum_snr += ppdu->rssi; /* Info not available per user */
	avg->rx[type].num_mpdu += user->mpdu_cnt_fcs_ok;
	avg->rx[type].num_retry += user->mpdu_cnt_fcs_err;

	switch (ppdu->u.bw) {
	case CMN_BW_20MHZ:
		avg->rx[type].sum_snr += PKT_BW_GAIN_20MHZ;
		break;
	case CMN_BW_40MHZ:
		avg->rx[type].sum_snr += PKT_BW_GAIN_40MHZ;
		break;
	case CMN_BW_80MHZ:
		avg->rx[type].sum_snr += PKT_BW_GAIN_80MHZ;
		break;
	case CMN_BW_160MHZ:
		avg->rx[type].sum_snr += PKT_BW_GAIN_160MHZ;
		break;
	default:
		dp_info("Invalid BW index = %d", ppdu->u.bw);
	}

	flush = 0;
	flush |= avg->rx[type].num_ppdu & FLUSH_OVERFLOW_CHECK;
	flush |= avg->rx[type].sum_mbps & FLUSH_OVERFLOW_CHECK;
	flush |= avg->rx[type].num_snr & FLUSH_OVERFLOW_CHECK;
	flush |= avg->rx[type].sum_snr & FLUSH_OVERFLOW_CHECK;

	return flush ? 1 : 0;
}

static void
wlan_peer_update_avg_rx_rate_stats(
				struct wlan_soc_rate_stats_ctx *soc_stats_ctx,
				struct cdp_rx_indication_ppdu *ppdu)
{
	struct wlan_peer_rate_stats_ctx *stats_ctx;
	struct wlan_avg_rate_stats *avg;
	struct cdp_rx_stats_ppdu_user *user;
	int i;

	if (soc_stats_ctx->stats_ver != PEER_EXT_RATE_STATS &&
	    soc_stats_ctx->stats_ver != PEER_EXT_ALL_STATS)
		return;

	for (i = 0; i < ppdu->num_users; i++) {
		user = &ppdu->user[i];

		STATS_CTX_LOCK_ACQUIRE(&soc_stats_ctx->rx_ctx_lock);

		if (user->peer_id == CDP_INVALID_PEER) {
			dp_warn("no valid peer \n");
			STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->rx_ctx_lock);
			continue;
		}

		stats_ctx = cdp_peer_get_peerstats_ctx(soc_stats_ctx->soc,
						       ppdu->vdev_id,
						       user->mac_addr);
		if (qdf_unlikely(!stats_ctx)) {
			dp_warn("peer rate stats ctx is NULL, return");
			dp_warn("peer_mac:  " QDF_MAC_ADDR_FMT,
				QDF_MAC_ADDR_REF(user->mac_addr));
			STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->rx_ctx_lock);
			continue;
		}

		avg = &stats_ctx->avg.stats;

		if (wlan_peer_update_avg_rx_rate_stats_user(avg, ppdu, user))
			wlan_peer_flush_avg_rate_stats(soc_stats_ctx,
						       stats_ctx);
		STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->rx_ctx_lock);
	}
}

static void
wlan_peer_read_ewma_avg_rssi(struct wlan_rx_rate_stats *rx_stats)
{
	uint8_t ant, ht, cache_idx;

	for (cache_idx = 0; cache_idx < WLANSTATS_CACHE_SIZE; cache_idx++) {
		rx_stats->avg_rssi.internal =
			qdf_ewma_rx_rssi_read(&rx_stats->avg_rssi);

		for (ant = 0; ant < SS_COUNT; ant++) {
			for (ht = 0; ht < MAX_BW; ht++) {
				rx_stats->avg_rssi_ant[ant][ht].internal =
				qdf_ewma_rx_rssi_read(
					&rx_stats->avg_rssi_ant[ant][ht]);
			}
		}
		rx_stats += 1;
	}
}

static void
wlan_peer_flush_rx_rate_stats(struct wlan_soc_rate_stats_ctx *soc_stats_ctx,
			      struct wlan_peer_rate_stats_ctx *stats_ctx)
{
	struct wlan_peer_rate_stats_intf buf;
	struct wlan_peer_rx_rate_stats *rx_stats;
	uint8_t idx;

	if (!soc_stats_ctx)
		return;

	rx_stats = &stats_ctx->rate_stats->rx;

	buf.cookie = 0;
	wlan_peer_read_ewma_avg_rssi(rx_stats->stats);
	buf.stats = (struct wlan_rx_rate_stats *)rx_stats->stats;
	buf.buf_len = WLANSTATS_CACHE_SIZE * sizeof(struct wlan_rx_rate_stats);
	buf.stats_type = DP_PEER_RX_RATE_STATS;
	/* Prepare 64 bit cookie */
	/*-------------------|-------------------|
	 *  32 bit target    | 32 bit peer cookie|
	 *-------------------|-------------------|
	 */
	buf.cookie = ((((buf.cookie | soc_stats_ctx->is_lithium)
		      << WLANSTATS_PEER_COOKIE_LSB) &
		      WLANSTATS_COOKIE_PLATFORM_OFFSET) |
		      (((buf.cookie | stats_ctx->peer_cookie) &
		      WLANSTATS_COOKIE_PEER_COOKIE_OFFSET)));
	qdf_mem_copy(buf.peer_mac, stats_ctx->mac_addr, WLAN_MAC_ADDR_LEN);
	cdp_peer_flush_rate_stats(soc_stats_ctx->soc,
				  stats_ctx->pdev_id, &buf);

	soc_stats_ctx->rxs_cache_flush++;
	dp_info("rxs_cache_flush: %d", soc_stats_ctx->rxs_cache_flush);

	qdf_mem_zero(rx_stats->stats, WLANSTATS_CACHE_SIZE *
		     sizeof(struct wlan_rx_rate_stats));
	for (idx = 0; idx < WLANSTATS_CACHE_SIZE; idx++)
		rx_stats->stats[idx].ratecode = INVALID_CACHE_IDX;

	rx_stats->cur_cache_idx = 0;
}

static void
wlan_peer_read_sojourn_average(struct wlan_peer_tx_rate_stats *tx_stats)
{
	uint8_t tid;

	for (tid = 0; tid < CDP_DATA_TID_MAX; tid++) {
		tx_stats->sojourn.avg_sojourn_msdu[tid].internal =
		qdf_ewma_tx_lag_read(&tx_stats->sojourn.avg_sojourn_msdu[tid]);
	}
}

static void
wlan_peer_flush_tx_rate_stats(struct wlan_soc_rate_stats_ctx *soc_stats_ctx,
			      struct wlan_peer_rate_stats_ctx *stats_ctx)
{
	struct wlan_peer_rate_stats_intf buf;
	struct wlan_peer_tx_rate_stats *tx_stats;
	uint8_t idx;
	uint8_t tid;

	if (!soc_stats_ctx)
		return;

	tx_stats = &stats_ctx->rate_stats->tx;

	buf.cookie = 0;
	buf.stats = (struct wlan_tx_rate_stats *)tx_stats->stats;
	buf.buf_len = (WLANSTATS_CACHE_SIZE * sizeof(struct wlan_tx_rate_stats)
		       + sizeof(struct wlan_tx_sojourn_stats));
	buf.stats_type = DP_PEER_TX_RATE_STATS;
	/* Prepare 64 bit cookie */
	/*-------------------|-------------------|
	 *  32 bit target    | 32 bit peer cookie|
	 *-------------------|-------------------|
	 */
	buf.cookie = ((((buf.cookie | soc_stats_ctx->is_lithium)
		      << WLANSTATS_PEER_COOKIE_LSB) &
		      WLANSTATS_COOKIE_PLATFORM_OFFSET) |
		      (((buf.cookie | stats_ctx->peer_cookie) &
		      WLANSTATS_COOKIE_PEER_COOKIE_OFFSET)));

	wlan_peer_read_sojourn_average(tx_stats);
	qdf_mem_copy(buf.peer_mac, stats_ctx->mac_addr, WLAN_MAC_ADDR_LEN);
	cdp_peer_flush_rate_stats(soc_stats_ctx->soc,
				  stats_ctx->pdev_id, &buf);

	soc_stats_ctx->txs_cache_flush++;
	dp_info("txs_cache_flush: %d", soc_stats_ctx->txs_cache_flush);

	qdf_mem_zero(tx_stats->stats, WLANSTATS_CACHE_SIZE *
		     sizeof(struct wlan_tx_rate_stats));

	for (tid = 0; tid < WLAN_DATA_TID_MAX; tid++) {
		tx_stats->sojourn.sum_sojourn_msdu[tid] = 0;
		tx_stats->sojourn.num_msdus[tid] = 0;
	}
	for (idx = 0; idx < WLANSTATS_CACHE_SIZE; idx++)
		tx_stats->stats[idx].ratecode = INVALID_CACHE_IDX;

	tx_stats->cur_cache_idx = 0;
}

#ifdef WLAN_FEATURE_11BE
static inline uint32_t
wlan_peer_get_bw_from_enum(uint32_t bw)
{
	uint32_t bw_value;

	switch (bw) {
	case CMN_BW_20MHZ:
	case CMN_BW_40MHZ:
	case CMN_BW_80MHZ:
	case CMN_BW_160MHZ:
		bw_value = GET_BW_FROM_BW_ENUM(bw);
		break;
	case CMN_BW_320MHZ:
		bw_value = 320;
		break;
	default:
		bw_value = 0;
	}

	return bw_value;
}

static inline uint32_t
wlan_peer_get_punc_bw_from_punc_enum(uint8_t punc_bw)
{
	uint32_t ret = 0;

	switch (punc_bw) {
	case NO_PUNCTURE:
	case PUNCTURED_20MHZ:
	case PUNCTURED_40MHZ:
		ret = 20 * punc_bw;
		break;
	case PUNCTURED_80MHZ:
		ret = 80;
		break;
	case PUNCTURED_120MHZ:
		ret = 120;
		break;
	default:
		return ret;
	}

	return ret;
}

static inline void
wlan_peer_flush_tx_punc_bw_stats(struct wlan_peer_tx_link_stats *tx_stats)
{
	uint8_t punc_bw_max_idx;

	if (tx_stats->stats.num_ppdus) {
		tx_stats->stats.punc_bw.usage_avg =
					 tx_stats->stats.punc_bw.usage_total /
					 tx_stats->stats.num_ppdus;
		punc_bw_max_idx = tx_stats->stats.punc_bw.usage_max;
		tx_stats->stats.punc_bw.usage_max =
		(tx_stats->stats.punc_bw.usage_counter[punc_bw_max_idx] * 100) /
		  tx_stats->stats.num_ppdus;
	}
}

static inline void
wlan_peer_flush_rx_punc_bw_stats(struct wlan_peer_rx_link_stats *rx_stats)
{
	uint8_t punc_bw_max_idx;

	if (rx_stats->stats.num_ppdus) {
		rx_stats->stats.punc_bw.usage_avg =
			rx_stats->stats.punc_bw.usage_total /
			rx_stats->stats.num_ppdus;
		punc_bw_max_idx = rx_stats->stats.punc_bw.usage_max;
		rx_stats->stats.punc_bw.usage_max =
		(rx_stats->stats.punc_bw.usage_counter[punc_bw_max_idx] * 100) /
		 rx_stats->stats.num_ppdus;
	}
}

static inline void
wlan_peer_update_tx_punc_bw_stats(struct wlan_tx_link_stats *tx_stats,
				  struct cdp_tx_completion_ppdu_user *ppdu_user)
{
	tx_stats->punc_bw.usage_total +=
		wlan_peer_get_punc_bw_from_punc_enum(ppdu_user->punc_mode);

	if (ppdu_user->punc_mode < MAX_PUNCTURED_MODE) {
		if (tx_stats->punc_bw.usage_max < ppdu_user->punc_mode)
			tx_stats->punc_bw.usage_max = ppdu_user->punc_mode;
		tx_stats->punc_bw.usage_counter[ppdu_user->punc_mode]++;
	}

}

static inline void
wlan_peer_update_rx_punc_bw_stats(struct wlan_rx_link_stats *rx_stats,
				  struct cdp_rx_indication_ppdu *cdp_rx_ppdu)
{
	rx_stats->punc_bw.usage_total +=
		wlan_peer_get_punc_bw_from_punc_enum(cdp_rx_ppdu->punc_bw);

	if (cdp_rx_ppdu->punc_bw < MAX_PUNCTURED_MODE) {
		if (rx_stats->punc_bw.usage_max < cdp_rx_ppdu->punc_bw)
			rx_stats->punc_bw.usage_max =
				cdp_rx_ppdu->punc_bw;
		rx_stats->punc_bw.usage_counter[cdp_rx_ppdu->punc_bw]++;
	}

}
#else
static inline uint32_t
wlan_peer_get_bw_from_enum(uint32_t bw)
{
	uint32_t bw_value;

	switch (bw) {
	case CMN_BW_20MHZ:
	case CMN_BW_40MHZ:
	case CMN_BW_80MHZ:
	case CMN_BW_160MHZ:
		bw_value = GET_BW_FROM_BW_ENUM(bw);
		break;
	default:
		bw_value = 0;
	}

	return bw_value;
}

static inline void
wlan_peer_flush_tx_punc_bw_stats(struct wlan_peer_tx_link_stats *tx_stats)
{ }

static inline void
wlan_peer_flush_rx_punc_bw_stats(struct wlan_peer_rx_link_stats *rx_stats)
{ }

static inline void
wlan_peer_update_tx_punc_bw_stats(struct wlan_tx_link_stats *tx_stats,
				  struct cdp_tx_completion_ppdu_user *ppdu_user)
{ }

static inline void
wlan_peer_update_rx_punc_bw_stats(struct wlan_rx_link_stats *rx_stats,
				  struct cdp_rx_indication_ppdu *cdp_rx_ppdu)
{ }
#endif

static void
wlan_peer_flush_tx_link_stats(struct wlan_soc_rate_stats_ctx *soc_stats_ctx,
			      struct wlan_peer_rate_stats_ctx *stats_ctx)
{
	struct wlan_peer_rate_stats_intf buf;
	struct wlan_peer_tx_link_stats *tx_stats;
	uint8_t bw_max_idx;

	if (!soc_stats_ctx) {
		dp_info("soc stats context is NULL\n");
		return;
	}

	tx_stats = &stats_ctx->link_metrics->tx;

	buf.cookie = 0;
	buf.stats = (struct wlan_tx_link_stats *)&tx_stats->stats;
	buf.buf_len = sizeof(struct wlan_peer_tx_link_stats);
	buf.stats_type = DP_PEER_TX_LINK_STATS;
	/* Prepare 64 bit cookie */
	/*-------------------|-------------------|
	 *  32 bit target    | 32 bit peer cookie|
	 *-------------------|-------------------|
	 */
	buf.cookie = ((((buf.cookie | soc_stats_ctx->is_lithium)
		      << WLANSTATS_PEER_COOKIE_LSB) &
		      WLANSTATS_COOKIE_PLATFORM_OFFSET) |
		      (((buf.cookie | stats_ctx->peer_cookie) &
		      WLANSTATS_COOKIE_PEER_COOKIE_OFFSET)));

	tx_stats->stats.ack_rssi.internal =
			qdf_ewma_rx_rssi_read(&tx_stats->stats.ack_rssi);
	tx_stats->stats.phy_rate_actual_su =
			dp_ath_rate_out(tx_stats->stats.phy_rate_lpf_avg_su);
	tx_stats->stats.phy_rate_actual_mu =
			dp_ath_rate_out(tx_stats->stats.phy_rate_lpf_avg_mu);

	if (tx_stats->stats.num_ppdus) {
		tx_stats->stats.bw.usage_avg = tx_stats->stats.bw.usage_total /
					       tx_stats->stats.num_ppdus;
		bw_max_idx = tx_stats->stats.bw.usage_max;
		tx_stats->stats.bw.usage_max =
			(tx_stats->stats.bw.usage_counter[bw_max_idx] * 100) /
			 tx_stats->stats.num_ppdus;
	}

	wlan_peer_flush_tx_punc_bw_stats(tx_stats);
	if (tx_stats->stats.mpdu_success)
		tx_stats->stats.pkt_error_rate =
			(tx_stats->stats.mpdu_failed * 100) /
			 tx_stats->stats.mpdu_success;

	qdf_mem_copy(buf.peer_mac, stats_ctx->mac_addr, WLAN_MAC_ADDR_LEN);
	cdp_peer_flush_rate_stats(soc_stats_ctx->soc,
				  stats_ctx->pdev_id, &buf);

	qdf_mem_zero(&tx_stats->stats, sizeof(struct wlan_tx_link_stats));
}

static void
wlan_peer_flush_rx_link_stats(struct wlan_soc_rate_stats_ctx *soc_stats_ctx,
			      struct wlan_peer_rate_stats_ctx *stats_ctx)
{
	struct wlan_peer_rate_stats_intf buf;
	struct wlan_peer_rx_link_stats *rx_stats;
	uint8_t bw_max_idx;

	if (!soc_stats_ctx) {
		dp_info("soc stats context is NULL\n");
		return;
	}

	rx_stats = &stats_ctx->link_metrics->rx;

	buf.cookie = 0;
	buf.stats = (struct wlan_rx_link_stats *)&rx_stats->stats;
	buf.buf_len = sizeof(struct wlan_peer_rx_link_stats);
	buf.stats_type = DP_PEER_RX_LINK_STATS;
	/* Prepare 64 bit cookie */
	/*-------------------|-------------------|
	 *  32 bit target    | 32 bit peer cookie|
	 *-------------------|-------------------|
	 */
	buf.cookie = ((((buf.cookie | soc_stats_ctx->is_lithium)
		      << WLANSTATS_PEER_COOKIE_LSB) &
		      WLANSTATS_COOKIE_PLATFORM_OFFSET) |
		      (((buf.cookie | stats_ctx->peer_cookie) &
		      WLANSTATS_COOKIE_PEER_COOKIE_OFFSET)));

	rx_stats->stats.su_rssi.internal =
			qdf_ewma_rx_rssi_read(&rx_stats->stats.su_rssi);
	rx_stats->stats.phy_rate_actual_su =
			dp_ath_rate_out(rx_stats->stats.phy_rate_lpf_avg_su);
	rx_stats->stats.phy_rate_actual_mu =
			dp_ath_rate_out(rx_stats->stats.phy_rate_lpf_avg_mu);
	if (rx_stats->stats.num_ppdus) {
		rx_stats->stats.bw.usage_avg = rx_stats->stats.bw.usage_total /
					       rx_stats->stats.num_ppdus;
		bw_max_idx = rx_stats->stats.bw.usage_max;
		rx_stats->stats.bw.usage_max =
			(rx_stats->stats.bw.usage_counter[bw_max_idx] * 100) /
			 rx_stats->stats.num_ppdus;
	}

	wlan_peer_flush_rx_punc_bw_stats(rx_stats);
	if (rx_stats->stats.num_mpdus)
		rx_stats->stats.pkt_error_rate =
					(rx_stats->stats.mpdu_retries * 100) /
					 rx_stats->stats.num_mpdus;

	qdf_mem_copy(buf.peer_mac, stats_ctx->mac_addr, WLAN_MAC_ADDR_LEN);
	cdp_peer_flush_rate_stats(soc_stats_ctx->soc,
				  stats_ctx->pdev_id, &buf);

	qdf_mem_zero(&rx_stats->stats, sizeof(struct wlan_rx_link_stats));
}

static void
wlan_peer_flush_rate_stats(struct wlan_soc_rate_stats_ctx *soc_stats_ctx,
			   struct wlan_peer_rate_stats_ctx *stats_ctx)
{
	if (soc_stats_ctx->stats_ver == PEER_EXT_RATE_STATS ||
	    soc_stats_ctx->stats_ver == PEER_EXT_ALL_STATS) {

		wlan_peer_flush_tx_rate_stats(soc_stats_ctx, stats_ctx);

		wlan_peer_flush_rx_rate_stats(soc_stats_ctx, stats_ctx);

		wlan_peer_flush_avg_rate_stats(soc_stats_ctx, stats_ctx);
	}

	if (soc_stats_ctx->stats_ver == PEER_EXT_LINK_STATS ||
	    soc_stats_ctx->stats_ver == PEER_EXT_ALL_STATS) {

		wlan_peer_flush_tx_link_stats(soc_stats_ctx, stats_ctx);

		wlan_peer_flush_rx_link_stats(soc_stats_ctx, stats_ctx);
	}
}

void wlan_peer_rate_stats_flush_req(void *ctx, enum WDI_EVENT event,
				    void *buf, uint16_t peer_id,
				    uint32_t type)
{
	struct wlan_soc_rate_stats_ctx *soc_stats_ctx =
					(struct wlan_soc_rate_stats_ctx *)ctx;
	if (buf) {
		STATS_CTX_LOCK_ACQUIRE(&soc_stats_ctx->tx_ctx_lock);
		STATS_CTX_LOCK_ACQUIRE(&soc_stats_ctx->rx_ctx_lock);
		wlan_peer_flush_rate_stats(ctx, buf);
		STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->rx_ctx_lock);
		STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->tx_ctx_lock);
	}
}

static inline void
__wlan_peer_update_rx_rate_stats(struct wlan_rx_rate_stats *__rx_stats,
				 struct cdp_rx_indication_ppdu *cdp_rx_ppdu,
				 uint8_t user_idx)
{
	uint8_t ant, ht, mcs, nss, rssi;
	struct cdp_rx_stats_ppdu_user *ppdu_user = &cdp_rx_ppdu->user[user_idx];

	if (cdp_rx_ppdu->u.ppdu_type != DP_PPDU_TYPE_SU) {
		mcs = ppdu_user->mcs;
		nss = ppdu_user->nss;
	} else {
		mcs = cdp_rx_ppdu->u.mcs;
		nss = cdp_rx_ppdu->u.nss;
	}

	if (ppdu_user->rix == -1) {
		__rx_stats->ratecode = ppdu_user->rix;
	} else {
		__rx_stats->ratecode = ASSEMBLE_STATS_CODE(ppdu_user->rix,
							   nss, mcs,
							   cdp_rx_ppdu->u.bw);
	}

	__rx_stats->rate = ppdu_user->rx_ratekbps;
	__rx_stats->num_bytes += ppdu_user->mpdu_ok_byte_count;
	__rx_stats->num_msdus += ppdu_user->num_msdu;
	__rx_stats->num_mpdus += ppdu_user->mpdu_cnt_fcs_ok;
	__rx_stats->num_retries += ppdu_user->retries;
	__rx_stats->num_ppdus += 1;

	if (cdp_rx_ppdu->u.gi == CDP_SGI_0_4_US)
		__rx_stats->num_sgi++;

	qdf_ewma_rx_rssi_add(&__rx_stats->avg_rssi, cdp_rx_ppdu->rssi);

	for (ant = 0; ant < SS_COUNT; ant++) {
		for (ht = 0; ht < MAX_BW; ht++) {
			rssi = cdp_rx_ppdu->rssi_chain[ant][ht];
			qdf_ewma_rx_rssi_add(&__rx_stats->avg_rssi_ant[ant][ht],
					     rssi);
		}
	}
}

static void
wlan_peer_update_tx_link_stats(struct wlan_soc_rate_stats_ctx *soc_stats_ctx,
			       struct cdp_tx_completion_ppdu *cdp_tx_ppdu)
{
	struct cdp_tx_completion_ppdu_user *ppdu_user;
	struct wlan_peer_rate_stats_ctx *stats_ctx;
	struct wlan_tx_link_stats *tx_stats;
	uint8_t user_idx;

	if (soc_stats_ctx->stats_ver != PEER_EXT_LINK_STATS &&
	    soc_stats_ctx->stats_ver != PEER_EXT_ALL_STATS)
		return;

	for (user_idx = 0; user_idx < cdp_tx_ppdu->num_users; user_idx++) {
		ppdu_user = &cdp_tx_ppdu->user[user_idx];

		STATS_CTX_LOCK_ACQUIRE(&soc_stats_ctx->tx_ctx_lock);
		stats_ctx = cdp_peer_get_peerstats_ctx(soc_stats_ctx->soc,
						       cdp_tx_ppdu->vdev_id,
						       ppdu_user->mac_addr);

		if (qdf_unlikely(!stats_ctx)) {
			dp_warn("peer rate stats ctx is NULL, return");
			dp_warn("peer_mac:  " QDF_MAC_ADDR_FMT,
				QDF_MAC_ADDR_REF(ppdu_user->mac_addr));
			STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->tx_ctx_lock);
			continue;
		}

		tx_stats = &stats_ctx->link_metrics->tx.stats;


		tx_stats->num_ppdus += ppdu_user->long_retries + 1;
		tx_stats->bytes += ppdu_user->success_bytes;
		tx_stats->mpdu_failed += ppdu_user->mpdu_failed;
		tx_stats->mpdu_success += ppdu_user->mpdu_success;

		if (ppdu_user->ppdu_type == DP_PPDU_TYPE_SU) {
			tx_stats->phy_rate_lpf_avg_su =
				dp_ath_rate_lpf(tx_stats->phy_rate_lpf_avg_su,
						ppdu_user->tx_ratekbps);
		} else if (ppdu_user->ppdu_type == DP_PPDU_TYPE_MU_OFDMA ||
			   ppdu_user->ppdu_type == DP_PPDU_TYPE_MU_MIMO) {
			tx_stats->phy_rate_lpf_avg_mu =
				dp_ath_rate_lpf(tx_stats->phy_rate_lpf_avg_mu,
						ppdu_user->tx_ratekbps);

			if (ppdu_user->ppdu_type == DP_PPDU_TYPE_MU_OFDMA)
				tx_stats->ofdma_usage++;

			if (ppdu_user->ppdu_type == DP_PPDU_TYPE_MU_MIMO)
				tx_stats->mu_mimo_usage++;
		}

		tx_stats->bw.usage_total +=
				wlan_peer_get_bw_from_enum(ppdu_user->bw);
		if (ppdu_user->bw < BW_USAGE_MAX_SIZE) {
			if (tx_stats->bw.usage_max < ppdu_user->bw)
				tx_stats->bw.usage_max = ppdu_user->bw;
			tx_stats->bw.usage_counter[ppdu_user->bw]++;
		}

		wlan_peer_update_tx_punc_bw_stats(tx_stats, ppdu_user);
		if (ppdu_user->ack_rssi_valid)
			qdf_ewma_rx_rssi_add(&tx_stats->ack_rssi,
					     ppdu_user->usr_ack_rssi);

		STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->tx_ctx_lock);
	}
}

static void
wlan_peer_update_rx_link_stats(struct wlan_soc_rate_stats_ctx *soc_stats_ctx,
			       struct cdp_rx_indication_ppdu *cdp_rx_ppdu)
{
	struct cdp_rx_stats_ppdu_user *ppdu_user;
	struct wlan_peer_rate_stats_ctx *stats_ctx;
	struct wlan_rx_link_stats *rx_stats;
	uint8_t user_idx;

	if (soc_stats_ctx->stats_ver != PEER_EXT_LINK_STATS &&
	    soc_stats_ctx->stats_ver != PEER_EXT_ALL_STATS)
		return;

	for (user_idx = 0;
	     user_idx < cdp_rx_ppdu->num_users && user_idx < CDP_MU_MAX_USERS;
	     user_idx++) {
		ppdu_user = &cdp_rx_ppdu->user[user_idx];

		if (ppdu_user->peer_id == CDP_INVALID_PEER)
			continue;

		STATS_CTX_LOCK_ACQUIRE(&soc_stats_ctx->rx_ctx_lock);
		stats_ctx = cdp_peer_get_peerstats_ctx(soc_stats_ctx->soc,
						       ppdu_user->vdev_id,
						       ppdu_user->mac_addr);

		if (qdf_unlikely(!stats_ctx)) {
			dp_warn("peer rate stats ctx is NULL, return");
			dp_warn("peer_mac:  " QDF_MAC_ADDR_FMT,
				QDF_MAC_ADDR_REF(ppdu_user->mac_addr));
			STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->rx_ctx_lock);
			continue;
		}

		rx_stats = &stats_ctx->link_metrics->rx.stats;

		rx_stats->num_ppdus++;
		rx_stats->bytes += ppdu_user->mpdu_ok_byte_count;
		rx_stats->mpdu_retries += ppdu_user->retries;
		rx_stats->num_mpdus += ppdu_user->mpdu_cnt_fcs_ok;

		if (cdp_rx_ppdu->u.ppdu_type == DP_PPDU_TYPE_SU) {
			rx_stats->phy_rate_lpf_avg_su =
				dp_ath_rate_lpf(rx_stats->phy_rate_lpf_avg_su,
						cdp_rx_ppdu->rx_ratekbps);
			qdf_ewma_rx_rssi_add(&rx_stats->su_rssi,
					     cdp_rx_ppdu->rssi);
		} else if (cdp_rx_ppdu->u.ppdu_type == DP_PPDU_TYPE_MU_OFDMA ||
			   cdp_rx_ppdu->u.ppdu_type == DP_PPDU_TYPE_MU_MIMO) {
			rx_stats->phy_rate_lpf_avg_mu =
				dp_ath_rate_lpf(rx_stats->phy_rate_lpf_avg_mu,
						cdp_rx_ppdu->rx_ratekbps);

			if (cdp_rx_ppdu->u.ppdu_type == DP_PPDU_TYPE_MU_OFDMA)
				rx_stats->ofdma_usage++;

			if (cdp_rx_ppdu->u.ppdu_type == DP_PPDU_TYPE_MU_MIMO)
				rx_stats->mu_mimo_usage++;
		}

		rx_stats->bw.usage_total +=
				wlan_peer_get_bw_from_enum(cdp_rx_ppdu->u.bw);
		if (cdp_rx_ppdu->u.bw < BW_USAGE_MAX_SIZE) {
			if (rx_stats->bw.usage_max < cdp_rx_ppdu->u.bw)
				rx_stats->bw.usage_max = cdp_rx_ppdu->u.bw;
			rx_stats->bw.usage_counter[cdp_rx_ppdu->u.bw]++;
		}

		wlan_peer_update_rx_punc_bw_stats(rx_stats, cdp_rx_ppdu);
		STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->rx_ctx_lock);
	}
}

static void
wlan_peer_update_rx_rate_stats(struct wlan_soc_rate_stats_ctx *soc_stats_ctx,
			       struct cdp_rx_indication_ppdu *cdp_rx_ppdu)
{
	struct wlan_peer_rate_stats_ctx *stats_ctx;
	struct wlan_peer_rx_rate_stats *rx_stats;
	struct wlan_rx_rate_stats *__rx_stats;
	struct cdp_rx_stats_ppdu_user *ppdu_user;
	uint8_t cache_idx;
	uint8_t user_idx;
	uint8_t max_users;
	bool idx_match = false;

	if (soc_stats_ctx->stats_ver != PEER_EXT_RATE_STATS &&
	    soc_stats_ctx->stats_ver != PEER_EXT_ALL_STATS)
		return;

	user_idx = 0;
	max_users = QDF_MIN(cdp_rx_ppdu->num_users, CDP_MU_MAX_USERS);

	for (user_idx = 0; user_idx < max_users; user_idx++) {
		ppdu_user = &cdp_rx_ppdu->user[user_idx];

		if (ppdu_user->peer_id == CDP_INVALID_PEER)
			continue;

		STATS_CTX_LOCK_ACQUIRE(&soc_stats_ctx->rx_ctx_lock);

		stats_ctx = cdp_peer_get_peerstats_ctx(soc_stats_ctx->soc,
						       ppdu_user->vdev_id,
						       ppdu_user->mac_addr);

		if (qdf_unlikely(!stats_ctx)) {
			dp_warn("peer rate stats ctx is NULL, return");
			dp_warn("peer_mac:  " QDF_MAC_ADDR_FMT,
				QDF_MAC_ADDR_REF(ppdu_user->mac_addr));
			STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->rx_ctx_lock);
			continue;
		}

		rx_stats = &stats_ctx->rate_stats->rx;

		if (qdf_unlikely(!ppdu_user->rx_ratekbps || !ppdu_user->rix ||
				 ppdu_user->rix > DP_RATE_TABLE_SIZE)) {
			STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->rx_ctx_lock);
			continue;
		}

		if (qdf_likely(rx_stats->cur_rix == ppdu_user->rix &&
			       rx_stats->cur_rate == ppdu_user->rx_ratekbps)) {
			__rx_stats = &rx_stats->stats[rx_stats->cur_cache_idx];
			__wlan_peer_update_rx_rate_stats(__rx_stats,
							 cdp_rx_ppdu, user_idx);
			soc_stats_ctx->rxs_last_idx_cache_hit++;
			STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->rx_ctx_lock);
			continue;
		}

		/* check if cache is available */
		for (cache_idx = 0; cache_idx < WLANSTATS_CACHE_SIZE; cache_idx++) {
			__rx_stats = &rx_stats->stats[cache_idx];
			if ((__rx_stats->ratecode == INVALID_CACHE_IDX) ||
			    ((GET_DP_PEER_STATS_RIX(__rx_stats->ratecode) == ppdu_user->rix) &&
			     (__rx_stats->rate == ppdu_user->rx_ratekbps))) {
				idx_match = true;
				break;
			}
		}
		/* if index matches or found empty index, update stats to that
		 * cache index else flush cache and update stats to cache index
		 * zero
		 */
		if (idx_match) {
			__wlan_peer_update_rx_rate_stats(__rx_stats,
							 cdp_rx_ppdu, user_idx);
			rx_stats->cur_rix = ppdu_user->rix;
			rx_stats->cur_rate = ppdu_user->rx_ratekbps;
			rx_stats->cur_cache_idx = cache_idx;
			soc_stats_ctx->rxs_cache_hit++;
		} else {
			wlan_peer_flush_rx_rate_stats(soc_stats_ctx, stats_ctx);
			__rx_stats = &rx_stats->stats[0];
			__wlan_peer_update_rx_rate_stats(__rx_stats,
							 cdp_rx_ppdu, user_idx);
			rx_stats->cur_rix = ppdu_user->rix;
			rx_stats->cur_rate = ppdu_user->rx_ratekbps;
			rx_stats->cur_cache_idx = 0;
			soc_stats_ctx->rxs_cache_miss++;
		}
		STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->rx_ctx_lock);
	}
}

static inline void
__wlan_peer_update_tx_rate_stats(struct wlan_tx_rate_stats *__tx_stats,
				 struct cdp_tx_completion_ppdu_user *ppdu_user)
{
	uint32_t num_ppdus;
	uint32_t mpdu_attempts;
	uint32_t mpdu_success;

	num_ppdus = (uint32_t)ppdu_user->long_retries + 1;
	mpdu_attempts = num_ppdus * (uint32_t)ppdu_user->mpdu_tried_ucast;
	mpdu_success = (uint32_t)ppdu_user->mpdu_tried_ucast - (uint32_t)ppdu_user->mpdu_failed;
	if (ppdu_user->rix == -1) {
		__tx_stats->ratecode = ppdu_user->rix;
	} else {
		__tx_stats->ratecode = ASSEMBLE_STATS_CODE(ppdu_user->rix,
						      ppdu_user->nss,
						      ppdu_user->mcs,
						      ppdu_user->bw);
	}

	__tx_stats->rate = ppdu_user->tx_ratekbps;
	__tx_stats->num_ppdus += num_ppdus;
	__tx_stats->mpdu_attempts += mpdu_attempts;
	__tx_stats->mpdu_success += mpdu_success;
	__tx_stats->num_msdus += ppdu_user->success_msdus;
	__tx_stats->num_bytes += ppdu_user->success_bytes;
	__tx_stats->num_retries += ppdu_user->mpdu_failed;
}

static void
wlan_peer_update_tx_rate_stats(struct wlan_soc_rate_stats_ctx *soc_stats_ctx,
			       struct cdp_tx_completion_ppdu *cdp_tx_ppdu)
{
	struct cdp_tx_completion_ppdu_user *ppdu_user;
	struct wlan_peer_rate_stats_ctx *stats_ctx;
	struct wlan_peer_tx_rate_stats *tx_stats;
	struct wlan_tx_rate_stats *__tx_stats;
	uint8_t cache_idx;
	uint8_t user_idx;
	bool idx_match = false;

	if (soc_stats_ctx->stats_ver != PEER_EXT_RATE_STATS &&
	    soc_stats_ctx->stats_ver != PEER_EXT_ALL_STATS)
		return;

	for (user_idx = 0; user_idx < cdp_tx_ppdu->num_users; user_idx++) {
		ppdu_user = &cdp_tx_ppdu->user[user_idx];

		STATS_CTX_LOCK_ACQUIRE(&soc_stats_ctx->tx_ctx_lock);
		stats_ctx = cdp_peer_get_peerstats_ctx(soc_stats_ctx->soc,
						       cdp_tx_ppdu->vdev_id,
						       ppdu_user->mac_addr);

		if (qdf_unlikely(!ppdu_user->tx_ratekbps || !ppdu_user->rix ||
				 ppdu_user->rix > DP_RATE_TABLE_SIZE)) {
			STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->tx_ctx_lock);
			continue;
		}

		if (qdf_unlikely(!stats_ctx)) {
			dp_warn("peer rate stats ctx is NULL, investigate");
			dp_warn("peer_mac: " QDF_MAC_ADDR_FMT,
				QDF_MAC_ADDR_REF(ppdu_user->mac_addr));
			STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->tx_ctx_lock);
			continue;
		}

		tx_stats = &stats_ctx->rate_stats->tx;

		if (qdf_likely(tx_stats->cur_rix == ppdu_user->rix &&
			       tx_stats->cur_rate == ppdu_user->tx_ratekbps)) {
			__tx_stats = &tx_stats->stats[tx_stats->cur_cache_idx];
			__wlan_peer_update_tx_rate_stats(__tx_stats, ppdu_user);
			soc_stats_ctx->txs_last_idx_cache_hit++;
			STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->tx_ctx_lock);
			continue;
		}

		/* check if cache is available */
		for (cache_idx = 0; cache_idx < WLANSTATS_CACHE_SIZE;
					cache_idx++) {
			__tx_stats = &tx_stats->stats[cache_idx];

			if ((__tx_stats->ratecode == INVALID_CACHE_IDX) ||
			    ((GET_DP_PEER_STATS_RIX(__tx_stats->ratecode) == ppdu_user->rix) &&
			     (__tx_stats->rate == ppdu_user->tx_ratekbps))) {
				idx_match = true;
				break;
			}
		}
		/* if index matches or found empty index,
		 * update stats to that cache index
		 * else flush cache and update stats to cache index zero
		 */
		if (idx_match) {
			soc_stats_ctx->txs_cache_hit++;

			__wlan_peer_update_tx_rate_stats(__tx_stats,
							 ppdu_user);
			tx_stats->cur_rix = ppdu_user->rix;
			tx_stats->cur_rate = ppdu_user->tx_ratekbps;
			tx_stats->cur_cache_idx = cache_idx;
		} else {
			soc_stats_ctx->txs_cache_miss++;

			wlan_peer_flush_tx_rate_stats(soc_stats_ctx, stats_ctx);
			__tx_stats = &tx_stats->stats[0];
			__wlan_peer_update_tx_rate_stats(__tx_stats,
							 ppdu_user);
			tx_stats->cur_rix = ppdu_user->rix;
			tx_stats->cur_rate = ppdu_user->tx_ratekbps;
			tx_stats->cur_cache_idx = 0;
		}
		STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->tx_ctx_lock);
	}
}

static void
wlan_peer_update_sojourn_stats(struct wlan_soc_rate_stats_ctx  *soc_stats_ctx,
			       struct cdp_tx_sojourn_stats *sojourn_stats)
{
	struct wlan_peer_rate_stats_ctx *stats_ctx;
	struct wlan_peer_tx_rate_stats *tx_stats;
	uint8_t tid;

	if (soc_stats_ctx->stats_ver != PEER_EXT_RATE_STATS &&
	    soc_stats_ctx->stats_ver != PEER_EXT_ALL_STATS)
		return;

	stats_ctx = (struct wlan_peer_rate_stats_ctx *)sojourn_stats->cookie;

	if (qdf_unlikely(!stats_ctx)) {
		dp_warn("peer rate stats ctx is NULL, return");
		return;
	}

	STATS_CTX_LOCK_ACQUIRE(&soc_stats_ctx->tx_ctx_lock);

	tx_stats = &stats_ctx->rate_stats->tx;

	for (tid = 0; tid < CDP_DATA_TID_MAX; tid++) {
		tx_stats->sojourn.avg_sojourn_msdu[tid].internal =
			sojourn_stats->avg_sojourn_msdu[tid].internal;

		tx_stats->sojourn.sum_sojourn_msdu[tid] +=
			sojourn_stats->sum_sojourn_msdu[tid];

		tx_stats->sojourn.num_msdus[tid] +=
			sojourn_stats->num_msdus[tid];
	}
	STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->tx_ctx_lock);
}

void wlan_peer_update_rate_stats(void *ctx,
				 enum WDI_EVENT event,
				 void *buf, uint16_t peer_id,
				 uint32_t stats_type)
{
	struct wlan_soc_rate_stats_ctx *soc_stats_ctx;
	struct cdp_tx_completion_ppdu *cdp_tx_ppdu;
	struct cdp_rx_indication_ppdu *cdp_rx_ppdu;
	struct cdp_tx_sojourn_stats *sojourn_stats;
	qdf_nbuf_t nbuf;

	if (qdf_unlikely(!buf))
		return;

	if (qdf_unlikely(!ctx))
		return;

	soc_stats_ctx = ctx;
	nbuf = buf;

	switch (event) {
	case WDI_EVENT_TX_PPDU_DESC:
		cdp_tx_ppdu = (struct cdp_tx_completion_ppdu *)
					qdf_nbuf_data(nbuf);
		wlan_peer_update_tx_rate_stats(soc_stats_ctx, cdp_tx_ppdu);
		wlan_peer_update_tx_link_stats(soc_stats_ctx, cdp_tx_ppdu);
		wlan_peer_update_avg_tx_rate_stats(soc_stats_ctx, cdp_tx_ppdu);
		qdf_nbuf_free(nbuf);
		break;
	case WDI_EVENT_RX_PPDU_DESC:
		cdp_rx_ppdu = (struct cdp_rx_indication_ppdu *)
					qdf_nbuf_data(nbuf);
		wlan_peer_update_rx_rate_stats(soc_stats_ctx, cdp_rx_ppdu);
		wlan_peer_update_rx_link_stats(soc_stats_ctx, cdp_rx_ppdu);
		wlan_peer_update_avg_rx_rate_stats(soc_stats_ctx, cdp_rx_ppdu);
		qdf_nbuf_free(nbuf);
		break;
	case WDI_EVENT_TX_SOJOURN_STAT:
		/* sojourn stats buffer is statically allocated buffer
		 * at pdev level, do not free it
		 */
		sojourn_stats = (struct cdp_tx_sojourn_stats *)
					qdf_nbuf_data(nbuf);
		wlan_peer_update_sojourn_stats(soc_stats_ctx, sojourn_stats);
		break;
	default:
		dp_err("Err, Invalid type");
	}
}

void wlan_peer_create_event_handler(void *ctx, enum WDI_EVENT event,
				    void *buf, uint16_t peer_id,
				    uint32_t type)
{
	struct cdp_peer_cookie *peer_info;
	struct wlan_peer_rate_stats_ctx *stats;
	struct wlan_soc_rate_stats_ctx *soc_stats_ctx;
	uint8_t idx;

	soc_stats_ctx = ctx;

	peer_info = (struct cdp_peer_cookie *)buf;
	stats = qdf_mem_malloc(sizeof(*stats));
	if (!stats) {
		qdf_err("malloc failed, returning NULL");
		return;
	}

	if (soc_stats_ctx->stats_ver == PEER_EXT_RATE_STATS ||
	    soc_stats_ctx->stats_ver == PEER_EXT_ALL_STATS) {
		stats->rate_stats =
			qdf_mem_malloc(sizeof(struct wlan_peer_rate_stats));
		if (!stats->rate_stats) {
			qdf_err("malloc failed");
			goto peer_create_fail1;
		}
		for (idx = 0; idx < WLANSTATS_CACHE_SIZE; idx++) {
			stats->rate_stats->tx.stats[idx].ratecode =
							INVALID_CACHE_IDX;
			stats->rate_stats->rx.stats[idx].ratecode =
							INVALID_CACHE_IDX;
		}
	}

	if (soc_stats_ctx->stats_ver == PEER_EXT_LINK_STATS ||
	    soc_stats_ctx->stats_ver == PEER_EXT_ALL_STATS) {
		stats->link_metrics =
			qdf_mem_malloc(sizeof(struct wlan_peer_link_metrics));
		if (!stats->link_metrics) {
			qdf_err("malloc failed");
			goto peer_create_fail2;
		}
	}

	qdf_mem_copy(stats->mac_addr, peer_info->mac_addr, QDF_MAC_ADDR_SIZE);
	stats->peer_cookie = peer_info->cookie;
	stats->pdev_id = peer_info->pdev_id;

	peer_info->ctx = (void *)stats;
	return;

peer_create_fail2:
	if (soc_stats_ctx->stats_ver == PEER_EXT_RATE_STATS ||
	    soc_stats_ctx->stats_ver == PEER_EXT_ALL_STATS) {
		qdf_mem_free(stats->rate_stats);
		stats->rate_stats = NULL;
	}
peer_create_fail1:
	qdf_mem_free(stats);
}

void wlan_peer_destroy_event_handler(void *ctx, enum WDI_EVENT event,
				     void *buf, uint16_t peer_id,
				     uint32_t type)
{
	struct cdp_peer_cookie *peer_info;
	struct wlan_peer_rate_stats_ctx *stats;
	struct wlan_soc_rate_stats_ctx *soc_stats_ctx;

	soc_stats_ctx = ctx;

	peer_info = (struct cdp_peer_cookie *)buf;
	stats = (struct wlan_peer_rate_stats_ctx *)peer_info->ctx;

	STATS_CTX_LOCK_ACQUIRE(&soc_stats_ctx->tx_ctx_lock);
	STATS_CTX_LOCK_ACQUIRE(&soc_stats_ctx->rx_ctx_lock);
	if (stats) {
		wlan_peer_flush_rate_stats(ctx, stats);
		if (stats->rate_stats) {
			qdf_mem_free(stats->rate_stats);
		}
		if (stats->link_metrics) {
			qdf_mem_free(stats->link_metrics);
		}
		qdf_mem_free(stats);
		dp_info("Debug Deinitialized rate stats");
	}
	STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->rx_ctx_lock);
	STATS_CTX_LOCK_RELEASE(&soc_stats_ctx->tx_ctx_lock);
}
#endif
