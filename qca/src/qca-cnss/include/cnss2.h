/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef _NET_CNSS2_H
#define _NET_CNSS2_H

#include <linux/pci.h>

#define CNSS_MAX_FILE_NAME		20
#define CNSS_MAX_TIMESTAMP_LEN		32
#define CNSS_MAX_DEV_MEM_NUM		4

/*
 * Temporary change for compilation, will be removed
 * after WLAN host driver switched to use new APIs
 */
#define CNSS_API_WITH_DEV

#define CNSS_MAX_LINKS_PER_CHIP		2
#define CNSS_MAX_MLO_CHIPS		4
#define CNSS_MAX_MLO_GROUPS		2
#define CNSS_MAX_ADJ_CHIPS		2

enum cnss_bus_width_type {
	CNSS_BUS_WIDTH_NONE,
	CNSS_BUS_WIDTH_IDLE,
	CNSS_BUS_WIDTH_LOW,
	CNSS_BUS_WIDTH_MEDIUM,
	CNSS_BUS_WIDTH_HIGH,
	CNSS_BUS_WIDTH_VERY_HIGH
};

enum cnss_notif_type {
	CNSS_BEFORE_SHUTDOWN,
	CNSS_AFTER_SHUTDOWN,
	CNSS_BEFORE_POWERUP,
	CNSS_AFTER_POWERUP,
	CNSS_RAMDUMP_NOTIFICATION,
	CNSS_POWERUP_FAILURE,
	CNSS_PROXY_VOTE,
	CNSS_PROXY_UNVOTE,
	CNSS_SOC_RESET,
	CNSS_PREPARE_FOR_FATAL_SHUTDOWN,
	CNSS_RAMDUMP_DONE,
	/* The below event should be the last event for all devices */
	CNSS_NOTIF_TYPE_MAX
};

enum cnss_platform_cap_flag {
	CNSS_HAS_EXTERNAL_SWREG = 0x01,
	CNSS_HAS_UART_ACCESS = 0x02,
};

struct cnss_platform_cap {
	u32 cap_flag;
};

struct cnss_fw_files {
	char image_file[CNSS_MAX_FILE_NAME];
	char board_data[CNSS_MAX_FILE_NAME];
	char otp_data[CNSS_MAX_FILE_NAME];
	char utf_file[CNSS_MAX_FILE_NAME];
	char utf_board_data[CNSS_MAX_FILE_NAME];
	char epping_file[CNSS_MAX_FILE_NAME];
	char evicted_data[CNSS_MAX_FILE_NAME];
};

struct cnss_device_version {
	u32 family_number;
	u32 device_number;
	u32 major_version;
	u32 minor_version;
};

struct cnss_dev_mem_info {
	u64 start;
	u64 size;
};

struct cnss_soc_info {
	void __iomem *va;
	phys_addr_t pa;
	uint32_t chip_id;
	uint32_t chip_family;
	uint32_t board_id;
	uint32_t soc_id;
	uint32_t fw_version;
	char fw_build_timestamp[CNSS_MAX_TIMESTAMP_LEN + 1];
	struct cnss_device_version device_version;
	struct cnss_dev_mem_info dev_mem_info[CNSS_MAX_DEV_MEM_NUM];
};

struct cnss_wlan_runtime_ops {
	int (*runtime_suspend)(struct pci_dev *pdev);
	int (*runtime_resume)(struct pci_dev *pdev);
};

struct cnss_wlan_driver {
	char *name;
	int  (*probe)(struct pci_dev *pdev, const struct pci_device_id *id);
	void (*remove)(struct pci_dev *pdev);
	int (*idle_restart)(struct pci_dev *pdev,
			    const struct pci_device_id *id);
	int  (*idle_shutdown)(struct pci_dev *pdev);
	int  (*reinit)(struct pci_dev *pdev, const struct pci_device_id *id);
	void (*shutdown)(struct pci_dev *pdev);
	void (*crash_shutdown)(struct pci_dev *pdev);
	int  (*suspend)(struct pci_dev *pdev, pm_message_t state);
	int  (*resume)(struct pci_dev *pdev);
	int  (*suspend_noirq)(struct pci_dev *pdev);
	int  (*resume_noirq)(struct pci_dev *pdev);
	void (*modem_status)(struct pci_dev *, int state);
	void (*update_status)(struct pci_dev *pdev, const struct pci_device_id *, int status);
	struct cnss_wlan_runtime_ops *runtime_ops;
	const struct pci_device_id *id_table;
	int  (*fatal)(struct pci_dev *pdev, const struct pci_device_id *id);
};

