/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _CNSS_MAIN_H
#define _CNSS_MAIN_H
#include <linux/version.h>
#include <asm/arch_timer.h>
#include <linux/etherdevice.h>
#include <linux/pm_qos.h>
#include <linux/platform_device.h>
#include <cnss2.h>
#ifdef CONFIG_QTI_MEMORY_DUMP_V2
#include <soc/qcom/memory_dump.h>
#endif

#ifdef CONFIG_CNSS2_KERNEL_SSR_FRAMEWORK
#include <soc/qcom/subsystem_restart.h>
#include <linux/esoc_client.h>
#endif

#include "qmi/qmi.h"
#include "bus/bus.h"

#define MAX_NO_OF_MAC_ADDR		4
#define QMI_WLFW_MAX_TIMESTAMP_LEN	32
#define CNSS_RDDM_TIMEOUT_MS		20000
#define RECOVERY_TIMEOUT		60000
#define TIME_CLOCK_FREQ_HZ		19200000
#define CNSS_DEVICE_NAME_MAX_LEN	16
#define CNSS_NUM_META_INFO_SEGMENTS	1
#define CNSS_RAMDUMP_MAGIC		0x574C414E /* WLAN in ASCII */
#define CNSS_RAMDUMP_VERSION		0
#define CNSS_RAMDUMP_VERSION_V2		2
#define CNSS_RAMDUMP_FILE_NAME_MAX_LEN	(2 * CNSS_DEVICE_NAME_MAX_LEN)

#define CNSS_DMS_QMI_CONNECTION_WAIT_MS 50
#define CNSS_DMS_QMI_CONNECTION_WAIT_RETRY 200
#define CNSS_DAEMON_CONNECT_TIMEOUT_MS  30000
#define CNSS_CAL_DB_FILE_PREFIX "wlfw_cal_01"
#define CNSS_CAL_DB_FILE_SUFFIX ".bin"

/* FW type value is encoded in the most significant nibble of board_id
 * in DTS or in OTP register
 */
#define CNSS_FW_TYPE_MASK		0xF000
#define CNSS_FW_TYPE_SHIFT		12

#define CNSS_PCI_SWITCH_LINK_MASK      GENMASK(1, 0)

#if (KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE)
typedef void ramdump_device_t;
#else
typedef struct ramdump_device ramdump_device_t;
#endif

enum cnss_cal_db_op {
	CNSS_CAL_DB_UPLOAD,
	CNSS_CAL_DB_DOWNLOAD,
	CNSS_CAL_DB_INVALID_OP,
};

