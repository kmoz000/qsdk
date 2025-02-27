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

#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include "qdf_lock.h"
#include "qdf_debugfs.h"
#include "qdf_module.h"
#include "qdf_list.h"
#include "dp_htt_logger.h"
#include "dp_types.h"
#include "dp_internal.h"
#include <htt.h>

#define HTT_DBG_FILE_PERM           (QDF_FILE_USR_READ | QDF_FILE_USR_WRITE | \
					 QDF_FILE_GRP_READ | \
					 QDF_FILE_OTH_READ)
/**
 * HTT_DISPLAY_SIZE   : Supported Number of command/event/wbm_event to be
 * read at one shot through wdf debugfs framework
 */
#define HTT_DISPLAY_SIZE 25

/**
 * disable_all_command(): Disable all command
 * disable_all_event(): Disable all event
 * enable_event(): Enable specific event
 */
#define disable_all_command(htt_logger_handle) \
	htt_logger_handle->log_info.htt_cmd_disable_list = 0xFFFFFFFFFFFFFFFF
#define disable_all_event(htt_logger_handle)\
	htt_logger_handle->log_info.htt_event_disable_list = 0xFFFFFFFFFFFFFFFF
#define disable_wbm_success_status(htt_logger_handle) \
	htt_logger_handle->log_info.htt_wbm_event_disable_list = 0x1
#define enable_htt_logging(htt_logger_handle) \
	htt_logger_handle->log_info.htt_logging_enable = 1
#define enable_event(htt_logger_handle, eventid) \
do { \
	htt_disable_mask = ~(0x1ULL << eventid); \
	htt_logger_handle->log_info.htt_event_disable_list &= \
		htt_disable_mask; \
} while (0)

#define enable_command(htt_logger_handle, cmd_id) \
do { \
	htt_disable_mask = ~(0x1ULL << cmd_id); \
	htt_logger_handle->log_info.htt_cmd_disable_list &= \
		htt_disable_mask; \
} while (0)

/**
 * log_buf_init() - Initialize htt buffer
 */
static inline void log_buf_init(struct htt_log_buf_t *buf)
{
	buf->length = 0;
	buf->buf_tail_idx = 0;
	buf->size = HTT_EVENT_DEBUG_MAX_ENTRY;
}

/**
 * htt_command_record() - Record Command
 * @ h - htt logger handle
 * @ msg_type - Command Id
 * @ msg_data - Data to be logged
 */
int htt_command_record(struct htt_logger *h, uint8_t msg_type,
		       uint8_t *msg_data)
{
	struct htt_command_debug *buf = NULL;
	uint32_t *p_buf_tail_idx = NULL;
	uint8_t tdata[HTT_COMMAND_DEBUG_ENTRY_MAX_LENGTH];
	struct htt_log_buf_t *cmd_log_buf;

	/* return 0 if:
	 * 1. Initialization failed.
	 * 2. htt_logging_enable is 0 -> can get disable from file ops
	 * 3. This specific msg_type is disabled
	 */
	if (!h)
		return 0;

	cmd_log_buf = &h->log_info.htt_command_log_buf_info;
	if ((0 == h->log_info.htt_logging_enable) ||
	    ((1ULL << msg_type) & h->log_info.htt_cmd_disable_list))
		return 0;

	qdf_spin_lock_bh(&cmd_log_buf->record_lock);
	if (!msg_data) {
		/* if msg_data is NULL then fill 0xFF in data field */
		qdf_mem_set(tdata, HTT_COMMAND_DEBUG_ENTRY_MAX_LENGTH, 0xFF);
		msg_data = tdata;
	}

	p_buf_tail_idx = &h->log_info.htt_command_log_buf_info.buf_tail_idx;

	/* rewind pointer index if buffer became full */
	if (HTT_EVENT_DEBUG_MAX_ENTRY <= *p_buf_tail_idx)
		*p_buf_tail_idx = 0;

	buf = h->log_info.htt_command_log_buf_info.buf;
	buf[*p_buf_tail_idx].command = msg_type;
	qdf_mem_copy(buf[*p_buf_tail_idx].data, msg_data,
		     HTT_COMMAND_DEBUG_ENTRY_MAX_LENGTH);
	buf[*p_buf_tail_idx].time = qdf_get_log_timestamp();
	buf[*p_buf_tail_idx].cpu_id = smp_processor_id();

	(*p_buf_tail_idx)++;
	h->log_info.htt_command_log_buf_info.length++;
	qdf_spin_unlock_bh(&cmd_log_buf->record_lock);
	return 0;
}
qdf_export_symbol(htt_command_record);

/**
 * htt_event_record() - Record Event
 * @ h - htt logger handle
 * @ msg_type - Event Id
 * @ msg_data - Data to be logged
 */
int htt_event_record(struct htt_logger *h, uint8_t msg_type, uint8_t *msg_data)
{
	struct htt_event_debug *buf = NULL;
	uint32_t *p_buf_tail_idx = NULL;
	uint8_t tdata[HTT_EVENT_DEBUG_ENTRY_MAX_LENGTH];
	struct htt_log_buf_t *event_log_buf;

	/* return 0 if:
	 * 1. Initialization failed.
	 * 2. htt_logging_enable is 0 -> can get disable from file ops
	 * 3. This specific msg_type is disabled
	 */
	if (!h)
		return 0;

	event_log_buf = &h->log_info.htt_event_log_buf_info;
	if ((0 == h->log_info.htt_logging_enable) ||
	    ((1ULL << msg_type) & h->log_info.htt_event_disable_list))
		return 0;

	qdf_spin_lock(&event_log_buf->record_lock);
	if (!msg_data) {
		/* if msg_data is NULL then fill 0xFF in data field */
		qdf_mem_set(tdata, HTT_EVENT_DEBUG_ENTRY_MAX_LENGTH, 0xFF);
		msg_data = tdata;
	}

	p_buf_tail_idx = &h->log_info.htt_event_log_buf_info.buf_tail_idx;

	/* rewind pointer index if buffer became full */
	if (HTT_EVENT_DEBUG_MAX_ENTRY <= *p_buf_tail_idx)
		*p_buf_tail_idx = 0;

	buf = h->log_info.htt_event_log_buf_info.buf;
	buf[*p_buf_tail_idx].event = msg_type;
	qdf_mem_copy(buf[*p_buf_tail_idx].data, msg_data,
		     HTT_EVENT_DEBUG_ENTRY_MAX_LENGTH);
	buf[*p_buf_tail_idx].time = qdf_get_log_timestamp();
	buf[*p_buf_tail_idx].cpu_id = smp_processor_id();

	(*p_buf_tail_idx)++;
	h->log_info.htt_event_log_buf_info.length++;
	qdf_spin_unlock(&event_log_buf->record_lock);
	return 0;
}