enum cnss_driver_status {
	CNSS_UNINITIALIZED,
	CNSS_INITIALIZED,
	CNSS_LOAD_UNLOAD,
	CNSS_RECOVERY,
	CNSS_FW_DOWN,
};

struct cnss_ce_tgt_pipe_cfg {
	u32 pipe_num;
	u32 pipe_dir;
	u32 nentries;
	u32 nbytes_max;
	u32 flags;
	u32 reserved;
};

struct cnss_ce_svc_pipe_cfg {
	u32 service_id;
	u32 pipe_dir;
	u32 pipe_num;
};

struct cnss_shadow_reg_cfg {
	u16 ce_id;
	u16 reg_offset;
};

struct cnss_shadow_reg_v2_cfg {
	u32 addr;
};

struct cnss_rri_over_ddr_cfg {
	u32 base_addr_low;
	u32 base_addr_high;
};

struct cnss_wlan_enable_cfg {
	u32 num_ce_tgt_cfg;
	struct cnss_ce_tgt_pipe_cfg *ce_tgt_cfg;
	u32 num_ce_svc_pipe_cfg;
	struct cnss_ce_svc_pipe_cfg *ce_svc_cfg;
	u32 num_shadow_reg_cfg;
	struct cnss_shadow_reg_cfg *shadow_reg_cfg;
	u32 num_shadow_reg_v2_cfg;
	struct cnss_shadow_reg_v2_cfg *shadow_reg_v2_cfg;
	bool rri_over_ddr_cfg_valid;
	struct cnss_rri_over_ddr_cfg rri_over_ddr_cfg;
};

enum cnss_driver_mode {
	CNSS_MISSION,
	CNSS_FTM,
	CNSS_EPPING,
	CNSS_WALTEST,
	CNSS_OFF,
	CNSS_CCPM,
	CNSS_QVIT,
	CNSS_CALIBRATION,
	CNSS_FTM_CALIBRATION = 10,
};

enum cnss_recovery_reason {
	CNSS_REASON_DEFAULT,
	CNSS_REASON_LINK_DOWN,
	CNSS_REASON_RDDM,
	CNSS_REASON_TIMEOUT,
	CNSS_REASON_FATAL_SHUTDOWN
};

enum cnss_crash_type {
	CNSS_NO_CRASH,
	CNSS_USERPD_CRASH,
	CNSS_ROOTPD_CRASH
};

struct cnss_mlo_chip_info {
	u32 hw_link_ids[CNSS_MAX_LINKS_PER_CHIP];
	u8 group_id;
	u8 soc_id;
	u8 chip_id;
	u8 num_local_links;
	u8 num_adj_chips;
	u8 adj_chip_ids[CNSS_MAX_LINKS_PER_CHIP];
	u8 valid_link_ids[CNSS_MAX_LINKS_PER_CHIP];
};

struct cnss_mlo_group_info {
	u8 group_id;
	u8 num_chips;
	u16 max_num_peers;
	u8 num_wsi_chips;
	u8 soc_chip_bitmap;
	u8 wsi_order_bitmap;
	u8 skip_soc_chip_bitmap;
	struct cnss_mlo_chip_info chip_info[CNSS_MAX_MLO_CHIPS];
	u16 rddm_dump_all;
};

struct cnss_module_param {
	u8 mlo_max_groups;
	u8 mlo_max_chips;
	bool mlo_default_cfg;
};

struct cnss_plat_data;