/* Currently these target mem modes are supported for various targets
 *
 *				IPQ8074
 *
 * Start Address for all modes: 0x4B000000
 * All offsets mentioned below are with reference to the above start address
 *
 * NOTE: Mode 3 and 4 are for external use cases, please do not use this for
 * adding new modes
 * +======+========+=========+===========+===========+===========+
 * | MODE | Memory | BDF Off | Caldb Off | QDSS Off  |M3 Dump Off|
 * |      |        |  256KB  |   4.5MB   |    1MB    |    1MB    |
 * +======+========+=========+===========+===========+===========+
 * |   0  |  97MB  | 0xC0000 |  0xA00000 | 0x5F00000 | 0x6000000 |
 * +------+--------+---------+-----------+-----------+-----------+
 * |   1  |  57MB  | 0xC0000 |  0xA00000 | 0x3700000 | 0x3800000 |
 * +------+--------+---------+-----------+-----------+-----------+
 * |   2  |  42MB  | 0xC0000 |  DISABLED | 0x2800000 | 0x2900000 |
 * +------+--------+---------+-----------+-----------+-----------+
 * |   3  | 127MB  | 0xC0000 |  0xA00000 | 0x7D00000 | 0x7E00000 |
 * +------+--------+---------+-----------+-----------+-----------+
 * |   4  |  57MB  | 0xC0000 |  0xA00000 | 0x3700000 | 0x3800000 |
 * +======+========+=========+===========+===========+===========+
 *
 *				IPQ6018
 *
 * Start Address for all modes: 0x4AB00000
 * All offsets mentioned below are with reference to the above start address
 *
 * +======+========+=========+===========+===========+===========+
 * | MODE | Memory | BDF Off | Caldb Off | QDSS Off  |M3 Dump Off|
 * |      |        |  256KB  |   4.5MB   |    1MB    |    1MB    |
 * +======+========+=========+===========+===========+===========+
 * |   0  |  87MB  | 0xC0000 |  0xA00000 | 0x5500000 | 0x5600000 |
 * +------+--------+---------+-----------+-----------+-----------+
 * |   1  |  57MB  | 0xC0000 |  0xA00000 | 0x3700000 | 0x3800000 |
 * +------+--------+---------+-----------+-----------+-----------+
 * |   2  |  42MB  | 0xC0000 |  DISABLED | 0x2800000 | 0x2900000 |
 * +======+========+=========+===========+===========+===========+
 *
 *				IPQ5018
 *
 * Start Address for all Modes: 0x4B000000
 * All offsets mentioned below are with reference to the above start address
 *
 * +======+========+=========+===========+===========+===========+
 * | MODE | Memory | BDF Off | Caldb Off | QDSS Off  |M3 Dump Off|
 * |      |        |  256KB  |    2MB    |    1MB    |    1MB    |
 * +======+========+=========+===========+===========+===========+
 * |   0  |  28MB  | 0xA00000| 0x1A00000 | 0x1900000 | 0x1800000 |
 * +------+--------+---------+-----------+-----------+-----------+
 * |   1  |  28MB  | 0xA00000| 0x1A00000 | 0x1900000 | 0x1800000 |
 * +------+--------+---------+-----------+-----------+-----------+
 * |   2  |  26MB  | 0xA00000|  DISABLED | 0x1900000 | 0x1800000 |
 * +======+========+=========+===========+===========+===========+
 *
 *				QCN9000
 *
 * Start Address varies for each RDP, please refer RDP specific DTS file.
 * All offsets mentioned below are with reference to the start address from DTS
 * HREMOTE Offset is always same as Start Offset
 *
 * NOTE: Mode 3 and 4 are for external use cases, please do not use this for
 * adding new modes
 *
 * +======+========+===========+===========+===========+===========+==========+
 * | MODE | Memory |  HREMOTE  |M3 Dump Off| QDSS Off  | Caldb Off | MHI DMA  |
 * |      |        |    SIZE   |    1MB    |    1MB    |    8MB    | RESERVED |
 * +======+========+===========+===========+===========+===========+==========+
 * |   0  |  45MB  |    35MB   | 0x2300000 | 0x2400000 | 0x2500000 |   24MB   |
 * +------+--------+-----------+-----------+-----------+-----------+----------+
 * |   1  |  30MB  |    20MB   | 0x1400000 | 0x1500000 | 0x1600000 |   16MB   |
 * +------+--------+-----------+-----------+-----------+-----------+----------+
 * |   2  |  17MB  |    15MB   |  0xF00000 | 0x1000000 |  DISABLED |   16MB   |
 * +------+--------+-----------+-----------+-----------+-----------+----------+
 * |   3  |  65MB  |    55MB   | 0x3700000 | 0x3800000 | 0x3900000 |   24MB   |
 * +------+--------+-----------+-----------+-----------+-----------+----------+
 * |   4  |  33MB  |    23MB   | 0x1700000 | 0x1800000 | 0x1900000 |   24MB   |
 * +======+========+===========+===========+===========+===========+==========+
 *
 *				IPQ9574
 *
 * Start Address for all Modes: 0x4AB00000
 * All offsets mentioned below are with reference to the above start address
 *
 * +======+========+=========+===========+===========+===========+
 * | MODE | Memory | BDF Off | Caldb Off | QDSS Off  |M3 Dump Off|
 * |      |        |  256KB  |    5MB    |    1MB    |    1MB    |
 * +======+========+=========+===========+===========+===========+
 * |   0  |  50MB  | 0xC00000| 0x2D00000 | 0x2C00000 | 0x2B00000 |
 * +------+--------+---------+-----------+-----------+-----------+
 * |   1  |  36MB  | 0xC00000| 0x1F00000 | 0x1E00000 | 0x1D00000 |
 * +======+========+=========+===========+===========+===========+
 *

 *				QCN9224
 *
 * Start Address varies for each RDP, please refer RDP specific DTS file.
 * All offsets mentioned below are with reference to the start address from DTS
 * HREMOTE Offset is always same as Start Offset
 *
 * MLO uses 16MB and comes at the end of all QCN9224 memory and MHI mem nodes
 * RDDM size of QCN9224 is 6M and part of MHI regions.
 *
 * +======+========+===========+===========+===========+===========+==========+
 * | MODE | Memory |  HREMOTE  |M3 Dump Off| QDSS Off  | Caldb Off | MHI DMA  |
 * |      |        |    SIZE   |    1MB    |    1MB    |    8MB    | RESERVED |
 * +======+========+===========+===========+===========+===========+==========+
 * |   0  |  46MB  |    36MB   | 0x2400000 | 0x2500000 | 0x2600000 |   26MB   |
 * +======+========+===========+===========+===========+===========+==========+
 */