/**
 * htt_wbm_event_recored() - Record HTT wbm event
 *
 * @ h - Pointer for HTT tracer
 * @ tx_status - Status flag
 * @ msg_data - Event data
 */
int htt_wbm_event_record(struct htt_logger *h, uint8_t tx_status,
			 uint8_t *msg_data)
{
	uint32_t *p_buf_tail_idx = NULL;
	struct htt_wbm_event_debug *buf = NULL;
	uint8_t tdata[HTT_WBM_EVENT_DEBUG_ENTRY_MAX_LENGTH];
	struct htt_log_buf_t *wbm_event_log_buf;
	uint8_t log_length = qdf_min(HTT_WBM_EVENT_DEBUG_ENTRY_MAX_LENGTH,
				     HAL_TX_COMP_HTT_STATUS_LEN);

	/* return 0 if:
	 * 1. Initialization failed.
	 * 2. htt_logging_enable is 0 -> can get disable from file ops
	 * 3. This specific tx_status is disabled can be used to disable
	 *    tx_Status when tx_status is successful.
	 */
	if (!h)
		return 0;

	wbm_event_log_buf = &h->log_info.htt_wbm_event_log_buf_info;
	if ((0 == h->log_info.htt_logging_enable) ||
	    ((1 << tx_status) & h->log_info.htt_wbm_event_disable_list))
		return 0;

	qdf_spin_lock(&wbm_event_log_buf->record_lock);
	if (!msg_data) {
		/* if msg_data is NULL then fill 0xFF in data field */
		qdf_mem_set(tdata, HTT_WBM_EVENT_DEBUG_ENTRY_MAX_LENGTH, 0xFF);
		msg_data = tdata;
	}

	p_buf_tail_idx = &h->log_info.htt_wbm_event_log_buf_info.buf_tail_idx;
	if (HTT_EVENT_DEBUG_MAX_ENTRY <= *p_buf_tail_idx)
		*p_buf_tail_idx = 0;

	buf = h->log_info.htt_wbm_event_log_buf_info.buf;
	buf[*p_buf_tail_idx].tx_status = tx_status;
	qdf_mem_copy(buf[*p_buf_tail_idx].data, msg_data, log_length);
	buf[*p_buf_tail_idx].time = qdf_get_log_timestamp();
	buf[*p_buf_tail_idx].cpu_id = smp_processor_id();

	(*p_buf_tail_idx)++;
	h->log_info.htt_wbm_event_log_buf_info.length++;
	qdf_spin_unlock(&wbm_event_log_buf->record_lock);
	return 0;
}

/* debugfs routines */
/**
 * debug_htt_command_log_show() - debugfs functions to display content of
 * command and event buffers. Macro uses max buffer length to display
 * buffer when it is wraparound.
 *
 * @m: qdf_debugfs handler to access htt_logger_handle
 * @arg: Private data htt_logger_hand here
 *
 * Return: Status
 */