/* Function prototypes for CNSS2 APIs used from wifi driver
 * are defined here.
 * Please add Stubs also for any API added here to handle case
 * for targets that don't support CNSS2
 */
#if defined(CONFIG_ARCH_IPQ40XX) || defined(CONFIG_ARCH_IPQ806x)
static inline void cnss_wlan_unregister_driver(struct cnss_wlan_driver *driver)
{
}

static inline void cnss_device_crashed(struct device *dev)
{
}

static inline int cnss_pci_link_down(struct device *dev)
{
	return -EINVAL;
}

static inline void cnss_schedule_recovery(struct device *dev,
					  enum cnss_recovery_reason reason)
{
}

static inline int cnss_self_recovery(struct device *dev,
				     enum cnss_recovery_reason reason)
{
	return -EINVAL;
}

static inline int cnss_get_fw_files_for_target(struct device *dev,
					       struct cnss_fw_files *pfw_files,
					       u32 target_type,
					       u32 target_version)
{
	return -EINVAL;
}

static inline int cnss_get_platform_cap(struct device *dev,
					struct cnss_platform_cap *cap)
{
	return -EINVAL;
}

static inline int cnss_bus_get_soc_info(struct device *dev,
				    struct cnss_soc_info *info)
{
	return -EINVAL;
}

static inline int cnss_power_on_device(struct cnss_plat_data *plat_priv,
				       int device_id)
{
	return -EINVAL;
}

static inline int cnss_power_off_device(struct cnss_plat_data *plat_priv,
					int device_id)
{
	return -EINVAL;
}

static inline int cnss_wlan_pm_control(struct device *dev, bool vote)
{
	return -EINVAL;
}

static inline int cnss_bus_get_user_msi_assignment(struct device *dev,
					       char *user_name,
					       int *num_vectors,
					       uint32_t *user_base_data,
					       uint32_t *base_vector)
{
	return -EINVAL;
}

static inline int cnss_bus_get_msi_irq(struct device *dev, unsigned int vector)
{
	return -EINVAL;
}

static inline int cnss_get_pci_slot(struct device *dev)
{
	return 0;
}

static inline void cnss_bus_get_msi_address(struct device *dev,
					uint32_t *msi_addr_low,
					uint32_t *msi_addr_high)
{
}

static inline int cnss_wlan_enable(struct device *dev,
				   struct cnss_wlan_enable_cfg *config,
				   enum cnss_driver_mode mode,
				   const char *host_version)
{
	return 0;
}

static inline int cnss_wlan_disable(struct device *dev,
				    enum cnss_driver_mode mode)
{
	return 0;
}

static inline void cnss_wait_for_fw_ready(struct device *dev)
{
}

static inline void cnss_set_ramdump_enabled(struct device *dev, bool enabled)
{
}

static inline void cnss_set_recovery_enabled(struct device *dev, bool enabled)
{
}

static inline void *cnss_subsystem_get(struct device *dev, int device_id)
{
	return NULL;
}

static inline void cnss_subsystem_put(struct device *dev)
{
}

static inline int cnss_pcie_rescan(void)
{
	return -EINVAL;
}

static inline void cnss_pcie_remove_bus(void)
{
}

static inline void *cnss_get_pci_dev_by_device_id(int device_id)
{
	return NULL;
}

static inline void *cnss_get_pci_dev_from_plat_dev(void *pdev)
{
	return NULL;
}

static inline void *cnss_get_pci_dev_id_from_plat_dev(void *pdev)
{
	return NULL;
}

static inline int cnss_athdiag_read(struct device *dev, uint32_t offset,
				    uint32_t mem_type, uint32_t data_len,
				    uint8_t *output)
{
	return -EINVAL;
}

static inline int cnss_athdiag_write(struct device *dev, uint32_t offset,
				     uint32_t mem_type, uint32_t data_len,
				     uint8_t *input)
{
	return -EINVAL;
}

static inline bool cnss_is_dev_initialized(struct device *dev)
{
	return false;
}