#define MAX_TGT_MEM_MODES		7

#define CNSS_EVENT_SYNC   BIT(0)
#define CNSS_EVENT_UNINTERRUPTIBLE BIT(1)
#define CNSS_EVENT_SYNC_UNINTERRUPTIBLE (CNSS_EVENT_SYNC | \
				CNSS_EVENT_UNINTERRUPTIBLE)

enum cnss_dev_bus_type {
	CNSS_BUS_NONE = -1,
	CNSS_BUS_PCI,
	CNSS_BUS_AHB
};

struct cnss_vreg_cfg {
	const char *name;
	u32 min_uv;
	u32 max_uv;
	u32 load_ua;
	u32 delay_us;
	u32 need_unvote;
};

struct cnss_vreg_info {
	struct list_head list;
	struct regulator *reg;
	struct cnss_vreg_cfg cfg;
	u32 enabled;
};

enum cnss_vreg_type {
	CNSS_VREG_PRIM,
};

struct cnss_clk_cfg {
	const char *name;
	u32 freq;
	u32 required;
};

struct cnss_clk_info {
	struct list_head list;
	struct clk *clk;
	struct cnss_clk_cfg cfg;
	u32 enabled;
};

struct cnss_pinctrl_info {
	struct pinctrl *pinctrl;
	struct pinctrl_state *bootstrap_active;
	struct pinctrl_state *wlan_en_active;
	struct pinctrl_state *wlan_en_sleep;
};

#ifdef CONFIG_CNSS2_KERNEL_RPROC_FRAMEWORK
struct subsys_desc {
	const char *name;
	struct device *dev;
};
#endif

struct cnss_subsys_info {
	struct subsys_device *subsys_device;
#ifndef CONFIG_CNSS2_KERNEL_5_15
	struct subsys_desc subsys_desc;
#endif
	void *subsys_handle;
	bool subsystem_put_in_progress;
};

struct cnss_ramdump_info {
	ramdump_device_t *ramdump_dev;
	unsigned long ramdump_size;
	void *ramdump_va;
	phys_addr_t ramdump_pa;
#ifdef CONFIG_QTI_MEMORY_DUMP_V2
	struct msm_dump_data dump_data;
#endif
};

struct cnss_dump_seg {
	unsigned long address;
	void *v_address;
	unsigned long size;
	u32 type;
};

struct cnss_dump_data {
	u32 version;
	u32 magic;
	char name[32];
	phys_addr_t paddr;
	int nentries;
	u32 seg_version;
};

struct cnss_ramdump_info_v2 {
	ramdump_device_t *ramdump_dev;
	unsigned long ramdump_size;
	void *dump_data_vaddr;
	u8 dump_data_valid;
	struct cnss_dump_data dump_data;
};

struct cnss_esoc_info {
	struct esoc_desc *esoc_desc;
	u8 notify_modem_status;
	void *modem_notify_handler;
	int modem_current_status;
};

struct cnss_bus_bw_info {
	struct msm_bus_scale_pdata *bus_scale_table;
	u32 bus_client;
	int current_bw_vote;
};

struct cnss_wlan_mac_addr {
	u8 mac_addr[MAX_NO_OF_MAC_ADDR][ETH_ALEN];
	u32 no_of_mac_addr_set;
};

struct cnss_wlan_mac_info {
	struct cnss_wlan_mac_addr wlan_mac_addr;
	bool is_wlan_mac_set;
};

struct cnss_fw_mem {
	size_t size;
	void *va;
	phys_addr_t pa;
	u8 valid;
	u32 type;
};

struct wlfw_rf_chip_info {
	u32 chip_id;
	u32 chip_family;
};

struct wlfw_rf_board_info {
	u32 board_id;
	u32 board_id_override;
	/* Board ID is u32 but actual used size varies based on target type */
	u16 num_bytes;
};

struct wlfw_soc_info {
	u32 soc_id;
};

struct wlfw_fw_version_info {
	u32 fw_version;
	char fw_build_timestamp[QMI_WLFW_MAX_TIMESTAMP_LEN + 1];
};