static QDF_STATUS debug_htt_command_log_show(qdf_debugfs_file_t file, void *arg)
{
	struct htt_logger *htt_logger_handle = (struct htt_logger *)arg;
	struct htt_log_buf_t *htt_log = NULL;
	int pos, nread, i;
	uint64_t secs, usecs;

	htt_log = &htt_logger_handle->log_info.htt_command_log_buf_info;

	qdf_spin_lock(&htt_logger_handle->log_info.htt_record_lock);
	if (!htt_log->length) {
		qdf_spin_unlock(&htt_logger_handle->log_info.htt_record_lock);
		qdf_debugfs_printf(file,
				   "no elements to read from ring buffer!\n");
		return QDF_STATUS_SUCCESS;
	}

	if (htt_log->length <= HTT_DISPLAY_SIZE)
		nread = htt_log->length;
	else
		nread = HTT_DISPLAY_SIZE;

	if (htt_log->buf_tail_idx == 0)
		pos = HTT_DISPLAY_SIZE - 1;
	else
		pos = htt_log->buf_tail_idx - 1;

	qdf_debugfs_printf(file, "Length = %d\n", htt_log->length);
	qdf_spin_unlock(&htt_logger_handle->log_info.htt_record_lock);
	while (nread--) {
		struct htt_command_debug *htt_record;

		htt_record = (struct htt_command_debug *)
		&(((struct htt_command_debug *)htt_log->buf)[pos]);
		qdf_debugfs_printf(file, "CMD ID = %x\n",
				   htt_record->command);
		qdf_debugfs_printf(file, "DATA[0-%d]:",
				   HTT_COMMAND_DEBUG_ENTRY_MAX_LENGTH - 1);
		for (i = 0; i < HTT_COMMAND_DEBUG_ENTRY_MAX_LENGTH; i++)
			qdf_debugfs_printf(file, "%x ", htt_record->data[i]);

		qdf_log_timestamp_to_secs(htt_record->time, &secs, &usecs);
		qdf_debugfs_printf(file, "\nCPU ID = %d", htt_record->cpu_id);
		qdf_debugfs_printf(file, "\nCMD TIME = [%llu.%06llu]\n",
				   secs, usecs);

		if (pos == 0)
			pos = HTT_DISPLAY_SIZE - 1;
		else
			pos--;
	}
	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS debug_htt_event_log_show(qdf_debugfs_file_t file, void *arg)
{
	struct htt_logger *htt_logger_handle = (struct htt_logger *)arg;
	struct htt_log_buf_t *htt_log = NULL;
	int pos, nread, i;
	uint64_t secs, usecs;

	htt_log = &htt_logger_handle->log_info.htt_event_log_buf_info;

	qdf_spin_lock(&htt_logger_handle->log_info.htt_record_lock);
	if (!htt_log->length) {
		qdf_spin_unlock(&htt_logger_handle->log_info.htt_record_lock);
		qdf_debugfs_printf(file,
				   "no elements to read from ring buffer!\n");
		return QDF_STATUS_SUCCESS;
	}

	if (htt_log->length <= HTT_DISPLAY_SIZE)
		nread = htt_log->length;
	else
		nread = HTT_DISPLAY_SIZE;

	if (htt_log->buf_tail_idx == 0)
		pos = HTT_DISPLAY_SIZE - 1;
	else
		pos = htt_log->buf_tail_idx - 1;

	qdf_debugfs_printf(file, "Length = %d\n", htt_log->length);
	qdf_spin_unlock(&htt_logger_handle->log_info.htt_record_lock);
	while (nread--) {
		struct htt_event_debug *htt_record;

		htt_record = (struct htt_event_debug *)
		&(((struct htt_event_debug *)htt_log->buf)[pos]);
		qdf_debugfs_printf(file, "EVENT ID = %x\n", htt_record->event);
		qdf_debugfs_printf(file, "DATA[0 - %d]:",
				   HTT_EVENT_DEBUG_ENTRY_MAX_LENGTH - 1);
		for (i = 0; i < HTT_EVENT_DEBUG_ENTRY_MAX_LENGTH; i++)
			qdf_debugfs_printf(file, "%x ", htt_record->data[i]);

		qdf_log_timestamp_to_secs(htt_record->time, &secs, &usecs);
		qdf_debugfs_printf(file, "\nCPU ID = %d", (htt_record->cpu_id));
		qdf_debugfs_printf(file, "\nEVENT TIME = [%llu.%06llu]\n",
				   secs, usecs);

		if (pos == 0)
			pos = HTT_DISPLAY_SIZE - 1;
		else
			pos--;
	}
	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS debug_htt_wbm_event_log_show(qdf_debugfs_file_t file,
					       void *arg)
{
	struct htt_logger *htt_logger_handle = (struct htt_logger *)arg;
	struct htt_log_buf_t *htt_log = NULL;
	int pos, nread, i;
	uint64_t secs, usecs;

	htt_log = &htt_logger_handle->log_info.htt_wbm_event_log_buf_info;

	qdf_spin_lock(&htt_logger_handle->log_info.htt_record_lock);
	if (!htt_log->length) {
		qdf_spin_unlock(&htt_logger_handle->log_info.htt_record_lock);
		qdf_debugfs_printf(file,
				   "no elements to read from ring buffer!\n");
		return QDF_STATUS_SUCCESS;
	}

	if (htt_log->length <= HTT_DISPLAY_SIZE)
		nread = htt_log->length;
	else
		nread = HTT_DISPLAY_SIZE;

	if (htt_log->buf_tail_idx == 0)
		pos = HTT_DISPLAY_SIZE - 1;
	else
		pos = htt_log->buf_tail_idx - 1;

	qdf_debugfs_printf(file, "Length = %d\n", htt_log->length);
	qdf_spin_unlock(&htt_logger_handle->log_info.htt_record_lock);
	while (nread--) {
		struct htt_wbm_event_debug *htt_record;

		htt_record = (struct htt_wbm_event_debug *)
		&(((struct htt_wbm_event_debug *)htt_log->buf)[pos]);
		qdf_debugfs_printf(file, "WBM Event Tx status =  = %x\n",
				   htt_record->tx_status);
		qdf_debugfs_printf(file, "DATA[0-%d]:",
				   HTT_WBM_EVENT_DEBUG_ENTRY_MAX_LENGTH - 1);
		for (i = 0; i < HTT_WBM_EVENT_DEBUG_ENTRY_MAX_LENGTH; i++)
			qdf_debugfs_printf(file, "%x ", htt_record->data[i]);

		qdf_log_timestamp_to_secs(htt_record->time, &secs, &usecs);
		qdf_debugfs_printf(file, "\nCPU ID = %d", (htt_record->cpu_id));
		qdf_debugfs_printf(file, "\nWBM EVENT TIME = [%llu.%06llu]\n",
				   secs, usecs);

		if (pos == 0)
			pos = HTT_DISPLAY_SIZE - 1;
		else
			pos--;
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * debug_htt_enable_show() - debugfs functions to display enable state of
 * htt logging feature.
 *
 * @file: qdf debugfs handler
 * @arg: priv data used to get htt_logger_handler
 *
 * Return: Status
 */
static QDF_STATUS debug_htt_enable_show(qdf_debugfs_file_t file, void *arg)
{
	struct htt_logger *htt_logger_handle = (struct htt_logger *)arg;
	struct htt_debug_log_info *log_info_p = &htt_logger_handle->log_info;

	qdf_debugfs_printf(file, "%d\n",
			   log_info_p->htt_logging_enable);
	return QDF_STATUS_SUCCESS;
}

/**
 * debug_htt_cmd_disable_list_show() - debugfs functions to display disabled
 * command id for tracing.
 *
 * @file: qdf debugfs handler
 * @arg: priv data used to get htt_logger_handler
 *
 * Return: Status
 */
static QDF_STATUS debug_htt_cmd_disable_list_show(qdf_debugfs_file_t file,
						  void *arg)
{
	struct htt_logger *htt_logger_handle = (struct htt_logger *)arg;
	struct htt_debug_log_info *log_info_p = &htt_logger_handle->log_info;

	qdf_debugfs_printf(file, "%llu\n",
			   log_info_p->htt_cmd_disable_list);

	return QDF_STATUS_SUCCESS;
}

/**
 * debug_htt_event_disable_list_show() - debugfs functions to display disabled
 * event id for tracing.
 *
 * @file: qdf debugfs handler
 * @arg: priv data used to get htt_logger_handler
 *
 * Return: Status
 */
static QDF_STATUS debug_htt_event_disable_list_show(qdf_debugfs_file_t file,
						    void *arg)
{
	struct htt_logger *htt_logger_handle = (struct htt_logger *)arg;
	struct htt_debug_log_info *log_info_p = &htt_logger_handle->log_info;

	qdf_debugfs_printf(file, "%llu\n",
			   log_info_p->htt_event_disable_list);

	return QDF_STATUS_SUCCESS;
}

/**
 * debug_htt_cmd_disable_list_show() - debugfs functions to display disabled
 * wbm event id for tracing.
 *
 * @file: qdf debugfs handler
 * @arg: priv data used to get htt_logger_handler
 *
 * Return: Status
 */
static QDF_STATUS debug_htt_wbm_event_disable_list_show(qdf_debugfs_file_t file
							, void *arg)
{
	struct htt_logger *htt_logger_handle = (struct htt_logger *)arg;
	struct htt_debug_log_info *log_info_p = &htt_logger_handle->log_info;

	qdf_debugfs_printf(file, "%llu\n",
			   log_info_p->htt_wbm_event_disable_list);

	return QDF_STATUS_SUCCESS;
}

/**
 * debug_htt_log_size_show() - debugfs functions to display configured size of
 * htt logging command/event buffer and management command/event buffer.
 *
 * @file: qdf debugfs handler
 * @arg: priv data used to get htt_logger_handler
 *
 * Return: Status
 */
static QDF_STATUS debug_htt_log_size_show(qdf_debugfs_file_t file, void *arg)
{
	qdf_debugfs_printf(file, "HTT command/event log max size:%d\n",
			   HTT_EVENT_DEBUG_MAX_ENTRY);

	return QDF_STATUS_SUCCESS;
}

/**
 * debug_htt_##func_base##_write() - debugfs functions to clear
 * htt logging command/event buffer and management command/event buffer.
 *
 * @priv: file handler to access htt_logger_handle
 * @buf: received data buffer
 * @len: length of received buffer
 *
 * Return: Status
 */
#define GENERATE_DEBUG_WRITE_FUNCS(func_base, htt_ring_size, htt_record_type) \
	static QDF_STATUS debug_htt_##func_base##_write(void *priv,           \
			const char *buf, qdf_size_t len)                      \
{                                                                             \
	int k, ret;                                                           \
	struct htt_logger *htt_logger_handle = (struct htt_logger *)priv;    \
	struct htt_log_buf_t *htt_log = &htt_logger_handle->log_info.         \
					htt_##func_base##_buf_info;           \
	ret = kstrtoint(buf, 0, &k);                                          \
	if ((ret != 0) || (k != 0)) {                                         \
		return QDF_STATUS_E_PERM;                                     \
	}                                                                     \
	qdf_spin_lock(&htt_logger_handle->log_info.htt_record_lock);          \
	qdf_mem_zero(htt_log->buf, (htt_ring_size) *                          \
			sizeof(struct htt_record_type));                      \
	htt_log->length = 0;                                                  \
	htt_log->buf_tail_idx = 0;                                       \
	qdf_spin_unlock(&htt_logger_handle->log_info.htt_record_lock);        \
	return QDF_STATUS_SUCCESS ;                                           \
}

GENERATE_DEBUG_WRITE_FUNCS(command_log, HTT_EVENT_DEBUG_MAX_ENTRY,
			   htt_command_debug);
GENERATE_DEBUG_WRITE_FUNCS(event_log, HTT_EVENT_DEBUG_MAX_ENTRY,
			   htt_event_debug);
GENERATE_DEBUG_WRITE_FUNCS(wbm_event_log, HTT_EVENT_DEBUG_MAX_ENTRY,
			   htt_wbm_event_debug);

/**
 * debug_htt_enable_write() - debugfs functions to enable/disable
 * htt logging feature.
 *
 * @priv: file handler to access htt_logger_handle
 * @buf: received data buffer
 * @len: length of received buffer
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS debug_htt_enable_write(void *priv, const char *buf,
					 qdf_size_t len)
{
	struct htt_logger *htt_logger_handle = (struct htt_logger *)priv;
	int k, ret;

	ret = kstrtoint(buf, 0, &k);
	if ((ret != 1) || ((k != 0) && (k != 1)))
		return QDF_STATUS_E_PERM;

	htt_logger_handle->log_info.htt_logging_enable = k;
	return QDF_STATUS_SUCCESS;
}

/**
 * debug_htt_cmd_disable_list_write() - Debugfs write for command
 * to be disabled for tracing
 * @priv: file handler to access htt_logger_handle
 * @buf: received data buffer
 * @len: length of received buffer
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS debug_htt_cmd_disable_list_write(void *priv,
						   const char *buf,
						   qdf_size_t len)
{
	struct htt_logger *htt_logger_handle = (struct htt_logger *)priv;
	int ret;
	uint64_t k;

	ret = kstrtou64(buf, 0, &k);
	if ((ret != 0) || ((k < 0) || (k > 0xFFFFFFFF)))
		return QDF_STATUS_E_PERM;
	htt_logger_handle->log_info.htt_cmd_disable_list = k;

	return QDF_STATUS_SUCCESS;
}

/**
 * debug_htt_event_disable_list_write() - Debugfs write for event
 * to be disabled for tracing
 * @priv: file handler to access htt_logger_handle
 * @buf: received data buffer
 * @len: length of received buffer
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS debug_htt_event_disable_list_write(void *priv,
						     const char *buf,
						     qdf_size_t len)
{
	struct htt_logger *htt_logger_handle = (struct htt_logger *)priv;
	int ret;
	uint64_t k;

	ret = kstrtou64(buf, 0, &k);
	if ((ret != 0) || ((k < 0) || (k > 0xFFFFFFFF)))
		return QDF_STATUS_E_PERM;

	htt_logger_handle->log_info.htt_event_disable_list = k;
	return QDF_STATUS_SUCCESS;
}

/**
 * debug_htt_wbm_event_disable_list_write() - Debugfs write for wbm event
 * to be disabled for tracing
 * @priv: file handler to access htt_logger_handle
 * @buf: received data buffer
 * @len: length of received buffer
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS debug_htt_wbm_event_disable_list_write(void *priv,
							 const char *buf,
							 qdf_size_t len)
{
	struct htt_logger *htt_logger_handle = (struct htt_logger *)priv;
	int ret;
	uint64_t k;

	ret = kstrtou64(buf, 0, &k);
	if ((ret != 0) || ((k < 0) || (k > 0xFFFFFFFF)))
		return QDF_STATUS_E_PERM;

	htt_logger_handle->log_info.htt_wbm_event_disable_list = k;
	return QDF_STATUS_SUCCESS;
}

/**
 * debug_htt_log_size_write() - reserved.
 *
 * @priv: file handler to access htt_logger_handle
 * @buf: received data buffer
 * @len: length of received buffer
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS debug_htt_log_size_write(void *priv,
					   const char *buf,
					   qdf_size_t len)
{
	return -EINVAL;
}

/* Structure to maintain debug information */
struct htt_debugfs_info {
	const char *name;
	struct qdf_debugfs_fops *ops;
};

#define DEBUGFS_FOPS(func_base) { .name = #func_base,               \
	.ops = &debug_##func_base##_ops }

#define GENERATE_DEBUG_STRUCTS(func_base)                        \
\
static struct qdf_debugfs_fops debug_##func_base##_ops = {       \
	.show           = debug_##func_base##_show,              \
	.write          = debug_##func_base##_write,             \
	.priv           = NULL,                                  \
}

GENERATE_DEBUG_STRUCTS(htt_command_log);
GENERATE_DEBUG_STRUCTS(htt_event_log);
GENERATE_DEBUG_STRUCTS(htt_wbm_event_log);
GENERATE_DEBUG_STRUCTS(htt_enable);
GENERATE_DEBUG_STRUCTS(htt_log_size);
GENERATE_DEBUG_STRUCTS(htt_cmd_disable_list);
GENERATE_DEBUG_STRUCTS(htt_event_disable_list);
GENERATE_DEBUG_STRUCTS(htt_wbm_event_disable_list);

struct htt_debugfs_info htt_debugfs_infos[NUM_HTT_DEBUG_INFOS] = {
	DEBUGFS_FOPS(htt_command_log),
	DEBUGFS_FOPS(htt_event_log),
	DEBUGFS_FOPS(htt_wbm_event_log),
	DEBUGFS_FOPS(htt_enable),
	DEBUGFS_FOPS(htt_log_size),
	DEBUGFS_FOPS(htt_cmd_disable_list),
	DEBUGFS_FOPS(htt_event_disable_list),
	DEBUGFS_FOPS(htt_wbm_event_disable_list),
};

/**
 * htt_log_buffer_free() - Free all dynamic allocated buffer memory for
 * event logging
 * @htt_logger_handle: HTT handle.
 *
 * Return: None
 */
static inline void htt_log_buffer_free(struct htt_logger *htt_logger_handle)
{
	struct htt_debug_log_info *log_info_p = &htt_logger_handle->log_info;

	/* first disable logging and then free buffer and release lock */
	log_info_p->htt_logging_enable = 0;

	/* Free allocated buffer */
	if (log_info_p->htt_command_log_buf_info.buf) {
		qdf_mem_free(log_info_p->htt_command_log_buf_info.buf);
		log_info_p->htt_command_log_buf_info.buf = NULL;
	}
	if (log_info_p->htt_event_log_buf_info.buf) {
		qdf_mem_free(log_info_p->htt_event_log_buf_info.buf);
		log_info_p->htt_event_log_buf_info.buf = NULL;
	}
	if (log_info_p->htt_wbm_event_log_buf_info.buf) {
		qdf_mem_free(log_info_p->htt_wbm_event_log_buf_info.buf);
		log_info_p->htt_wbm_event_log_buf_info.buf = NULL;
	}
}

/**
 * htt_log_buffer_alloc() - Allocate all dynamic allocated buffer memory for
 * event logging
 * @htt_logger_handle: HTT handle.
 *
 * Return: None
 */
static QDF_STATUS htt_log_buffer_alloc(struct htt_logger *htt_logger_handle)
{
	struct htt_log_buf_t *cmd_log_buf;
	struct htt_log_buf_t *event_log_buf;
	struct htt_log_buf_t *wbm_event_log_buf;

	cmd_log_buf =
		&htt_logger_handle->log_info.htt_command_log_buf_info;
	event_log_buf =
		&htt_logger_handle->log_info.htt_event_log_buf_info;
	wbm_event_log_buf =
		&htt_logger_handle->log_info.htt_wbm_event_log_buf_info;

	/* first disable logging and then free buffer and release lock */
	htt_logger_handle->log_info.htt_logging_enable = 0;

	/* Allocate data buffer */
	cmd_log_buf->buf = (struct htt_command_debug *)qdf_mem_malloc(
				HTT_EVENT_DEBUG_MAX_ENTRY *
				sizeof(struct htt_command_debug));
	if (!cmd_log_buf->buf) {
		HTT_LOGE("Memory allocation for HTT CMD log failed.");
		goto error_mem_fail;
	}
	event_log_buf->buf = (struct htt_event_debug *)qdf_mem_malloc(
				HTT_EVENT_DEBUG_MAX_ENTRY *
				sizeof(struct htt_event_debug));
	if (!event_log_buf->buf) {
		HTT_LOGE("Memory allocation for HTT EVENT log failed.");
		goto error_mem_fail;
	}
	wbm_event_log_buf->buf = (struct htt_wbm_event_debug *)qdf_mem_malloc(
					HTT_EVENT_DEBUG_MAX_ENTRY *
					sizeof(struct htt_wbm_event_debug));
	if (!wbm_event_log_buf->buf) {
		HTT_LOGE("Memory allocation for HTT WBM EVENT log failed.");
		goto error_mem_fail;
	}
	return QDF_STATUS_SUCCESS;

error_mem_fail:
	htt_log_buffer_free(htt_logger_handle);
	return QDF_STATUS_E_NOMEM;
}

/**
 * htt_log_lock_free - Destroy lock
 */
static inline void htt_log_lock_alloc(struct htt_logger *htt_logger_handle)
{
	struct htt_debug_log_info *log_info_p = &htt_logger_handle->log_info;
	struct htt_log_buf_t *cmd_log_buf =
		&htt_logger_handle->log_info.htt_command_log_buf_info;
	struct htt_log_buf_t *event_log_buf =
		&htt_logger_handle->log_info.htt_event_log_buf_info;
	struct htt_log_buf_t *wbm_event_log_buf =
		&htt_logger_handle->log_info.htt_wbm_event_log_buf_info;

	/* Create lock for cmd/event/wbm */
	qdf_spinlock_create(&cmd_log_buf->record_lock);
	qdf_spinlock_create(&event_log_buf->record_lock);
	qdf_spinlock_create(&wbm_event_log_buf->record_lock);

	/* Create recording lock in read through debugfs */
	qdf_spinlock_create(&log_info_p->htt_record_lock);
}

/**
 * htt_log_lock_free - Destroy lock
 */
static inline void htt_log_lock_free(struct htt_logger *htt_logger_handle)
{
	struct htt_debug_log_info *log_info_p = &htt_logger_handle->log_info;
	struct htt_log_buf_t *cmd_log_buf =
		&htt_logger_handle->log_info.htt_command_log_buf_info;
	struct htt_log_buf_t *event_log_buf =
		&htt_logger_handle->log_info.htt_event_log_buf_info;
	struct htt_log_buf_t *wbm_event_log_buf =
		&htt_logger_handle->log_info.htt_wbm_event_log_buf_info;

	/* Destroy lock for cmd/event/wbm */
	qdf_spinlock_destroy(&cmd_log_buf->record_lock);
	qdf_spinlock_destroy(&event_log_buf->record_lock);
	qdf_spinlock_destroy(&wbm_event_log_buf->record_lock);

	/* Destroy recording lock in read through debugfs */
	qdf_spinlock_destroy(&log_info_p->htt_record_lock);
}

/**
 * htt_debugfs_remove() - Remove debugfs entry for htt logging.
 * @htt_logger_handle: htt handle
 * @dentry: qdf_debugfs directory entry
 * @id: Index to debug info data array
 *
 * Return: none
 */
static void htt_debugfs_remove(struct htt_logger *htt_logger_handle)
{
	int i;
	qdf_dentry_t dentry = htt_logger_handle->log_info.htt_log_debugfs_dir;

	if (dentry) {
		for (i = 0; i < NUM_HTT_DEBUG_INFOS; ++i) {
			if (htt_logger_handle->debugfs_de[i])
				htt_logger_handle->debugfs_de[i] = NULL;
		}
	}

	if (dentry)
		qdf_debugfs_remove_dir_recursive(dentry);
}

/**
 * htt_debugfs_init() - debugfs functions to create debugfs directory and to
 * create debugfs enteries.
 *
 * @ htt_logger_handle: htt logger handler
 *
 * Return: init status
 */

static QDF_STATUS htt_debugfs_init(struct htt_logger *htt_logger_handle,
				   uint8_t psoc_id)
{
	char buf[32];
	int i;

	snprintf(buf, sizeof(buf), "HTT_SOC%d_LOGGER", psoc_id);

	htt_logger_handle->log_info.htt_log_debugfs_dir =
		qdf_debugfs_create_dir(buf, NULL);

	if (!htt_logger_handle->log_info.htt_log_debugfs_dir) {
		HTT_LOGE("error while creating debugfs dir for %s", buf);
		return QDF_STATUS_E_FAILURE;
	}

	for (i = 0; i < NUM_HTT_DEBUG_INFOS; ++i) {
		htt_debugfs_infos[i].ops->priv = htt_logger_handle;
		htt_logger_handle->debugfs_de[i] =
				qdf_debugfs_create_file_simplified(
				htt_debugfs_infos[i].name, HTT_DBG_FILE_PERM,
				htt_logger_handle->log_info.htt_log_debugfs_dir,
				htt_debugfs_infos[i].ops);

		if (!htt_logger_handle->debugfs_de[i])
			goto out;
	}

	return QDF_STATUS_SUCCESS;
out:
	HTT_LOGE("debug Entry creation failed[%s]!", htt_debugfs_infos[i].name);
	htt_debugfs_remove(htt_logger_handle);
	return QDF_STATUS_E_FAILURE;
}

/**
 * htt_interface_logging_init() - Initialize HTT tracer
 * @ htt_logger_handle: htt logger handler
 */
void htt_interface_logging_init(struct htt_logger **phtt_logger_handle,
				struct cdp_ctrl_objmgr_psoc *ctrl_psoc)
{
	QDF_STATUS ret;
	struct htt_logger *htt_logger_handle;
	struct htt_log_buf_t *cmd_log_buf;
	struct htt_log_buf_t *event_log_buf;
	struct htt_log_buf_t *wbm_event_log_buf;
	uint64_t htt_disable_mask;
	struct wlan_objmgr_psoc *psoc;

	*phtt_logger_handle = (struct htt_logger *)qdf_mem_malloc(
				sizeof(struct htt_logger));
	if (!*phtt_logger_handle) {
		HTT_LOGE("Memory allocation for HTT logger buffer failed.");
		return;
	}
	/* Allocate Buffer */
	if (QDF_STATUS_SUCCESS != htt_log_buffer_alloc(*phtt_logger_handle)) {
		qdf_mem_free(*phtt_logger_handle);
		*phtt_logger_handle = NULL;
		return;
	}
	htt_logger_handle = *phtt_logger_handle;
	cmd_log_buf =
		&htt_logger_handle->log_info.htt_command_log_buf_info;
	event_log_buf =
		&htt_logger_handle->log_info.htt_event_log_buf_info;
	wbm_event_log_buf =
		&htt_logger_handle->log_info.htt_wbm_event_log_buf_info;

	/* Initialize HTT Cmd/Event/WBM Event */
	log_buf_init(cmd_log_buf);
	log_buf_init(event_log_buf);
	log_buf_init(wbm_event_log_buf);

	/*
	 * Disable logging all commands and enable for specific
	 * commands
	 */
	disable_all_command(htt_logger_handle);
	enable_command(htt_logger_handle, HTT_H2T_MSG_TYPE_RX_RING_CFG);
	enable_command(htt_logger_handle, HTT_H2T_MSG_TYPE_SRING_SETUP);
	enable_command(htt_logger_handle,
		       HTT_H2T_MSG_TYPE_RX_RING_SELECTION_CFG);
	enable_command(htt_logger_handle,
		       HTT_H2T_MSG_TYPE_RX_FULL_MONITOR_MODE);
	enable_command(htt_logger_handle,
		       HTT_H2T_MSG_TYPE_PRIMARY_LINK_PEER_MIGRATE_RESP);

	/*
	 *  Disable logging for all events and enable for specific
	 *  events
	 */
	disable_all_event(htt_logger_handle);
	enable_event(htt_logger_handle, HTT_T2H_MSG_TYPE_PEER_MAP);
	enable_event(htt_logger_handle, HTT_T2H_MSG_TYPE_PEER_UNMAP);
	enable_event(htt_logger_handle, HTT_T2H_MSG_TYPE_RX_ADDBA);
	enable_event(htt_logger_handle, HTT_T2H_MSG_TYPE_RX_DELBA);
	enable_event(htt_logger_handle, HTT_T2H_MSG_TYPE_PEER_MAP_V2);
	enable_event(htt_logger_handle, HTT_T2H_MSG_TYPE_PEER_UNMAP_V2);
	enable_event(htt_logger_handle, HTT_T2H_MSG_TYPE_PEER_MAP_V3);
	enable_event(htt_logger_handle, HTT_T2H_MSG_TYPE_MLO_RX_PEER_MAP);
	enable_event(htt_logger_handle, HTT_T2H_MSG_TYPE_MLO_RX_PEER_UNMAP);
	enable_event(htt_logger_handle,
		     HTT_T2H_MSG_TYPE_PRIMARY_LINK_PEER_MIGRATE_IND);

	/* Disable success status*/
	disable_wbm_success_status(htt_logger_handle);
	/* Create lock for all event */
	htt_log_lock_alloc(htt_logger_handle);

	/* Enable HTT logging */
	enable_htt_logging(htt_logger_handle);

	psoc = (struct wlan_objmgr_psoc *)(ctrl_psoc);
	ret = htt_debugfs_init(*phtt_logger_handle, wlan_psoc_get_id(psoc));
	if (QDF_STATUS_SUCCESS != ret)
		goto debugfs_init_failed;
	return;

debugfs_init_failed:
	htt_log_buffer_free(*phtt_logger_handle);
	htt_log_lock_free(*phtt_logger_handle);
	(*phtt_logger_handle)->log_info.htt_logging_enable = 0;
	qdf_mem_free(*phtt_logger_handle);
	*phtt_logger_handle = NULL;
}

/**
 * htt_interface_logging_deinit() - Deinitialize HTT tracer
 * @ htt_logger_handle: htt logger handler
 */
void htt_interface_logging_deinit(struct htt_logger *htt_logger_handle)
{
	/**
	 * This case will hit in deinit path
	 *   if htt_initialization failed
	 */
	if (!htt_logger_handle)
		return;
	htt_debugfs_remove(htt_logger_handle);
	htt_log_buffer_free(htt_logger_handle);
	htt_log_lock_free(htt_logger_handle);
	qdf_mem_free(htt_logger_handle);
}

#ifdef HTT_STATS_DEBUGFS_SUPPORT
void htt_stats_msg_receive(void *data, A_INT32 len);

/* File permission for HTT stats debugfs entry */
#define PDEV_HTT_STATS_DBGFS_FILE_PERM   (QDF_FILE_USR_READ | \
					  QDF_FILE_GRP_READ | QDF_FILE_OTH_READ)

/* Delay in ms to get the HTT stats for debugfs entry */
#define PDEV_HTT_STATS_DBGFS_WAIT_TIME  2000

/* Char array size of parent directory of debugfs HTT stats */
#define PDEV_HTT_STATS_DBGFS_DIR_SIZE 19

/* Char array size of debugfs file for HTT stats */
#define PDEV_HTT_STATS_DBGFS_FILE_SIZE 7

/* dp_pdev_htt_stats_dbgfs_show() - Function to display HTT stats
 * @file: qdf debugfs file handler
 * @arg: pointer to HTT stats debugfs private object
 *
 * Return: QDF_STATUS
 */
static inline
QDF_STATUS
dp_pdev_htt_stats_dbgfs_show(qdf_debugfs_file_t file, void *arg)
{
	struct pdev_htt_stats_dbgfs_priv *priv = arg;
	struct dp_pdev *pdev = NULL;
	uint32_t config_param0 = 0;
	uint32_t config_param1 = 0;
	uint32_t config_param2 = 0;
	uint32_t config_param3 = 0;
	int cookie_val = 0;
	uint8_t mac_id = 0;
	QDF_STATUS ret = QDF_STATUS_SUCCESS;

	pdev = priv->pdev;
	pdev->dbgfs_cfg->m = file;

	qdf_mutex_acquire(&pdev->dbgfs_cfg->lock);

	dp_h2t_ext_stats_msg_send(pdev, priv->stats_id, config_param0,
				  config_param1, config_param2, config_param3,
				  cookie_val, DBG_STATS_COOKIE_HTT_DBGFS,
				  mac_id);
	ret = qdf_wait_single_event(&pdev->dbgfs_cfg->htt_stats_dbgfs_event,
				    PDEV_HTT_STATS_DBGFS_WAIT_TIME);
	if (ret == QDF_STATUS_E_TIMEOUT)
		QDF_TRACE(QDF_MODULE_ID_DP, QDF_TRACE_LEVEL_ERROR,
			  "Event timeout: Failed to get response from FW");

	qdf_mutex_release(&pdev->dbgfs_cfg->lock);
	return ret;
}

/* dp_pdev_dbgfs_init() - Init debugfs of HTT stats to create
 * debugfs directories and entries
 * @pdev: DP pdev handle
 *
 * Return: QDF_STATUS
 */
static inline
QDF_STATUS dp_pdev_dbgfs_init(struct dp_pdev *pdev)
{
	char dir_name[PDEV_HTT_STATS_DBGFS_DIR_SIZE] = {0};
	char file_name[PDEV_HTT_STATS_DBGFS_FILE_SIZE] = {0};
	int idx;
	char *net_dev_name = NULL;

	if (pdev->soc->cdp_soc.ol_ops->get_device_name) {
		net_dev_name = pdev->soc->cdp_soc.ol_ops->get_device_name(
					pdev->soc->ctrl_psoc, pdev->pdev_id);
	}

	if (net_dev_name == NULL)
		return QDF_STATUS_E_FAILURE;

	qdf_snprintf(dir_name, sizeof(dir_name), "dp_wifistats_%s", net_dev_name);

	pdev->dbgfs_cfg->debugfs_entry[0] = qdf_debugfs_create_dir(dir_name,
								   NULL);
	if (!pdev->dbgfs_cfg->debugfs_entry[0]) {
		QDF_TRACE(QDF_MODULE_ID_DP, QDF_TRACE_LEVEL_ERROR,
			  "Failed to create debugfs directory for HTT stats");
		return QDF_STATUS_E_FAILURE;
	}

	for (idx = 1; idx < PDEV_HTT_STATS_DBGFS_SIZE - 1; idx++) {
		qdf_snprintf(file_name, sizeof(file_name), "cmd%d", idx);
		pdev->dbgfs_cfg->priv[idx - 1].pdev = pdev;
		pdev->dbgfs_cfg->priv[idx - 1].stats_id = idx;
		pdev->dbgfs_cfg->pdev_htt_stats_dbgfs_ops[idx - 1].show =
						dp_pdev_htt_stats_dbgfs_show;
		pdev->dbgfs_cfg->pdev_htt_stats_dbgfs_ops[idx - 1].write = NULL;
		pdev->dbgfs_cfg->pdev_htt_stats_dbgfs_ops[idx - 1].priv =
						&pdev->dbgfs_cfg->priv[idx - 1];

		pdev->dbgfs_cfg->debugfs_entry[idx] =
			qdf_debugfs_create_file_simplified(
			   file_name, PDEV_HTT_STATS_DBGFS_FILE_PERM,
			   pdev->dbgfs_cfg->debugfs_entry[0],
			   &pdev->dbgfs_cfg->pdev_htt_stats_dbgfs_ops[idx - 1]);

		if (!pdev->dbgfs_cfg->debugfs_entry[idx]) {
			QDF_TRACE(QDF_MODULE_ID_DP, QDF_TRACE_LEVEL_ERROR,
				  "Failed to create htt stats dbgfs file for %d", idx);
			return QDF_STATUS_E_FAILURE;
		}
	}
	return QDF_STATUS_SUCCESS;
}

/* dp_pdev_htt_stats_dbgfs_init() - Function to allocate memory and initialize
 * debugfs for HTT stats
 * @pdev: dp pdev handle
 *
 * Return: QDF_STATUS
 */
QDF_STATUS dp_pdev_htt_stats_dbgfs_init(struct dp_pdev *pdev)
{
	pdev->dbgfs_cfg =
		qdf_mem_malloc(sizeof(struct pdev_htt_stats_dbgfs_cfg));
	if (!pdev->dbgfs_cfg) {
		QDF_TRACE(QDF_MODULE_ID_DP, QDF_TRACE_LEVEL_ERROR,
			  "%s: Memory allocation failed for pdev htt stats dbgfs cfg", __func__);
		return QDF_STATUS_E_NOMEM;
	}
	qdf_mutex_create(&pdev->dbgfs_cfg->lock);
	qdf_event_create(&pdev->dbgfs_cfg->htt_stats_dbgfs_event);
	pdev->dbgfs_cfg->htt_stats_dbgfs_msg_process = htt_stats_msg_receive;

	if (dp_pdev_dbgfs_init(pdev)) {
		QDF_TRACE(QDF_MODULE_ID_DP, QDF_TRACE_LEVEL_ERROR,
			  "%s: Failed to initialize for pdev htt stats dbgfs", __func__);
		dp_pdev_htt_stats_dbgfs_deinit(pdev);
		return QDF_STATUS_E_FAILURE;
	}
	return QDF_STATUS_SUCCESS;
}

/* dp_pdev_htt_stats_dbgfs_deinit() - Function to free memory and remove
 * debugfs directory and entries for HTT stats
 * @pdev: dp pdev handle
 *
 * Return: none
 */
void dp_pdev_htt_stats_dbgfs_deinit(struct dp_pdev *pdev)
{
	if (pdev->dbgfs_cfg) {
		qdf_mutex_destroy(&pdev->dbgfs_cfg->lock);
		qdf_event_destroy(&pdev->dbgfs_cfg->htt_stats_dbgfs_event);
		pdev->dbgfs_cfg->htt_stats_dbgfs_msg_process = NULL;

		if (pdev->dbgfs_cfg->debugfs_entry[0]) {
			qdf_debugfs_remove_dir_recursive(
					    pdev->dbgfs_cfg->debugfs_entry[0]);
			pdev->dbgfs_cfg->debugfs_entry[0] = NULL;
		}
		qdf_mem_free(pdev->dbgfs_cfg);
		pdev->dbgfs_cfg = NULL;
	}
}
#endif /* HTT_STATS_DEBUGFS_SUPPORT */