static inline u64 cnss_bus_get_q6_time(struct device *dev)
{
	return 0;
}

static inline void cnss_dump_qmi_history(void)
{
}

static inline void cnss_get_ramdump_device_name(struct device *dev,
						char *ramdump_dev_name,
						size_t ramdump_dev_name_len)
{
}
static inline unsigned int cnss_get_driver_mode(void)
{
	return CNSS_MISSION;
}
static inline int cnss_set_driver_mode(unsigned int mode)
{
	return -EINVAL;
}
static inline bool cnss_get_global_mlo_support(void)
{
	return false;
}
static inline
int cnss_send_buffer_to_afcmem(struct device *dev, char *afcdb, uint32_t len,
			       uint8_t slotid)
{
	return -EINVAL;
}
static inline int cnss_reset_afcmem(struct device *dev, uint8_t slotid)
{
}
static inline int cnss_get_mlo_chip_id(struct device *dev)
{
	return -EINVAL;
}
static inline bool cnss_get_mlo_capable(struct device *dev)
{
	return false;
}
static inline bool cnss_is_mlo_default_cfg_enabled(struct device *dev)
{
	return false;
}
static inline int cnss_get_mlo_global_config_region_info(struct device *dev,
							 void **bar,
							 int *num_bytes)
{
	return 0;
}
static inline int cnss_get_num_mlo_links(struct device *dev)
{
	return -EINVAL;
}
static inline int cnss_get_mlo_chip_info(struct device *dev,
					 struct cnss_mlo_chip_info **chip_info)
{
	return -EINVAL;
}
static inline int cnss_get_num_mlo_capable_devices(unsigned int *device_id,
						   int num_elements)
{
	return -EINVAL;
}
static inline int cnss_get_max_mlo_chips(struct device *dev)
{
	return -EINVAL;
}
static inline int cnss_get_dev_link_ids(struct device *dev, u8 *link_ids,
					int max_elements)
{
	return -EINVAL;
}
static inline int cnss_bus_reg_read(struct device *dev, u32 addr, u32 *val,
				void __iomem *base)
{
	return -EINVAL;
}
static inline int cnss_bus_reg_write(struct device *dev, u32 addr, u32 val,
				 void __iomem *base)
{
	return -EINVAL;
}
static inline int cnss_wlan_register_driver_ops(struct cnss_wlan_driver *driver)
{
	return 0;
}
static inline int cnss_wlan_probe_driver(void)
{
	return 0;
}
static inline int cnss_set_bar_addr(struct device *dev, void __iomem *mem)
{
	return -EINVAL;
}
static inline int cnss_set_mlo_config(struct cnss_module_param *modparam,
				      struct cnss_mlo_group_info *group_info)
{
	return 0;
}
static
inline int cnss_set_mlo_group_config(struct cnss_mlo_group_info *src_mlo_config,
				     uint8_t group_id)
{
	return 0;
}
static inline void cnss_set_default_mlo_config(void)
{
}
static inline int cnss_reset_mlo_config(uint32_t group_id)
{
	return 0;
}
static inline void cnss_print_mlo_config(void)
{
}
static inline void cnss_set_led_gpio(int led_gpio, unsigned int value,
				     unsigned int flags)
{
}
static bool cnss_get_enable_intx(struct device *dev)
{
	return false;
}
static int cnss_get_num_mlo_groups(void)
{
	return 0;
}
static bool cnss_get_mlo_group_info(uint8_t grp_id,
			struct cnss_mlo_group_info *grp_info)
{
	return false;
}
static inline int cnss_get_mlo_group_id(struct device *dev)
{
	return -EINVAL;
}
static inline int cnss_set_wsi_remap(struct device *dev)
{
	return -EINVAL;
}
static inline void cnss_set_recovery_mode(struct device *dev, u8 recovery_mode)
{
	return -EINVAL;
}
static inline void cnss_set_standby_mode(struct device *dev, u8 standby_mode)
{
	return -EINVAL;
}
static inline void cnss_set_wsi_remap_state(struct device *dev, bool state)
{
}
static inline void cnss_set_pci_link_speed_width(struct device *dev,
						u16 link_speed, u16 link_width);
{
	return -EINVAL;
}
#else
extern int cnss_wlan_register_driver_ops(struct cnss_wlan_driver *driver);
extern int cnss_wlan_probe_driver(void);
extern void cnss_wlan_unregister_driver(struct cnss_wlan_driver *driver);
extern void cnss_device_crashed(struct device *dev);
extern int cnss_pci_link_down(struct device *dev);
extern int cnss_pci_is_device_down(struct device *dev);
extern void cnss_schedule_recovery(struct device *dev,
				   enum cnss_recovery_reason reason);