enum cnss_mem_type {
	CNSS_MEM_TYPE_MSA,
	CNSS_MEM_TYPE_DDR,
	CNSS_MEM_BDF,
	CNSS_MEM_M3,
	CNSS_MEM_CAL_V01,
	CNSS_MEM_DPD_V01,
	CNSS_MEM_ETR,
	CNSS_MEM_HANG_DATA,
	CNSS_MEM_MLO_GLOBAL,
	CNSS_MEM_PAGEABLE,
	CNSS_MEM_AFC,
};

enum cnss_fw_dump_type {
	CNSS_FW_IMAGE,
	CNSS_FW_RDDM,
	CNSS_FW_REMOTE_HEAP,
	CNSS_FW_PAGEABLE,
	/* Below regions added from CNSS_RAMDUMP_VERSION_V2 */
	CNSS_FW_REMOTE_M3_DUMP,
	CNSS_FW_REMOTE_ETR,
	CNSS_FW_REMOTE_CALDB,
	CNSS_FW_REMOTE_AFC,
	CNSS_FW_REMOTE_MLO_GLOBAL,
	CNSS_FW_DUMP_TYPE_MAX,
};

struct cnss_dump_entry {
	u32 type;
	u32 entry_start;
	u32 entry_num;
};

struct cnss_dump_meta_info {
	u32 magic;
	u32 version;
	u32 chipset;
	u32 total_entries;
	struct cnss_dump_entry entry[CNSS_FW_DUMP_TYPE_MAX];
};

enum cnss_driver_event_type {
	CNSS_DRIVER_EVENT_SERVER_ARRIVE,
	CNSS_DRIVER_EVENT_SERVER_EXIT,
	CNSS_DRIVER_EVENT_REQUEST_MEM,
	CNSS_DRIVER_EVENT_FW_MEM_READY,
	CNSS_DRIVER_EVENT_FW_READY,
	CNSS_DRIVER_EVENT_COLD_BOOT_CAL_START,
	CNSS_DRIVER_EVENT_COLD_BOOT_CAL_DONE,
	CNSS_DRIVER_EVENT_REGISTER_DRIVER,
	CNSS_DRIVER_EVENT_UNREGISTER_DRIVER,
	CNSS_DRIVER_EVENT_RECOVERY,
	CNSS_DRIVER_EVENT_FORCE_FW_ASSERT,
	CNSS_DRIVER_EVENT_POWER_UP,
	CNSS_DRIVER_EVENT_POWER_DOWN,
	CNSS_DRIVER_EVENT_IDLE_RESTART,
	CNSS_DRIVER_EVENT_IDLE_SHUTDOWN,
	CNSS_DRIVER_EVENT_QDSS_TRACE_REQ_MEM,
	CNSS_DRIVER_EVENT_QDSS_TRACE_SAVE,
	CNSS_DRIVER_EVENT_QDSS_TRACE_FREE,
	CNSS_DRIVER_EVENT_M3_DUMP_UPLOAD_REQ,
	CNSS_DRIVER_EVENT_QDSS_MEM_READY,
	CNSS_DRIVER_EVENT_QDSS_TRACE_REQ_DATA,
	CNSS_DRIVER_EVENT_RAMDUMP_DONE,
	CNSS_DRIVER_EVENT_MAX,
};

enum cnss_driver_state {
	CNSS_QMI_WLFW_CONNECTED,
	CNSS_FW_MEM_READY,
	CNSS_FW_READY,
	CNSS_COLD_BOOT_CAL,
	CNSS_DRIVER_LOADING,
	CNSS_DRIVER_UNLOADING,
	CNSS_DRIVER_IDLE_RESTART,
	CNSS_DRIVER_IDLE_SHUTDOWN,
	CNSS_DRIVER_PROBED,
	CNSS_DRIVER_RECOVERY,
	CNSS_FW_BOOT_RECOVERY,
	CNSS_DEV_ERR_NOTIFY,
	CNSS_DRIVER_DEBUG,
	CNSS_COEX_CONNECTED,
	CNSS_IMS_CONNECTED,
	CNSS_IN_SUSPEND_RESUME,
	CNSS_DAEMON_CONNECTED,
	CNSS_QDSS_STARTED,
	CNSS_RECOVERY_WAIT_FOR_DRIVER,
	CNSS_RDDM_DUMP_IN_PROGRESS,
};

struct cnss_recovery_data {
	enum cnss_recovery_reason reason;
};

enum cnss_pins {
	CNSS_WLAN_EN,
	CNSS_PCIE_TXP,
	CNSS_PCIE_TXN,
	CNSS_PCIE_RXP,
	CNSS_PCIE_RXN,
	CNSS_PCIE_REFCLKP,
	CNSS_PCIE_REFCLKN,
	CNSS_PCIE_RST,
	CNSS_PCIE_WAKE,
};

struct cnss_pin_connect_result {
	u32 fw_pwr_pin_result;
	u32 fw_phy_io_pin_result;
	u32 fw_rf_pin_result;
	u32 host_pin_result;
};

enum cnss_debug_quirks {
	LINK_DOWN_SELF_RECOVERY,
	SKIP_DEVICE_BOOT,
	USE_CORE_ONLY_FW,
	SKIP_RECOVERY,
	QMI_BYPASS,
	ENABLE_WALTEST,
	ENABLE_PCI_LINK_DOWN_PANIC,
	FBC_BYPASS,
	ENABLE_DAEMON_SUPPORT,
	DISABLE_DRV,
};

enum cnss_bdf_type {
	CNSS_BDF_BIN,
	CNSS_BDF_ELF,
	CNSS_BDF_REGDB = 4,
	CNSS_BDF_WIN,
	CNSS_CALDATA_WIN,
	CNSS_BDF_HDS,
	CNSS_BDF_RXGAINLUT,
	CNSS_BDF_DUMMY = 255,
};

enum cnss_cal_status {
	CNSS_CAL_DONE,
	CNSS_CAL_TIMEOUT,
};

struct cnss_cal_info {
	enum cnss_cal_status cal_status;
};

struct cnss_control_params {
	unsigned long quirks;
	unsigned int mhi_timeout;
	unsigned int qmi_timeout;
	unsigned int bdf_type;
	unsigned int time_sync_period;
};

struct cnss_cpr_info {
	resource_size_t tcs_cmd_base_addr;
	resource_size_t tcs_cmd_data_addr;
	void __iomem *tcs_cmd_base_addr_io;
	void __iomem *tcs_cmd_data_addr_io;
	u32 cpr_pmic_addr;
	u32 voltage;
};

enum cnss_ce_index {
	CNSS_CE_00,
	CNSS_CE_01,
	CNSS_CE_02,
	CNSS_CE_03,
	CNSS_CE_04,
	CNSS_CE_05,
	CNSS_CE_06,
	CNSS_CE_07,
	CNSS_CE_08,
	CNSS_CE_09,
	CNSS_CE_10,
	CNSS_CE_11,
	CNSS_CE_12,
	CNSS_CE_13,
	CNSS_CE_14,
	CNSS_CE_15,
	CNSS_CE_COMMON,
};

enum cnss_module_param_feature {
	CALDATA,
	REGDB,
};

/* M3 SSR Dump related constants and structure */
#define M3_DUMP_OPEN_TIMEOUT 10000
#define M3_DUMP_OPEN_COMPLETION_TIMEOUT (2 * M3_DUMP_OPEN_TIMEOUT)
#define M3_DUMP_READ_TIMER_TIMEOUT 10000
#define M3_DUMP_COMPLETION_TIMEOUT 300000
struct m3_dump {
	struct task_struct *task;
	struct timer_list open_timer;
	struct completion open_complete;
	struct timer_list read_timer;
	struct completion read_complete;
	atomic_t open_timedout;
	atomic_t read_timedout;
	u32 pdev_id;
	u32 size;
	u64 timestamp;
	bool file_open;
	void *dump_addr;
};

#ifdef CONFIG_CNSS2_QGIC2M
struct qgic2_msi {
	int irq_num;
	uint32_t msi_gicm_base_data;
	uint32_t msi_gicm_addr_lo;
	uint32_t msi_gicm_addr_hi;
};
#endif

struct target_data {
	void *bar_addr_va;
	u64 bar_addr_pa;
	u32 bar_size;
	struct qgic2_msi *qgic2_msi;
};

enum cnss_fw_type {
	CNSS_FW_DEFAULT, /* 0 - amss.bin */
	CNSS_FW_DUAL_MAC, /* 1 - amss_dualmac.bin */
	CNSS_FW_MAX, /* 2 to 15 is reserved */
};

struct qdss_stream_data {
	dma_addr_t              qdss_paddr;
	void __iomem            *qdss_vaddr;
	struct work_struct	qld_stream_work;
	struct socket           *qld_stream_sock;
	atomic_t                seq_no;
	atomic_t                completed_seq_no;
};

enum cnss_recovery_type {
	CNSS_ASYNC_RECOVERY, /* asynchronous recovery */
	CNSS_SYNC_RECOVERY, /* synchronous recovery */
};