extern int cnss_self_recovery(struct device *dev,
			      enum cnss_recovery_reason reason);
extern int cnss_force_fw_assert(struct device *dev);
extern int cnss_force_collect_rddm(struct device *dev);
extern void *cnss_get_virt_ramdump_mem(struct device *dev, unsigned long *size);
extern int cnss_get_fw_files_for_target(struct device *dev,
					struct cnss_fw_files *pfw_files,
					u32 target_type, u32 target_version);
extern int cnss_get_platform_cap(struct device *dev,
				 struct cnss_platform_cap *cap);
#ifdef CONFIG_CNSS2_SMMU
extern struct iommu_domain *cnss_smmu_get_domain(struct device *dev);
extern int cnss_smmu_map(struct device *dev,
			 phys_addr_t paddr, uint32_t *iova_addr, size_t size);
#ifdef CONFIG_TARGET_SDX75
extern int cnss_smmu_unmap(struct device *dev, uint32_t iova_addr, size_t size);
#endif
#endif
extern int cnss_bus_get_soc_info(struct device *dev, struct cnss_soc_info *info);
extern int cnss_request_bus_bandwidth(struct device *dev, int bandwidth);
struct cnss_plat_data;
extern int cnss_power_on_device(struct cnss_plat_data *plat_priv,
				int device_id);
extern int cnss_power_off_device(struct cnss_plat_data *plat_priv,
				 int device_id);
extern int cnss_power_up(struct device *dev);
extern int cnss_power_down(struct device *dev);
extern int cnss_idle_restart(struct device *dev);
extern int cnss_idle_shutdown(struct device *dev);
#ifndef CONFIG_CNSS2_KERNEL_5_15
extern void cnss_request_pm_qos(struct device *dev, u32 qos_val);
extern void cnss_remove_pm_qos(struct device *dev);
#endif
extern void cnss_lock_pm_sem(struct device *dev);
extern void cnss_release_pm_sem(struct device *dev);
extern int cnss_wlan_pm_control(struct device *dev, bool vote);
extern int cnss_auto_suspend(struct device *dev);
extern int cnss_auto_resume(struct device *dev);
extern int cnss_pci_is_drv_connected(struct device *dev);
extern int cnss_pci_force_wake_request(struct device *dev);
extern int cnss_pci_is_device_awake(struct device *dev);
extern int cnss_pci_force_wake_release(struct device *dev);
extern int cnss_bus_get_user_msi_assignment(struct device *dev, char *user_name,
					int *num_vectors,
					uint32_t *user_base_data,
					uint32_t *base_vector);
extern int cnss_bus_get_msi_irq(struct device *dev, unsigned int vector);
extern int cnss_get_pci_slot(struct device *dev);
extern void cnss_bus_get_msi_address(struct device *dev, uint32_t *msi_addr_low,
				 uint32_t *msi_addr_high);
extern int cnss_wlan_enable(struct device *dev,
			    struct cnss_wlan_enable_cfg *config,
			    enum cnss_driver_mode mode,
			    const char *host_version);