struct cnss_bus_ops {
	int (*cnss_bus_init)(struct cnss_plat_data *plat_priv);
	void (*cnss_bus_deinit)(struct cnss_plat_data *plat_priv);
	int (*cnss_bus_alloc_fw_mem)(struct cnss_plat_data *plat_priv);
	void (*cnss_bus_free_fw_mem)(struct cnss_plat_data *plat_priv);
	int (*cnss_bus_alloc_qdss_mem)(struct cnss_plat_data *plat_priv);
	void (*cnss_bus_free_qdss_mem)(struct cnss_plat_data *plat_priv);
	int (*cnss_bus_driver_probe)(struct cnss_plat_data *plat_priv);
	int (*cnss_bus_driver_remove)(struct cnss_plat_data *plat_priv);
	int (*cnss_bus_dev_powerup)(struct cnss_plat_data *plat_priv);
	int (*cnss_bus_dev_shutdown)(struct cnss_plat_data *plat_priv);
	int (*cnss_bus_load_m3)(struct cnss_plat_data *plat_priv);
	u32 (*cnss_bus_get_wake_irq)(struct cnss_plat_data *plat_priv);
	int (*cnss_bus_force_fw_assert_hdlr)(struct cnss_plat_data *plat_priv);
	void (*cnss_bus_fw_boot_timeout_hdlr)(struct timer_list *timer);
	int (*cnss_bus_dev_crash_shutdown)(struct cnss_plat_data *plat_priv);
	void (*cnss_bus_collect_dump_info)(struct cnss_plat_data *plat_priv,
						bool in_panic);
	int (*cnss_bus_dev_ramdump)(struct cnss_plat_data *plat_priv);
	int (*cnss_bus_register_driver_hdlr)(struct cnss_plat_data *plat_priv,
						void *data);
	int (*cnss_bus_unregister_driver_hdlr)
				(struct cnss_plat_data *plat_priv);
	int (*cnss_bus_driver_modem_status)
			(struct cnss_plat_data *plat_priv,
					int modem_current_status);
	int (*cnss_bus_update_status)(struct cnss_plat_data *plat_priv,
					enum cnss_driver_status status);
	int (*cnss_bus_reg_read)(struct device *dev, u32 addr, u32 *val,
					void __iomem *base);
	int (*cnss_bus_reg_write)(struct device *dev, u32 addr, u32 val,
				void __iomem *base);
	int (*cnss_bus_get_soc_info)
			(struct device *dev, struct cnss_soc_info *info);
	u64 (*cnss_bus_get_q6_time)(struct device *dev);
	int (*cnss_bus_get_msi_irq)(struct device *dev, unsigned int vector);
	void (*cnss_bus_get_msi_address)(struct device *dev, u32 *msi_addr_low,
				u32 *msi_addr_high);
	int (*cnss_bus_get_user_msi_assignment)(struct device *dev,
				char *user_name, int *num_vectors,
				u32 *user_base_data, u32 *base_vector);
};

struct cnss_plat_data {
	void *wlan_priv;
	struct platform_device *plat_dev;
	struct platform_device_id *plat_dev_id;
	void *pci_dev;
	void *lvirq;
	void *pci_dev_id;
	void *bus_priv;
	void *rproc_handle;
	void *rproc_rpd_handle;
	int qrtr_node_id;
	int userpd_id;
	int pci_slot_id;
	char device_name[CNSS_DEVICE_NAME_MAX_LEN];
	struct cnss_vreg_info *vreg_info;
	enum cnss_dev_bus_type bus_type;
	struct list_head vreg_list;
	struct list_head clk_list;
	struct cnss_pinctrl_info pinctrl_info;
	struct cnss_subsys_info subsys_info;
	bool recovery_enabled;
	enum cnss_recovery_type recovery_type;
	struct cnss_ramdump_info ramdump_info;
	struct cnss_ramdump_info_v2 ramdump_info_v2;
	struct cnss_esoc_info esoc_info;
	struct cnss_bus_bw_info bus_bw_info;
	struct notifier_block modem_nb;
	struct notifier_block rpd_nb;
	struct cnss_platform_cap cap;
	struct pm_qos_request qos_request;
	struct cnss_device_version device_version;
	unsigned long device_id;
	struct cnss_wlan_driver *driver_ops;
	enum cnss_driver_status driver_status;
	u32 recovery_count;
	u8 recovery_mode;
	u8 standby_mode;
	unsigned long driver_state;
	struct list_head event_list;
	spinlock_t event_lock; /* spinlock for driver work event handling */
	struct work_struct event_work;
	struct work_struct recovery_work;
	struct workqueue_struct *event_wq;
	struct workqueue_struct *recovery_wq;
	struct work_struct cal_work;
	struct qmi_handle qmi_wlfw;
	struct sockaddr_qrtr sq;
	struct wlfw_rf_chip_info chip_info;
	struct wlfw_rf_board_info board_info;
	struct wlfw_soc_info soc_info;
	struct wlfw_fw_version_info fw_version_info;
	struct cnss_dev_mem_info dev_mem_info[CNSS_MAX_DEV_MEM_NUM];
	u32 otp_version;
	u32 fw_mem_seg_len;
	struct cnss_fw_mem fw_mem[QMI_WLFW_MAX_NUM_MEM_SEG_V01];
	struct cnss_fw_mem m3_mem;
	struct cnss_fw_mem *cal_mem;
	u64 cal_time;
	bool cbc_file_download;
	u32 cal_file_size;
	u32 qdss_mem_seg_len;
	struct cnss_fw_mem qdss_mem[QMI_WLFW_MAX_NUM_MEM_SEG_V01];
	int tgt_mem_cfg_mode;
	u32 *qdss_reg;
	struct cnss_pin_connect_result pin_result;
	struct dentry *root_dentry;
	atomic_t pm_count;
	struct timer_list fw_boot_timer;
	struct completion power_up_complete;
	unsigned int wlfw_service_instance_id;
	unsigned int service_id;
	struct notifier_block modem_atomic_nb;
	struct notifier_block rpd_atomic_nb;
	struct completion cal_complete;
	struct mutex dev_lock; /* mutex for register access through debugfs */
	u32 device_freq_hz;
	u32 diag_reg_read_addr;
	u32 diag_reg_read_mem_type;
	u32 diag_reg_read_len;
	u8 *diag_reg_read_buf;
	u8 cal_done;
	u8 powered_on;
	char *firmware_name;
	struct completion rddm_complete;
	struct completion recovery_complete;
	struct cnss_control_params ctrl_params;
	struct cnss_cpr_info cpr_info;
	u64 antenna;
	u64 grant;
	struct qmi_handle coex_qmi;
	struct qmi_handle ims_qmi;
	struct qmi_txn txn;
	u64 dynamic_feature;
	u64 target_assert_timestamp;
	u8 target_asserted;
	u32 daemon_support;
	u32 cold_boot_support;
	bool caldata_support;
	u32 eeprom_caldata_read_timeout;
	bool dma_alloc_supported;
	struct m3_dump m3_dump_data;
	union {
		struct target_data tgt_data;
	};
	bool hds_support;
	bool regdb_support;
	bool rxgainlut_support;
	u32 qdss_support;
	u32 qdss_etr_sg_mode;
	enum wlfw_bdf_dnld_method_v01 bdf_dnld_method;
	u32 probe_order;
	bool mlo_support;
	bool mlo_capable;
	/* This bar variable will be valid only for AHB devices. */
	void __iomem *bar;
	struct cnss_mlo_group_info *mlo_group_info;
	struct cnss_mlo_chip_info *mlo_chip_info;
	bool enable_intx;
	bool fw_ini_cfg_support;
	bool regdb_mandatory;
	enum cnss_fw_type firmware_type;
	struct qdss_stream_data qdss_stream;
	bool cal_in_progress;
	bool rd_dev_present;
	bool mlo_default_cfg;
	struct cnss_mlo_chip_info *adj_mlo_chip_info[CNSS_MAX_ADJ_CHIPS];
	enum cnss_recovery_reason reason;
	enum cnss_crash_type crash_type;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0))
	u8 switch_link_enable;
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	struct work_struct crash_work;
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
	struct srcu_notifier_head *notifier_list[2];
#endif
	struct completion soc_reset_request_complete;
	struct cnss_bus_ops *ops;
	bool wsi_remap_state;
};

#ifdef CONFIG_ARCH_QCOM
static inline u64 cnss_get_host_timestamp(struct cnss_plat_data *plat_priv)
{
	u64 ticks = __arch_counter_get_cntvct();
	u32 freq = arch_timer_get_cntfrq();

	do_div(ticks, freq / 100000);

	return ticks * 10;
}
#else
static inline u64 cnss_get_host_timestamp(struct cnss_plat_data *plat_priv)
{
	struct timespec64 ts;

	ktime_get_ts64(&ts);

	return (ts.tv_sec * 1000000) + (ts.tv_nsec / 1000);
}
#endif