extern int cnss_wlan_disable(struct device *dev, enum cnss_driver_mode mode);
extern unsigned int cnss_get_boot_timeout(struct device *dev);
void cnss_wait_for_fw_ready(struct device *dev);
void cnss_set_ramdump_enabled(struct device *dev, bool enabled);
void cnss_set_recovery_enabled(struct device *dev, bool enabled);
void *cnss_subsystem_get(struct device *dev, int device_id);
void cnss_subsystem_put(struct device *dev);
int cnss_pcie_rescan(void);
void cnss_pcie_remove_bus(void);
void *cnss_get_pci_dev_by_device_id(int device_id);
void *cnss_get_pci_dev_from_plat_dev(void *pdev);
void *cnss_get_pci_dev_id_from_plat_dev(void *pdev);
int cnss_dump_all_ce_reg(struct cnss_plat_data *plat_priv);
extern unsigned int cnss_get_qmi_timeout(struct cnss_plat_data *plat_priv);
extern int cnss_athdiag_read(struct device *dev, uint32_t offset,
			     uint32_t mem_type, uint32_t data_len,
			     uint8_t *output);
extern int cnss_athdiag_write(struct device *dev, uint32_t offset,
			      uint32_t mem_type, uint32_t data_len,
			      uint8_t *input);
bool cnss_is_dev_initialized(struct device *dev);
u64 cnss_bus_get_q6_time(struct device *dev);
extern void cnss_dump_qmi_history(void);
void cnss_get_ramdump_device_name(struct device *dev,
				  char *ramdump_dev_name,
				  size_t ramdump_dev_name_len);
unsigned int cnss_get_driver_mode(void);
int cnss_set_driver_mode(unsigned int mode);
bool cnss_get_global_mlo_support(void);
int cnss_send_buffer_to_afcmem(struct device *dev, char *afcdb, uint32_t len,
			    uint8_t slotid);
int cnss_reset_afcmem(struct device *dev, uint8_t slotid);
int cnss_get_mlo_chip_id(struct device *dev);
bool cnss_get_mlo_capable(struct device *dev);
bool cnss_is_mlo_default_cfg_enabled(struct device *dev);
int cnss_get_mlo_global_config_region_info(struct device *dev, void **bar,
					   int *num_bytes);
int cnss_get_num_mlo_links(struct device *dev);
int cnss_get_mlo_chip_info(struct device *dev,
			   struct cnss_mlo_chip_info **chip_info);
int cnss_get_num_mlo_capable_devices(unsigned int *device_id,
				     int num_elements);
int cnss_get_max_mlo_chips(struct device *dev);
int cnss_get_dev_link_ids(struct device *dev, u8 *link_ids, int max_elements);
int cnss_bus_reg_read(struct device *dev, u32 addr, u32 *val,
					void __iomem *base);
int cnss_bus_reg_write(struct device *dev, u32 addr, u32 val,
					void __iomem *base);
int cnss_set_bar_addr(struct device *dev, void __iomem *mem);
int cnss_set_mlo_config(struct cnss_module_param *modparam,
			struct cnss_mlo_group_info *group_info);
int cnss_set_mlo_group_config(struct cnss_mlo_group_info *src_mlo_config,
			      uint8_t group_id);
void cnss_set_default_mlo_config(void);
int cnss_reset_mlo_config(uint32_t group_id);
void cnss_print_mlo_config(void);
void cnss_set_led_gpio(int led_gpio, unsigned int value, unsigned int flags);
bool cnss_get_enable_intx(struct device *dev);
void *cnss_get_plat_dev_by_bus_dev(struct device *dev);
int cnss_get_num_mlo_groups(void);
bool cnss_get_mlo_group_info(uint8_t grp_id,
			struct cnss_mlo_group_info *grp_info);
int cnss_get_mlo_group_id(struct device *dev);
void cnss_set_recovery_mode(struct device *dev, u8 recovery_mode);
void cnss_set_standby_mode(struct device *dev, u8 standby_mode);
int cnss_set_wsi_remap(struct device *dev);
void cnss_set_wsi_remap_state(struct device *dev, bool state);
void cnss_set_pci_link_speed_width(struct device *dev, u16 link_speed,
					u16 link_width);
#endif
#endif /* _NET_CNSS2_H */