struct cnss_plat_data *cnss_get_plat_priv(struct platform_device *plat_dev);
int cnss_driver_event_post(struct cnss_plat_data *plat_priv,
			   enum cnss_driver_event_type type,
			   u32 flags, void *data);
int cnss_get_vreg(struct cnss_plat_data *plat_priv);
int cnss_get_vreg_type(struct cnss_plat_data *plat_priv,
		       enum cnss_vreg_type type);
void cnss_put_vreg_type(struct cnss_plat_data *plat_priv,
			enum cnss_vreg_type type);
int cnss_vreg_on_type(struct cnss_plat_data *plat_priv,
		      enum cnss_vreg_type type);
int cnss_vreg_off_type(struct cnss_plat_data *plat_priv,
		       enum cnss_vreg_type type);
int cnss_get_clk(struct cnss_plat_data *plat_priv);
void cnss_put_clk(struct cnss_plat_data *plat_priv);
int cnss_vreg_unvote_type(struct cnss_plat_data *plat_priv,
			  enum cnss_vreg_type type);
int cnss_get_pinctrl(struct cnss_plat_data *plat_priv);
int cnss_power_on_device(struct cnss_plat_data *plat_priv, int device_id);
int cnss_power_off_device(struct cnss_plat_data *plat_priv, int device_id);
bool cnss_is_device_powered_on(struct cnss_plat_data *plat_priv);
int cnss_register_subsys(struct cnss_plat_data *plat_priv);
void cnss_unregister_subsys(struct cnss_plat_data *plat_priv);
int cnss_register_ramdump(struct cnss_plat_data *plat_priv);
void cnss_unregister_ramdump(struct cnss_plat_data *plat_priv);
void cnss_set_pin_connect_status(struct cnss_plat_data *plat_priv);
u32 cnss_get_wake_msi(struct cnss_plat_data *plat_priv);
struct cnss_plat_data *cnss_get_plat_priv_by_device_id(int id);
struct cnss_plat_data *cnss_get_plat_priv_by_qrtr_node_id(int node_id);
struct cnss_plat_data *cnss_get_plat_priv_by_instance_id(int instance_id);
struct cnss_plat_data *cnss_get_plat_priv(struct platform_device *plat_dev);
int cnss_get_plat_env_index_from_plat_priv(struct cnss_plat_data *plat_priv);
int cnss_qca9000_shutdown_part2(struct cnss_plat_data *plat_priv);

#if defined(CNSS_LOWMEM_PROFILE) && defined(CONFIG_CNSS2_KERNEL_IPQ) && \
	defined(CNSS_FW_MOUNT_SUPPORT)
#define MOUNT_PATH			"/lib/wifi/mount/mount_fw_partition.sh"
#define UMOUNT_PATH			"/lib/wifi/mount/umount_fw_partition.sh"

void cnss_mount_firmware(struct cnss_plat_data *plat_priv);
void cnss_unmount_firmware(struct cnss_plat_data *plat_priv);
void cnss_schedule_umount_firmware(struct work_struct *work);
#else
static inline void cnss_mount_firmware(struct cnss_plat_data *plat_priv)
{
}
static inline void cnss_unmount_firmware(struct cnss_plat_data *plat_priv)
{
}
static inline void cnss_schedule_umount_firmware(struct work_struct *work)
{
}
#endif

int cnss_get_cpr_info(struct cnss_plat_data *plat_priv);
int cnss_update_cpr_info(struct cnss_plat_data *plat_priv);
void cnss_update_platform_feature_support(u8 type, u32 instance_id, u32 value);
const char *cnss_get_fw_path(struct cnss_plat_data *plat_priv);
int cnss_cal_file_download_to_mem(struct cnss_plat_data *plat_priv,
				  u32 *cal_file_size);
struct cnss_plat_data *cnss_get_plat_priv_by_chip_id(int chip_id);
int cnss_set_fw_type_and_name(struct cnss_plat_data *plat_priv);
struct cnss_plat_data *cnss_get_plat_priv_by_soc_id(int soc_id);
int cnss_get_mlo_master_chip_id(struct cnss_mlo_group_info *mlo_group_info);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0))
void cnss_modify_link_speed(struct cnss_plat_data *plat_priv);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
bool cnss_check_li_target(struct cnss_plat_data *plat_priv);
bool cnss_check_be_target(struct cnss_plat_data *plat_priv);
#endif
#endif /* _CNSS_MAIN_H */
