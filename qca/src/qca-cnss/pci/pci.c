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

#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/completion.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#ifdef CONFIG_CNSS2_DMA_ALLOC
#include <linux/cma.h>
#endif
#ifdef CONFIG_CNSS2_KERNEL_IPQ
#include <asm/cacheflush.h>
#endif
#ifdef KERNEL_SUPPORTS_QGIC2M
#include <soc/qcom/qgic2m.h>
#endif

#include "../main.h"
#include "cnss_common/cnss_common.h"
#include "debug/debug.h"
#include "pci/pci.h"
#include "bus/bus.h"
#include "legacyirq/legacyirq.h"
#if (KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE)
#include <linux/devcoredump.h>
#include <linux/elf.h>
#else
#include <soc/qcom/ramdump.h>
#endif

/* WINDOW0 address ranges are PCIE_PCIE_LOCAL_REG amd PCIE_MHI_REG */
#define PCIE_LOCAL_REG_BASE     0x1E00000
#define PCIE_LOCAL_REG_END	     0x1E03FFF

#define PCI_LINK_UP			1
#define PCI_LINK_DOWN			0

#define SAVE_PCI_CONFIG_SPACE		1
#define RESTORE_PCI_CONFIG_SPACE	0

#define PM_OPTIONS_DEFAULT		0
#define PM_OPTIONS_LINK_DOWN \
	(MSM_PCIE_CONFIG_NO_CFG_RESTORE | MSM_PCIE_CONFIG_LINKDOWN)

#define PCI_DMA_MASK_32_BIT		DMA_BIT_MASK(32)
#define PCI_DMA_MASK_36_BIT		DMA_BIT_MASK(36)
#define PCI_DMA_MASK_64_BIT		DMA_BIT_MASK(64)

#define MHI_NODE_NAME			"qcom,mhi"
#define MHI_MSI_NAME			"MHI"
#define QDSS_MSI_NAME			"QDSS"

#define WAKE_MSI_NAME			"WAKE"

#define FW_ASSERT_TIMEOUT		5000
#define DEV_RDDM_TIMEOUT		5000

#ifdef CONFIG_CNSS_EMULATION
#define EMULATION_HW			1
#else
#define EMULATION_HW			0
#endif
bool cnss_pci_registered;
static DEFINE_SPINLOCK(pci_link_down_lock);
#ifdef CONFIG_PCIE_QCOM
extern int qcom_pcie_rescan(void);
extern void qcom_pcie_remove_bus(void);
#endif

extern int timeout_factor;
static DEFINE_SPINLOCK(pci_reg_window_lock);
static DEFINE_SPINLOCK(qdss_lock);

#define MHI_TIMEOUT_OVERWRITE_MS	(plat_priv->ctrl_params.mhi_timeout)

#define MHISTATUS				0x48
#define MHICTRL					0x38
#define MHICTRL_RESET_MASK			0x2

#define FORCE_WAKE_DELAY_MIN_US			4000
#define FORCE_WAKE_DELAY_MAX_US			6000
#define FORCE_WAKE_DELAY_TIMEOUT_US		60000

#define QCA6390_TIME_SYNC_ENABLE		0x80000000
#define QCA6390_TIME_SYNC_CLEAR			0x0

#define QCN9000_TIME_SYNC_ENABLE		0x80000000
#define QCN9000_TIME_SYNC_CLEAR			0x0
#define MAX_RAMDUMP_TRANSFER_WAIT_CNT		50 /* x 20msec */
#define MAX_SOC_GLOBAL_RESET_WAIT_CNT		50 /* x 20msec */
#define BHI_ERRDBG1 (0x34)
#define BHI_ERRDBG3 (0x3C)

#define DEVICE_RDDM_COOKIE			0xCAFECACE

#define MHI_RAMDUMP_DUMP_COMPLETE		0x5000

#define PCIE_PCIE_LOCAL_REG_PCIE_LOCAL_RSV0	0x1E03164
#define QRTR_NODE_ID_REG_MASK			0x7FFFF
#define QRTR_NODE_ID_REG		PCIE_PCIE_LOCAL_REG_PCIE_LOCAL_RSV0

/* Timeout, to print boot debug logs, in seconds */
static int boot_debug_timeout = 7;
module_param(boot_debug_timeout, int, 0644);
MODULE_PARM_DESC(boot_debug_timeout, "boot debug logs timeout in seconds");
#define BOOT_DEBUG_TIMEOUT_MS			(boot_debug_timeout * 1000)

static struct cnss_reg_offset qdss_csr[] = {
	{ "QDSSCSR_ETRIRQCTRL", QDSS_APB_DEC_CSR_ETRIRQCTRL_OFFSET },
	{ "QDSSCSR_PRESERVEETF", QDSS_APB_DEC_CSR_PRESERVEETF_OFFSET },
	{ "QDSSCSR_PRESERVEETR0", QDSS_APB_DEC_CSR_PRESERVEETR0_OFFSET },
	{ "QDSSCSR_PRESERVEETR1", QDSS_APB_DEC_CSR_PRESERVEETR1_OFFSET },
	{ NULL },
};

#if defined(CONFIG_CNSS2_KERNEL_IPQ) || defined(CONFIG_CNSS2_KERNEL_5_15)
static struct mhi_channel_config cnss_pci_mhi_channels[] = {
	{
		.num = 0,
		.name = "LOOPBACK",
		.num_elements = 32,
		.event_ring = 1,
		.dir = DMA_TO_DEVICE,
		.ee_mask = 0x4,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
#if (KERNEL_VERSION(5, 11, 0) > LINUX_VERSION_CODE)
		.auto_start = false,
#endif
	},
	{
		.num = 1,
		.name = "LOOPBACK",
		.num_elements = 32,
		.event_ring = 1,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = 0x4,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
#if (KERNEL_VERSION(5, 11, 0) > LINUX_VERSION_CODE)
		.auto_start = false,
#endif
	},
	{
		.num = 4,
		.name = "DIAG",
		.num_elements = 32,
		.event_ring = 1,
		.dir = DMA_TO_DEVICE,
		.ee_mask = 0x4,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
#if (KERNEL_VERSION(5, 11, 0) > LINUX_VERSION_CODE)
		.auto_start = false,
#endif
	},
	{
		.num = 5,
		.name = "DIAG",
		.num_elements = 32,
		.event_ring = 1,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = 0x4,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
#if (KERNEL_VERSION(5, 11, 0) > LINUX_VERSION_CODE)
		.auto_start = false,
#endif
	},
	{
		.num = 20,
		.name = "IPCR",
#ifdef CONFIG_TARGET_SDX75
		.num_elements = 64,
#else
		.num_elements = 32,
#endif
		.event_ring = 1,
		.dir = DMA_TO_DEVICE,
		.ee_mask = 0x4,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
#if (KERNEL_VERSION(5, 11, 0) > LINUX_VERSION_CODE)
		.auto_start = true,
#endif
	},
	{
		.num = 21,
		.name = "IPCR",
#ifdef CONFIG_TARGET_SDX75
		.num_elements = 64,
#else
		.num_elements = 32,
#endif
		.event_ring = 1,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = 0x4,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = true,
#if (KERNEL_VERSION(5, 11, 0) > LINUX_VERSION_CODE)
		.auto_start = true,
#endif
	},
};

static struct mhi_event_config cnss_pci_mhi_events[] = {
	{
		.num_elements = 32,
		.irq_moderation_ms = 0,
		.irq = 1,
		.mode = MHI_DB_BRST_DISABLE,
		.priority = 1,
		.data_type = MHI_ER_CTRL,
		.hardware_event = false,
		.client_managed = false,
		.offload_channel = false,
	},
	{
		.num_elements = 256,
		.irq_moderation_ms = 1,
		.irq = 1,
		.mode = MHI_DB_BRST_DISABLE,
		.priority = 1,
		.hardware_event = false,
		.client_managed = false,
		.offload_channel = false,
	},
};

static struct mhi_controller_config cnss_pci_mhi_config = {
	.max_channels = 30,
	.timeout_ms = 10000,
	.use_bounce_buf = false,
	.buf_len = 0,
	.num_channels = ARRAY_SIZE(cnss_pci_mhi_channels),
	.ch_cfg = cnss_pci_mhi_channels,
	.num_events = ARRAY_SIZE(cnss_pci_mhi_events),
	.event_cfg = cnss_pci_mhi_events,
#ifdef CONFIG_TARGET_SDX75
	.rddm_timeout_us = 400000,
#endif
};
#endif

#if defined(CONFIG_CNSS2_KERNEL_5_15) && !IS_ENABLED(CONFIG_MHI_BUS_MISC)
static void cnss_mhi_debug_reg_dump(struct cnss_pci_data *pci_priv)
{
}

static bool cnss_mhi_scan_rddm_cookie(struct cnss_pci_data *pci_priv,
				      u32 cookie)
{
	return false;
}
#else
static void cnss_mhi_debug_reg_dump(struct cnss_pci_data *pci_priv)
{
	mhi_debug_reg_dump(pci_priv->mhi_ctrl);
}

static bool cnss_mhi_scan_rddm_cookie(struct cnss_pci_data *pci_priv,
				      u32 cookie)
{
	return mhi_scan_rddm_cookie(pci_priv->mhi_ctrl, cookie);
}
#endif


static int cnss_pci_check_link_status(struct cnss_pci_data *pci_priv)
{
#ifdef CONFIG_PCI_SUSPENDRESUME
	u16 device_id;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	if (pci_priv->pci_link_state == PCI_LINK_DOWN) {
		cnss_pr_dbg("PCIe link is suspended\n");
		return -EIO;
	}

	if (pci_priv->pci_link_down_ind) {
		cnss_pr_err("PCIe link is down\n");
		return -EIO;
	}

	pci_read_config_word(pci_priv->pci_dev, PCI_DEVICE_ID, &device_id);
	if (device_id != pci_priv->device_id)  {
		cnss_fatal_err("PCI device ID mismatch, link possibly down, current read ID: 0x%x, record ID: 0x%x\n",
			       device_id, pci_priv->device_id);
		return -EIO;
	}
#endif

	return 0;
}

static int cnss_pci_get_bar_addr(struct cnss_plat_data *plat_priv,
				 void __iomem **bar)
{
	struct cnss_pci_data *pci_priv = NULL;

	if (!plat_priv) {
		cnss_pr_err("%s: Plat Priv is null\n", __func__);
		return -ENODEV;
	}

	switch (plat_priv->bus_type) {
	case CNSS_BUS_AHB:
		if (plat_priv->device_id != QCN6122_DEVICE_ID &&
		    plat_priv->device_id != QCN9160_DEVICE_ID)
			return -EINVAL;

		if (!plat_priv->bar) {
			cnss_pr_err("%s: AHB bar is not yet assigned\n",
				    __func__);
			return -EINVAL;
		}
		*bar = plat_priv->bar;
		break;
	case CNSS_BUS_PCI:
		if (!plat_priv->bus_priv) {
			cnss_pr_err("PCI Priv is NULL\n");
			return -ENODEV;
		}

		pci_priv = plat_priv->bus_priv;
		if (!pci_priv->bar) {
			cnss_pr_err("PCI bar is not yet assigned\n");
			return -EINVAL;
		}
		*bar = pci_priv->bar;
		break;
	default:
		cnss_pr_err("Unsupported device id 0x%lx\n",
			    plat_priv->device_id);
		return -ENODEV;
	}

	return 0;
}

static void cnss_pci_select_window(struct cnss_plat_data *plat_priv, u32 addr)
{
	u32 window = (addr >> WINDOW_SHIFT) & WINDOW_VALUE_MASK;
	u32 prev_window = 0, curr_window = 0, prev_cleared_window = 0;
	volatile u32 write_val, read_val = 0;
	int retry = 0;
	void __iomem *bar = NULL;

	if (cnss_pci_get_bar_addr(plat_priv, &bar) < 0) {
		cnss_pr_err("%s: Get bar address failed\n", __func__);
		return;
	}


	prev_window = readl_relaxed(bar +
					QCN9000_PCIE_REMAP_BAR_CTRL_OFFSET);

	/* Clear out last 6 bits of window register */
	prev_cleared_window = prev_window & ~(0x3f);

	/* Write the new last 6 bits of window register. Only window 1 values
	 * are changed. Window 2 and 3 are unaffected.
	 */
	curr_window = prev_cleared_window | window;

	/* Skip writing into window register if the read value
	 * is same as calculated value.
	 */
	if (curr_window == prev_window)
		return;

	write_val = WINDOW_ENABLE_BIT | curr_window;
	writel_relaxed(write_val, bar +
		       QCN9000_PCIE_REMAP_BAR_CTRL_OFFSET);

	read_val = readl_relaxed(bar +
				 QCN9000_PCIE_REMAP_BAR_CTRL_OFFSET);

	/* If value written is not yet reflected, wait till it is reflected */
	while ((read_val != write_val) && (retry < 10)) {
		mdelay(1);
		read_val = readl_relaxed(bar +
					 QCN9000_PCIE_REMAP_BAR_CTRL_OFFSET);
		retry++;
	}

	if ((retry >= 10) && (read_val != write_val))
		cnss_pr_dbg("%s: retry count: %d", __func__, retry);
}

int cnss_pci_reg_read(struct cnss_plat_data *plat_priv,
		      u32 addr, u32 *val)
{
	int ret;
	u32 mhi_region_start_reg = 0;
	u32 mhi_region_end_reg = 0;
	unsigned long flags;
	struct cnss_pci_data *pci_priv = NULL;
	void __iomem *bar = NULL;

	if (!plat_priv) {
		cnss_pr_err("%s: Plat Priv is null\n", __func__);
		return -ENODEV;
	}

	ret = cnss_pci_get_bar_addr(plat_priv, &bar);
	if (ret < 0) {
		cnss_pr_err("%s: Get bar address failed\n", __func__);
		return ret;
	}

	if (plat_priv->bus_type == CNSS_BUS_PCI) {
		if (!plat_priv->bus_priv) {
			cnss_pr_err("PCI Priv is NULL\n");
			return -ENODEV;
		}
		pci_priv = plat_priv->bus_priv;
		ret = cnss_pci_check_link_status(pci_priv);
		if (ret)
			return ret;

		if (pci_priv->pci_dev->device == QCA6174_DEVICE_ID ||
		    addr < MAX_UNWINDOWED_ADDRESS) {
			*val = readl_relaxed(bar + addr);
			return 0;
		}
	}

	ret = cnss_get_mhi_region_len(plat_priv, &mhi_region_start_reg,
				      &mhi_region_end_reg);
	if (ret) {
		cnss_pr_err("MHI start and end region not assigned.\n");
		return ret;
	}

	spin_lock_irqsave(&pci_reg_window_lock, flags);
	cnss_pci_select_window(plat_priv, addr);

	if ((addr >= PCIE_LOCAL_REG_BASE && addr <= PCIE_LOCAL_REG_END) ||
		(addr >= mhi_region_start_reg && addr <= mhi_region_end_reg)) {
		if (addr >= mhi_region_start_reg && addr <= mhi_region_end_reg)
			addr = addr - mhi_region_start_reg;

		*val = readl_relaxed(bar +
				     (addr & WINDOW_RANGE_MASK));
	} else {
		*val = readl_relaxed(bar + WINDOW_START +
				     (addr & WINDOW_RANGE_MASK));
	}
	spin_unlock_irqrestore(&pci_reg_window_lock, flags);

	return 0;
}

static int cnss_pci_reg_write(struct cnss_pci_data *pci_priv, u32 addr,
			      u32 val)
{
	int ret;
	u32 mhi_region_start_reg = 0;
	u32 mhi_region_end_reg = 0;
	unsigned long flags;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	ret = cnss_pci_check_link_status(pci_priv);
	if (ret)
		return ret;

	if (!pci_priv->bar) {
		cnss_pr_err("PCI bar is not yet assigned\n");
		return 0;
	}

	if (pci_priv->pci_dev->device == QCA6174_DEVICE_ID ||
	    addr < MAX_UNWINDOWED_ADDRESS) {
		writel_relaxed(val, pci_priv->bar + addr);
		return 0;
	}

	ret = cnss_get_mhi_region_len(plat_priv, &mhi_region_start_reg,
				      &mhi_region_end_reg);
	if (ret) {
		cnss_pr_err("MHI start and end region not assigned.\n");
		return ret;
	}

	spin_lock_irqsave(&pci_reg_window_lock, flags);
	cnss_pci_select_window(plat_priv, addr);

	if ((addr >= PCIE_LOCAL_REG_BASE && addr <= PCIE_LOCAL_REG_END) ||
		(addr >= mhi_region_start_reg && addr <= mhi_region_end_reg)) {
		if (addr >= mhi_region_start_reg && addr <= mhi_region_end_reg)
			addr = addr - mhi_region_start_reg;

		writel_relaxed(val, pci_priv->bar +
			       (addr & WINDOW_RANGE_MASK));
	} else {
		writel_relaxed(val, pci_priv->bar + WINDOW_START +
			       (addr & WINDOW_RANGE_MASK));
	}
	spin_unlock_irqrestore(&pci_reg_window_lock, flags);

	return 0;
}

static int cnss_pci_force_wake_get(struct cnss_pci_data *pci_priv)
{
	struct device *dev = &pci_priv->pci_dev->dev;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	u32 timeout = 0;
	int ret;

	ret = cnss_pci_force_wake_request(dev);
	if (ret) {
		cnss_pr_err("Failed to request force wake\n");
		return ret;
	}

	while (!cnss_pci_is_device_awake(dev) &&
	       timeout <= FORCE_WAKE_DELAY_TIMEOUT_US) {
		usleep_range(FORCE_WAKE_DELAY_MIN_US, FORCE_WAKE_DELAY_MAX_US);
		timeout += FORCE_WAKE_DELAY_MAX_US;
	}

	if (cnss_pci_is_device_awake(dev) != true) {
		cnss_pr_err("Timed out to request force wake\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int cnss_pci_force_wake_put(struct cnss_pci_data *pci_priv)
{
	struct device *dev = &pci_priv->pci_dev->dev;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	int ret;

	ret = cnss_pci_force_wake_release(dev);
	if (ret)
		cnss_pr_err("Failed to release force wake\n");

	return ret;
}
static unsigned int pci_link_down_panic;
module_param(pci_link_down_panic, uint, 0600);
MODULE_PARM_DESC(pci_link_down_panic,
		 "Trigger kernel panic when PCI link down is detected");

struct paging_header hdr;

int cnss_pcie_rescan(void)
{
	return -EINVAL;
}
EXPORT_SYMBOL(cnss_pcie_rescan);

void cnss_pcie_remove_bus(void)
{
}
EXPORT_SYMBOL(cnss_pcie_remove_bus);

#ifdef CONFIG_PCI_SUSPENDRESUME
static int cnss_set_pci_config_space(struct cnss_pci_data *pci_priv, bool save)
{
	struct pci_dev *pci_dev = pci_priv->pci_dev;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	bool link_down_or_recovery;

	if (!plat_priv)
		return -ENODEV;

	link_down_or_recovery = pci_priv->pci_link_down_ind ||
		(test_bit(CNSS_DRIVER_RECOVERY, &plat_priv->driver_state));

	if (save) {
		if (link_down_or_recovery) {
			pci_priv->saved_state = NULL;
		} else {
			pci_save_state(pci_dev);
			pci_priv->saved_state = pci_store_saved_state(pci_dev);
		}
	} else {
		if (link_down_or_recovery) {
			pci_load_saved_state(pci_dev, pci_priv->default_state);
			pci_restore_state(pci_dev);
		} else if (pci_priv->saved_state) {
			pci_load_and_free_saved_state(pci_dev,
						      &pci_priv->saved_state);
			pci_restore_state(pci_dev);
		}
	}

	return 0;
}
#endif

static int cnss_pci_get_link_status(struct cnss_pci_data *pci_priv)
{
	u16 link_status;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	int ret;

	ret = pcie_capability_read_word(pci_priv->pci_dev, PCI_EXP_LNKSTA,
					&link_status);
	if (ret)
		return ret;

	cnss_pr_dbg("Get PCI link status register: %u\n", link_status);

	pci_priv->def_link_speed = link_status & PCI_EXP_LNKSTA_CLS;
	pci_priv->def_link_width =
		(link_status & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT;

	cnss_pr_dbg("Default PCI link speed is 0x%x, link width is 0x%x\n",
		    pci_priv->def_link_speed, pci_priv->def_link_width);

	return 0;
}

#ifdef CONFIG_CNSS2_PCI_MSM
static int cnss_set_pci_link_status(struct cnss_pci_data *pci_priv,
				    enum pci_link_status status)
{
	u16 link_speed, link_width;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	cnss_pr_dbg("Set PCI link status to: %u\n", status);

	switch (status) {
	case PCI_GEN1:
		link_speed = PCI_EXP_LNKSTA_CLS_2_5GB;
		link_width = PCI_EXP_LNKSTA_NLW_X1 >> PCI_EXP_LNKSTA_NLW_SHIFT;
		break;
	case PCI_GEN2:
		link_speed = PCI_EXP_LNKSTA_CLS_5_0GB;
		link_width = PCI_EXP_LNKSTA_NLW_X1 >> PCI_EXP_LNKSTA_NLW_SHIFT;
		break;
	case PCI_DEF:
		link_speed = pci_priv->def_link_speed;
		link_width = pci_priv->def_link_width;
		if (!link_speed && !link_width) {
			cnss_pr_err("PCI link speed or width is not valid\n");
			return -EINVAL;
		}
		break;
	default:
		cnss_pr_err("Unknown PCI link status config: %u\n", status);
		return -EINVAL;
	}

	return msm_pcie_set_link_bandwidth(pci_priv->pci_dev,
					   link_speed, link_width);
}
#endif

#ifdef CONFIG_PCI_SUSPENDRESUME
static int cnss_set_pci_link(struct cnss_pci_data *pci_priv, bool link_up)
{
#ifdef CONFIG_CNSS2_PCI_MSM
	int ret = 0;
	struct pci_dev *pci_dev = pci_priv->pci_dev;
	enum msm_pcie_pm_opt pm_ops;

	cnss_pr_vdbg("%s PCI link\n", link_up ? "Resuming" : "Suspending");

	if (link_up) {
		pm_ops = MSM_PCIE_RESUME;
	} else {
		if (pci_priv->drv_connected_last) {
			cnss_pr_vdbg("Use PCIe DRV suspend\n");
			pm_ops = MSM_PCIE_DRV_SUSPEND;
			cnss_set_pci_link_status(pci_priv, PCI_GEN1);
		} else {
			pm_ops = MSM_PCIE_SUSPEND;
		}
	}

	ret = msm_pcie_pm_control(pm_ops, pci_dev->bus->number, pci_dev,
				  NULL, PM_OPTIONS_DEFAULT);
	if (ret)
		cnss_pr_err("Failed to %s PCI link with default option, err = %d\n",
			    link_up ? "resume" : "suspend", ret);

	if (pci_priv->drv_connected_last) {
		if ((link_up && !ret) || (!link_up && ret))
			cnss_set_pci_link_status(pci_priv, PCI_DEF);
	}
#endif

	return 0;
}
#endif

int cnss_suspend_pci_link(struct cnss_pci_data *pci_priv)
{
#ifdef CONFIG_PCI_SUSPENDRESUME
	int ret = 0;

	if (!pci_priv)
		return -ENODEV;

	if (pci_priv->pci_link_state == PCI_LINK_DOWN) {
		cnss_pr_info("PCI link is already suspended\n");
		goto out;
	}

	pci_clear_master(pci_priv->pci_dev);

	ret = cnss_set_pci_config_space(pci_priv, SAVE_PCI_CONFIG_SPACE);
	if (ret)
		goto out;

	pci_disable_device(pci_priv->pci_dev);

	if (pci_priv->pci_dev->device != QCA6174_DEVICE_ID) {
		if (pci_set_power_state(pci_priv->pci_dev, PCI_D3hot))
			cnss_pr_err("Failed to set D3Hot, err =  %d\n", ret);
	}

	ret = cnss_set_pci_link(pci_priv, PCI_LINK_DOWN);
	if (ret)
		goto out;

	pci_priv->pci_link_state = PCI_LINK_DOWN;

	return 0;
out:
	return ret;
#endif
	return 0;
}

int cnss_resume_pci_link(struct cnss_pci_data *pci_priv)
{
#ifdef CONFIG_PCI_SUSPENDRESUME
	int ret = 0;

	if (!pci_priv)
		return -ENODEV;

	if (pci_priv->pci_link_state == PCI_LINK_UP) {
		cnss_pr_info("PCI link is already resumed\n");
		goto out;
	}

	ret = cnss_set_pci_link(pci_priv, PCI_LINK_UP);
	if (ret)
		goto out;

	pci_priv->pci_link_state = PCI_LINK_UP;

	if (pci_priv->pci_dev->device != QCA6174_DEVICE_ID) {
		ret = pci_set_power_state(pci_priv->pci_dev, PCI_D0);
		if (ret) {
			cnss_pr_err("Failed to set D0, err = %d\n", ret);
			goto out;
		}
	}

	ret = pci_enable_device(pci_priv->pci_dev);
	if (ret) {
		cnss_pr_err("Failed to enable PCI device, err = %d\n", ret);
		goto out;
	}

	ret = cnss_set_pci_config_space(pci_priv, RESTORE_PCI_CONFIG_SPACE);
	if (ret)
		goto out;

	pci_set_master(pci_priv->pci_dev);

	if (pci_priv->pci_link_down_ind)
		pci_priv->pci_link_down_ind = false;

	return 0;
out:
	return ret;
#endif
	return 0;
}

int cnss_pci_link_down(struct device *dev)
{
	unsigned long flags;
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	struct cnss_plat_data *plat_priv = NULL;

	if (!pci_priv) {
		cnss_pr_err("%s: pci_priv is NULL\n", __func__);
		return -EINVAL;
	}

	plat_priv = pci_priv->plat_priv;
	if (test_bit(ENABLE_PCI_LINK_DOWN_PANIC,
		     &plat_priv->ctrl_params.quirks))
		panic("cnss: PCI link is down\n");

	spin_lock_irqsave(&pci_link_down_lock, flags);
	if (pci_priv->pci_link_down_ind) {
		cnss_pr_dbg("PCI link down recovery is in progress, ignore\n");
		spin_unlock_irqrestore(&pci_link_down_lock, flags);
		return -EINVAL;
	}
	pci_priv->pci_link_down_ind = true;
	spin_unlock_irqrestore(&pci_link_down_lock, flags);

	cnss_pr_err("PCI link down is detected, schedule recovery\n");

	cnss_schedule_recovery(dev, CNSS_REASON_LINK_DOWN);

	return 0;
}
EXPORT_SYMBOL(cnss_pci_link_down);

int cnss_pci_is_device_down(struct device *dev)
{
	struct cnss_plat_data *plat_priv = cnss_bus_dev_to_plat_priv(dev);
	struct cnss_pci_data *pci_priv;

	if (!plat_priv) {
		cnss_pr_err("plat_priv is NULL\n");
		return -ENODEV;
	}

	pci_priv = plat_priv->bus_priv;
	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}

	return test_bit(CNSS_DEV_ERR_NOTIFY, &plat_priv->driver_state) |
		pci_priv->pci_link_down_ind;
}
EXPORT_SYMBOL(cnss_pci_is_device_down);

static char *cnss_mhi_state_to_str(enum cnss_mhi_state mhi_state)
{
	switch (mhi_state) {
	case CNSS_MHI_INIT:
		return "INIT";
	case CNSS_MHI_DEINIT:
		return "DEINIT";
	case CNSS_MHI_POWER_ON:
		return "POWER_ON";
	case CNSS_MHI_POWER_OFF:
		return "POWER_OFF";
	case CNSS_MHI_FORCE_POWER_OFF:
		return "FORCE_POWER_OFF";
	case CNSS_MHI_SUSPEND:
		return "SUSPEND";
	case CNSS_MHI_RESUME:
		return "RESUME";
	case CNSS_MHI_TRIGGER_RDDM:
		return "TRIGGER_RDDM";
	case CNSS_MHI_RDDM:
		return "MHI_RDDM";
	case CNSS_MHI_RDDM_DONE:
		return "RDDM_DONE";
	case CNSS_MHI_SOC_RESET:
		return "SOC_RESET";
	case CNSS_MHI_MISSION_MODE:
		return "MISSION_MODE";
	default:
		return "UNKNOWN";
	}
};

static int cnss_pci_check_mhi_state_bit(struct cnss_pci_data *pci_priv,
					enum cnss_mhi_state mhi_state)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	switch (mhi_state) {
	case CNSS_MHI_INIT:
		if (!test_bit(CNSS_MHI_INIT, &pci_priv->mhi_state))
			return 0;
		break;
	case CNSS_MHI_DEINIT:
	case CNSS_MHI_POWER_ON:
		if (test_bit(CNSS_MHI_INIT, &pci_priv->mhi_state) &&
		    !test_bit(CNSS_MHI_POWER_ON, &pci_priv->mhi_state))
			return 0;
		break;
	case CNSS_MHI_FORCE_POWER_OFF:
		if (test_bit(CNSS_MHI_POWER_ON, &pci_priv->mhi_state))
			return 0;
		break;
	case CNSS_MHI_POWER_OFF:
	case CNSS_MHI_SUSPEND:
		if (test_bit(CNSS_MHI_POWER_ON, &pci_priv->mhi_state) &&
		    !test_bit(CNSS_MHI_SUSPEND, &pci_priv->mhi_state))
			return 0;
		break;
	case CNSS_MHI_RESUME:
		if (test_bit(CNSS_MHI_SUSPEND, &pci_priv->mhi_state))
			return 0;
		break;
	case CNSS_MHI_TRIGGER_RDDM:
		if (test_bit(CNSS_MHI_POWER_ON, &pci_priv->mhi_state) &&
		    !test_bit(CNSS_MHI_TRIGGER_RDDM, &pci_priv->mhi_state))
			return 0;
		break;
	case CNSS_MHI_RDDM:
	case CNSS_MHI_RDDM_DONE:
	case CNSS_MHI_SOC_RESET:
	case CNSS_MHI_MISSION_MODE:
		return 0;
	default:
		cnss_pr_err("Unhandled MHI state: %s(%d)\n",
			    cnss_mhi_state_to_str(mhi_state), mhi_state);
	}

	cnss_pr_err("Cannot set MHI state %s(%d) in current MHI state (0x%lx)\n",
		    cnss_mhi_state_to_str(mhi_state), mhi_state,
		    pci_priv->mhi_state);

	return -EINVAL;
}

static void cnss_pci_set_mhi_state_bit(struct cnss_pci_data *pci_priv,
				       enum cnss_mhi_state mhi_state)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	switch (mhi_state) {
	case CNSS_MHI_INIT:
		set_bit(CNSS_MHI_INIT, &pci_priv->mhi_state);
		break;
	case CNSS_MHI_DEINIT:
		clear_bit(CNSS_MHI_INIT, &pci_priv->mhi_state);
		break;
	case CNSS_MHI_POWER_ON:
		set_bit(CNSS_MHI_POWER_ON, &pci_priv->mhi_state);
		break;
	case CNSS_MHI_POWER_OFF:
	case CNSS_MHI_FORCE_POWER_OFF:
		clear_bit(CNSS_MHI_POWER_ON, &pci_priv->mhi_state);
		clear_bit(CNSS_MHI_TRIGGER_RDDM, &pci_priv->mhi_state);
		clear_bit(CNSS_MHI_RDDM_DONE, &pci_priv->mhi_state);
		clear_bit(CNSS_MHI_MISSION_MODE, &pci_priv->mhi_state);
		break;
	case CNSS_MHI_SUSPEND:
		set_bit(CNSS_MHI_SUSPEND, &pci_priv->mhi_state);
		break;
	case CNSS_MHI_RESUME:
		clear_bit(CNSS_MHI_SUSPEND, &pci_priv->mhi_state);
		break;
	case CNSS_MHI_TRIGGER_RDDM:
		set_bit(CNSS_MHI_TRIGGER_RDDM, &pci_priv->mhi_state);
		break;
	case CNSS_MHI_RDDM:
		set_bit(CNSS_MHI_RDDM, &pci_priv->mhi_state);
		break;
	case CNSS_MHI_RDDM_DONE:
		set_bit(CNSS_MHI_RDDM_DONE, &pci_priv->mhi_state);
		break;
	case CNSS_MHI_SOC_RESET:
		set_bit(CNSS_MHI_SOC_RESET, &pci_priv->mhi_state);
		break;
	case CNSS_MHI_MISSION_MODE:
		set_bit(CNSS_MHI_MISSION_MODE, &pci_priv->mhi_state);
		break;
	default:
		cnss_pr_err("Unhandled MHI state (%d)\n", mhi_state);
	}
}

static int cnss_pci_set_mhi_state(struct cnss_pci_data *pci_priv,
				  enum cnss_mhi_state mhi_state)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = NULL;

	if (!pci_priv) {
		pr_err("pci_priv is NULL!\n");
		return -ENODEV;
	}

	plat_priv = pci_priv->plat_priv;

	if (pci_priv->device_id == QCA6174_DEVICE_ID)
		return 0;

	if (mhi_state < 0) {
		cnss_pr_err("Invalid MHI state (%d)\n", mhi_state);
		return -EINVAL;
	}

	ret = cnss_pci_check_mhi_state_bit(pci_priv, mhi_state);
	if (ret)
		goto out;
	cnss_pr_dbg("Setting MHI state: %s(%d)\n",
		    cnss_mhi_state_to_str(mhi_state), mhi_state);

	if (mhi_state == CNSS_MHI_SOC_RESET) {
		cnss_pci_set_mhi_state_bit(pci_priv, mhi_state);
		mhi_soc_reset(pci_priv->mhi_ctrl);
		return 0;
	}

	switch (mhi_state) {
	case CNSS_MHI_INIT:
		ret = mhi_prepare_for_power_up(pci_priv->mhi_ctrl);
		break;
	case CNSS_MHI_DEINIT:
		mhi_unprepare_after_power_down(pci_priv->mhi_ctrl);
		ret = 0;
		break;
	case CNSS_MHI_POWER_ON:
		ret = mhi_sync_power_up(pci_priv->mhi_ctrl);
		break;
	case CNSS_MHI_POWER_OFF:
		mhi_power_down(pci_priv->mhi_ctrl, true);
		ret = 0;
		break;
	case CNSS_MHI_FORCE_POWER_OFF:
		mhi_power_down(pci_priv->mhi_ctrl, false);
		ret = 0;
		break;
	case CNSS_MHI_SUSPEND:
#ifdef CONFIG_PCI_SUSPENDRESUME
		if (pci_priv->drv_connected_last)
			ret = mhi_pm_fast_suspend(pci_priv->mhi_ctrl, true);
		else
			ret = mhi_pm_suspend(pci_priv->mhi_ctrl);
#endif
		break;
	case CNSS_MHI_RESUME:
#ifdef CONFIG_PCI_SUSPENDRESUME
		if (pci_priv->drv_connected_last)
			ret = mhi_pm_fast_resume(pci_priv->mhi_ctrl, true);
		else
			ret = mhi_pm_resume(pci_priv->mhi_ctrl);
#endif
		break;
	case CNSS_MHI_TRIGGER_RDDM:
		ret = mhi_force_rddm_mode(pci_priv->mhi_ctrl);
		break;
	case CNSS_MHI_RDDM_DONE:
	case CNSS_MHI_MISSION_MODE:
		break;
	default:
		cnss_pr_err("Unhandled MHI state (%d)\n", mhi_state);
		ret = -EINVAL;
	}

	if (ret)
		goto out;

	cnss_pci_set_mhi_state_bit(pci_priv, mhi_state);

	return 0;

out:
	cnss_pr_err("Failed to set MHI state: %s(%d)\n",
		    cnss_mhi_state_to_str(mhi_state), mhi_state);
	return ret;
}

static int cnss_pci_set_qrtr_node_id(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	u32 val = 0;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	switch (plat_priv->device_id) {
	case QCN9000_DEVICE_ID:
	case QCN9224_DEVICE_ID:
		cnss_pr_info("Setting 0x%x to QRTR_NODE_ID_REG\n",
			     plat_priv->qrtr_node_id);

		writel_relaxed(plat_priv->qrtr_node_id,
			       pci_priv->bar +
			       (QRTR_NODE_ID_REG & QRTR_NODE_ID_REG_MASK));

		val = readl_relaxed(pci_priv->bar +
				    (QRTR_NODE_ID_REG & QRTR_NODE_ID_REG_MASK));

		cnss_pr_info("Value from QRTR_NODE_ID_REG: 0x%x\n", val);

		if (val != plat_priv->qrtr_node_id) {
			cnss_pr_err("%s: QRTR Node ID write to QRTR_NODE_ID_REG failed 0x%x",
				    __func__, val);
			ret = -EINVAL;
		}
		break;
	default:
		cnss_pr_dbg("Invalid device id 0x%lx", plat_priv->device_id);
		ret = -ENODEV;
	}

	return ret;
}

int cnss_pci_start_mhi(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = NULL;

	if (!pci_priv) {
		pr_err("pci_priv is NULL!\n");
		return -ENODEV;
	}

	plat_priv = pci_priv->plat_priv;
	if (test_bit(FBC_BYPASS, &plat_priv->ctrl_params.quirks))
		return 0;

	if (MHI_TIMEOUT_OVERWRITE_MS)
		pci_priv->mhi_ctrl->timeout_ms = MHI_TIMEOUT_OVERWRITE_MS * 1000;

	ret = cnss_pci_set_mhi_state(pci_priv, CNSS_MHI_INIT);
	if (ret)
		goto out;

	ret = cnss_pci_set_qrtr_node_id(pci_priv);
	if (ret)
		goto out;

	/* Start the timer to dump MHI/PBL/SBL debug data periodically */
	mod_timer(&pci_priv->boot_debug_timer,
		  jiffies + msecs_to_jiffies(BOOT_DEBUG_TIMEOUT_MS));

	ret = cnss_pci_set_mhi_state(pci_priv, CNSS_MHI_POWER_ON);

	if (ret)
		goto out;

	return 0;

out:
	if (ret == -ETIMEDOUT) {
		/* If MHI Start timedout but the target is already in mission
		 * mode and is able to do RDDM, RDDM cookie would be set.
		 * Dump SBL SRAM memory only if RDDM cookie is not set.
		 */
		if (!cnss_mhi_scan_rddm_cookie(pci_priv,
					  DEVICE_RDDM_COOKIE))
			cnss_pci_dump_bl_sram_mem(pci_priv);
	}

	return ret;
}

static void cnss_pci_power_off_mhi(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	if (test_bit(FBC_BYPASS, &plat_priv->ctrl_params.quirks))
		return;

	cnss_pci_set_mhi_state_bit(pci_priv, CNSS_MHI_RESUME);

	if (!pci_priv->pci_link_down_ind)
		cnss_pci_set_mhi_state(pci_priv, CNSS_MHI_POWER_OFF);
	else
		cnss_pci_set_mhi_state(pci_priv, CNSS_MHI_FORCE_POWER_OFF);
}

static void cnss_pci_deinit_mhi(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	if (test_bit(FBC_BYPASS, &plat_priv->ctrl_params.quirks))
		return;

	cnss_pci_set_mhi_state(pci_priv, CNSS_MHI_DEINIT);
}

static void cnss_pci_get_timestamp_qcn9000(struct cnss_pci_data *pci_priv,
					   u32 *low, u32 *high)
{
	cnss_pci_reg_write(pci_priv, QCN9000_WLAON_GLOBAL_COUNTER_CTRL5,
			   QCN9000_TIME_SYNC_CLEAR);
	cnss_pci_reg_write(pci_priv, QCN9000_WLAON_GLOBAL_COUNTER_CTRL5,
			   QCN9000_TIME_SYNC_ENABLE);

	cnss_pci_reg_read(pci_priv->plat_priv,
			  QCN9000_WLAON_GLOBAL_COUNTER_CTRL3, low);
	cnss_pci_reg_read(pci_priv->plat_priv,
			  QCN9000_WLAON_GLOBAL_COUNTER_CTRL4, high);
}

static void cnss_pci_get_timestamp_qcn9224(struct cnss_pci_data *pci_priv,
					   u32 *low, u32 *high)
{
	cnss_pci_reg_read(pci_priv->plat_priv,
			  QCN9224_PCIE_PCIE_MHI_TIME_LOW, low);
	cnss_pci_reg_read(pci_priv->plat_priv,
			  QCN9224_PCIE_PCIE_MHI_TIME_HIGH, high);
}

static int cnss_pci_get_device_timestamp(struct cnss_pci_data *pci_priv,
					 u64 *time_us)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	u32 low = 0, high = 0;
	u64 device_ticks;

	if (!plat_priv->device_freq_hz) {
		cnss_pr_err("Device time clock frequency is not valid\n");
		return -EINVAL;
	}

	switch (pci_priv->device_id) {
	case QCN9000_DEVICE_ID:
		cnss_pci_get_timestamp_qcn9000(pci_priv, &low, &high);
		break;
	case QCN9224_DEVICE_ID:
		cnss_pci_get_timestamp_qcn9224(pci_priv, &low, &high);
		break;
	default:
		cnss_pr_err("Unknown device type %d\n",
			    pci_priv->device_id);
		return -EINVAL;
	}

	device_ticks = (u64)high << 32 | low;
	do_div(device_ticks, plat_priv->device_freq_hz / 100000);
	*time_us = device_ticks * 10;

	return 0;
}

static int cnss_pci_update_timestamp(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	u64 host_time_us, device_time_us, offset;
	u32 low, high;
	int ret;

	ret = cnss_pci_check_link_status(pci_priv);
	if (ret)
		return ret;

	ret = cnss_pci_force_wake_get(pci_priv);
	if (ret)
		return ret;

	host_time_us = cnss_get_host_timestamp(plat_priv);
	ret = cnss_pci_get_device_timestamp(pci_priv, &device_time_us);
	if (ret)
		goto force_wake_put;

	if (host_time_us < device_time_us) {
		cnss_pr_err("Host time (%llu us) is smaller than device time (%llu us), stop\n",
			    host_time_us, device_time_us);
		ret = -EINVAL;
		goto force_wake_put;
	}

	offset = host_time_us - device_time_us;
	cnss_pr_dbg("Host time = %llu us, device time = %llu us, offset = %llu us\n",
		    host_time_us, device_time_us, offset);

	low = offset & 0xFFFFFFFF;
	high = offset >> 32;

	cnss_pci_reg_write(pci_priv, QCA6390_PCIE_SHADOW_REG_VALUE_34, low);
	cnss_pci_reg_write(pci_priv, QCA6390_PCIE_SHADOW_REG_VALUE_35, high);

	cnss_pci_reg_read(plat_priv, QCA6390_PCIE_SHADOW_REG_VALUE_34, &low);
	cnss_pci_reg_read(plat_priv, QCA6390_PCIE_SHADOW_REG_VALUE_35, &high);

	cnss_pr_dbg("Updated time sync regs [0x%x] = 0x%x, [0x%x] = 0x%x\n",
		    QCA6390_PCIE_SHADOW_REG_VALUE_34, low,
		    QCA6390_PCIE_SHADOW_REG_VALUE_35, high);

force_wake_put:
	cnss_pci_force_wake_put(pci_priv);

	return ret;
}

static u64 cnss_pci_get_q6_time(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	u64 device_time_us;
	int ret;

	ret = cnss_pci_check_link_status(pci_priv);
	if (ret) {
		cnss_pr_err("PCI link status is down\n");
		return 0;
	}

	cnss_pci_get_device_timestamp(pci_priv, &device_time_us);

	return device_time_us;
}

static void cnss_pci_time_sync_work_hdlr(struct work_struct *work)
{
	struct cnss_pci_data *pci_priv =
		container_of(work, struct cnss_pci_data, time_sync_work.work);
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	unsigned int time_sync_period_ms =
		plat_priv->ctrl_params.time_sync_period;

	if (cnss_pci_is_device_down(&pci_priv->pci_dev->dev))
		return;

	if (cnss_pci_pm_runtime_get_sync(pci_priv) < 0)
		goto runtime_pm_put;

	cnss_pci_update_timestamp(pci_priv);
	schedule_delayed_work(&pci_priv->time_sync_work,
			      msecs_to_jiffies(time_sync_period_ms));

runtime_pm_put:
	cnss_pci_pm_runtime_mark_last_busy(pci_priv);
	cnss_pci_pm_runtime_put_autosuspend(pci_priv);
}

static int cnss_pci_start_time_sync_update(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	switch (pci_priv->device_id) {
	case QCA6390_DEVICE_ID:
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (!plat_priv->device_freq_hz) {
		cnss_pr_dbg("Device time clock frequency is not valid, skip time sync\n");
		return -EINVAL;
	}

	cnss_pci_time_sync_work_hdlr(&pci_priv->time_sync_work.work);

	return 0;
}

static void cnss_pci_stop_time_sync_update(struct cnss_pci_data *pci_priv)
{
	switch (pci_priv->device_id) {
	case QCA6390_DEVICE_ID:
		break;
	default:
		return;
	}

	cancel_delayed_work_sync(&pci_priv->time_sync_work);
}

int cnss_pci_call_driver_probe(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv;

	if (!pci_priv)
		return -ENODEV;

	plat_priv = pci_priv->plat_priv;

	if (test_bit(CNSS_DRIVER_DEBUG, &plat_priv->driver_state)) {
		clear_bit(CNSS_DRIVER_RECOVERY, &plat_priv->driver_state);
		cnss_pr_dbg("Skip driver probe\n");
		goto out;
	}

	if (!plat_priv->driver_ops) {
		cnss_pr_err("driver_ops is NULL pci_priv %p plat_priv %p\n",
			    pci_priv, plat_priv);
		ret = -EINVAL;
		goto out;
	}

	if (test_bit(CNSS_DRIVER_RECOVERY, &plat_priv->driver_state) &&
	    test_bit(CNSS_DRIVER_PROBED, &plat_priv->driver_state)) {
		ret = plat_priv->driver_ops->reinit(pci_priv->pci_dev,
						   pci_priv->pci_device_id);
		if (ret) {
			cnss_pr_err("Failed to reinit host driver, err = %d\n",
				    ret);
			goto out;
		}
		clear_bit(CNSS_DRIVER_RECOVERY, &plat_priv->driver_state);
		complete(&plat_priv->recovery_complete);
	} else if (test_bit(CNSS_DRIVER_LOADING, &plat_priv->driver_state)) {
		ret = plat_priv->driver_ops->probe(pci_priv->pci_dev,
						  pci_priv->pci_device_id);
		if (ret) {
			cnss_pr_err("Failed to probe host driver, err = %d\n",
				    ret);
			goto out;
		}
		clear_bit(CNSS_DRIVER_RECOVERY, &plat_priv->driver_state);
		clear_bit(CNSS_DRIVER_LOADING, &plat_priv->driver_state);
		set_bit(CNSS_DRIVER_PROBED, &plat_priv->driver_state);
	} else if (test_bit(CNSS_DRIVER_IDLE_RESTART,
			    &plat_priv->driver_state)) {
		ret = plat_priv->driver_ops->idle_restart(pci_priv->pci_dev,
			pci_priv->pci_device_id);
		if (ret) {
			cnss_pr_err("Failed to idle restart host driver, err = %d\n",
				    ret);
			goto out;
		}
		clear_bit(CNSS_DRIVER_RECOVERY, &plat_priv->driver_state);
		clear_bit(CNSS_DRIVER_IDLE_RESTART, &plat_priv->driver_state);
		complete(&plat_priv->power_up_complete);
	} else {
		complete(&plat_priv->power_up_complete);
	}

	cnss_pci_start_time_sync_update(pci_priv);

	return 0;

out:
	return ret;
}

int cnss_pci_call_driver_remove(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv;
	int ret;

	if (!pci_priv)
		return -ENODEV;

	plat_priv = pci_priv->plat_priv;

	cnss_pci_stop_time_sync_update(pci_priv);

	if (test_bit(CNSS_COLD_BOOT_CAL, &plat_priv->driver_state) ||
	    test_bit(CNSS_FW_BOOT_RECOVERY, &plat_priv->driver_state) ||
	    test_bit(CNSS_DRIVER_DEBUG, &plat_priv->driver_state)) {
		cnss_pr_dbg("Skip driver remove\n");
		return 0;
	}

	if (!pci_priv->driver_ops) {
		cnss_pr_err("driver_ops is NULL\n");
		return -EINVAL;
	}

	if (test_bit(CNSS_DRIVER_RECOVERY, &plat_priv->driver_state) &&
	    test_bit(CNSS_DRIVER_PROBED, &plat_priv->driver_state)) {
		pci_priv->driver_ops->shutdown(pci_priv->pci_dev);
	} else if (test_bit(CNSS_DRIVER_UNLOADING, &plat_priv->driver_state)) {
		pci_priv->driver_ops->remove(pci_priv->pci_dev);
		clear_bit(CNSS_DRIVER_PROBED, &plat_priv->driver_state);
		clear_bit(CNSS_DEV_ERR_NOTIFY, &plat_priv->driver_state);
	} else if (test_bit(CNSS_DRIVER_IDLE_SHUTDOWN,
			    &plat_priv->driver_state)) {
		ret = pci_priv->driver_ops->idle_shutdown(pci_priv->pci_dev);
		if (ret == -EAGAIN) {
			clear_bit(CNSS_DRIVER_IDLE_SHUTDOWN,
				  &plat_priv->driver_state);
			return ret;
		}
		clear_bit(CNSS_DEV_ERR_NOTIFY, &plat_priv->driver_state);
	}

	return 0;
}

int cnss_pci_call_driver_modem_status(struct cnss_pci_data *pci_priv,
				      int modem_current_status)
{
	struct cnss_wlan_driver *driver_ops;

	if (!pci_priv)
		return -ENODEV;

	driver_ops = pci_priv->driver_ops;
	if (!driver_ops || !driver_ops->modem_status)
		return -EINVAL;

	driver_ops->modem_status(pci_priv->pci_dev, modem_current_status);

	return 0;
}

int cnss_pci_update_status(struct cnss_pci_data *pci_priv,
			   enum cnss_driver_status status)
{
	struct cnss_wlan_driver *driver_ops;
	struct cnss_plat_data *plat_priv = NULL;

	if (!pci_priv) {
		cnss_pr_err("%s: pci_priv is NULL", __func__);
		return -ENODEV;
	}

	plat_priv = pci_priv->plat_priv;
	driver_ops = pci_priv->driver_ops;
	if (!driver_ops || !driver_ops->update_status) {
		cnss_pr_err("%s: driver_ops is NULL", __func__);
		return -EINVAL;
	}

	cnss_pr_dbg("Update driver status: %d\n", status);

	if (status == CNSS_FW_DOWN)
		driver_ops->fatal((struct pci_dev *)plat_priv->plat_dev,
				  (const struct pci_device_id *)
				  plat_priv->plat_dev_id);

	return 0;
}

static void cnss_pci_dump_shadow_reg(struct cnss_pci_data *pci_priv)
{
	int i, j = 0, array_size = SHADOW_REG_COUNT + SHADOW_REG_INTER_COUNT;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	u32 reg_offset;

	if (in_interrupt() || irqs_disabled())
		return;

	if (cnss_pci_check_link_status(pci_priv))
		return;

	if (!pci_priv->debug_reg) {
		pci_priv->debug_reg = devm_kzalloc(&pci_priv->pci_dev->dev,
						   sizeof(*pci_priv->debug_reg)
						   * array_size, GFP_KERNEL);
		if (!pci_priv->debug_reg)
			return;
	}

	if (cnss_pci_force_wake_get(pci_priv))
		return;

	cnss_pr_dbg("Start to dump shadow registers\n");

	for (i = 0; i < SHADOW_REG_COUNT; i++, j++) {
		reg_offset = QCA6390_PCIE_SHADOW_REG_VALUE_0 + i * 4;
		pci_priv->debug_reg[j].offset = reg_offset;
		if (cnss_pci_reg_read(plat_priv, reg_offset,
				      &pci_priv->debug_reg[j].val))
			goto force_wake_put;
	}

	for (i = 0; i < SHADOW_REG_INTER_COUNT; i++, j++) {
		reg_offset = QCA6390_PCIE_SHADOW_REG_INTER_0 + i * 4;
		pci_priv->debug_reg[j].offset = reg_offset;
		if (cnss_pci_reg_read(plat_priv, reg_offset,
				      &pci_priv->debug_reg[j].val))
			goto force_wake_put;
	}

force_wake_put:
	cnss_pci_force_wake_put(pci_priv);
}

static int cnss_qca6174_powerup(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	ret = cnss_power_on_device(plat_priv, 0);
	if (ret) {
		cnss_pr_err("Failed to power on device, err = %d\n", ret);
		goto out;
	}

	ret = cnss_resume_pci_link(pci_priv);
	if (ret) {
		cnss_pr_err("Failed to resume PCI link, err = %d\n", ret);
		goto power_off;
	}

	ret = cnss_pci_call_driver_probe(pci_priv);
	if (ret)
		goto suspend_link;

	return 0;
suspend_link:
	cnss_suspend_pci_link(pci_priv);
power_off:
	cnss_power_off_device(plat_priv, 0);
out:
	return ret;
}

static int cnss_qca6174_shutdown(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	cnss_pci_pm_runtime_resume(pci_priv);

	ret = cnss_pci_call_driver_remove(pci_priv);
	if (ret == -EAGAIN)
		goto out;

	cnss_request_bus_bandwidth(&plat_priv->plat_dev->dev,
				   CNSS_BUS_WIDTH_NONE);
	cnss_pci_set_monitor_wake_intr(pci_priv, false);
	cnss_pci_set_auto_suspended(pci_priv, 0);

	ret = cnss_suspend_pci_link(pci_priv);
	if (ret)
		cnss_pr_err("Failed to suspend PCI link, err = %d\n", ret);

	cnss_power_off_device(plat_priv, 0);

	clear_bit(CNSS_DRIVER_UNLOADING, &plat_priv->driver_state);
	clear_bit(CNSS_DRIVER_IDLE_SHUTDOWN, &plat_priv->driver_state);

out:
	return ret;
}

static void cnss_qca6174_crash_shutdown(struct cnss_pci_data *pci_priv)
{
	if (pci_priv->driver_ops && pci_priv->driver_ops->crash_shutdown)
		pci_priv->driver_ops->crash_shutdown(pci_priv->pci_dev);
}

#if (KERNEL_VERSION(5, 15, 0) > LINUX_VERSION_CODE)
static int cnss_qca6174_ramdump(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct cnss_ramdump_info *ramdump_info;
	struct ramdump_segment segment;

	ramdump_info = &plat_priv->ramdump_info;
	if (!ramdump_info->ramdump_size)
		return -EINVAL;

	memset(&segment, 0, sizeof(segment));
	segment.v_address = ramdump_info->ramdump_va;
	segment.size = ramdump_info->ramdump_size;
	ret = do_ramdump(ramdump_info->ramdump_dev, &segment, 1);
	return ret;
}
#else
static int cnss_qca6174_ramdump(struct cnss_pci_data *pci_priv)
{
	return 0;
}
#endif

static int cnss_qcn9000_powerup(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = NULL;
	unsigned int timeout;

	if (!pci_priv) {
		cnss_pr_err("cnss_qcn9000_powerup pci_priv is NULL!\n");
		return -ENODEV;
	}

	plat_priv = pci_priv->plat_priv;

	if (plat_priv->ramdump_info_v2.dump_data_valid ||
	    test_bit(CNSS_DRIVER_RECOVERY, &plat_priv->driver_state)) {
		cnss_pci_clear_dump_info(pci_priv);
		cnss_pci_deinit_mhi(pci_priv);
	}

	ret = cnss_power_on_device(plat_priv, plat_priv->device_id);
	if (ret) {
		cnss_pr_err("Failed to power on device, err = %d\n", ret);
		goto out;
	}

	ret = cnss_resume_pci_link(pci_priv);
	if (ret) {
		cnss_pr_err("Failed to resume PCI link, err = %d\n", ret);
		goto power_off;
	}

	timeout = cnss_get_boot_timeout(&pci_priv->pci_dev->dev);

	ret = cnss_pci_start_mhi(pci_priv);
	if (ret) {
		cnss_fatal_err("Failed to start MHI, err = %d\n", ret);
		CNSS_ASSERT(0);
		if (!test_bit(CNSS_DEV_ERR_NOTIFY, &plat_priv->driver_state) &&
		    !pci_priv->pci_link_down_ind && timeout)
			mod_timer(&plat_priv->fw_boot_timer,
				  jiffies + msecs_to_jiffies(timeout >> 1));
		return 0;
	}

	if (test_bit(USE_CORE_ONLY_FW, &plat_priv->ctrl_params.quirks)) {
		clear_bit(CNSS_FW_BOOT_RECOVERY, &plat_priv->driver_state);
		clear_bit(CNSS_DRIVER_RECOVERY, &plat_priv->driver_state);
		return 0;
	}

	cnss_set_pin_connect_status(plat_priv);

	return 0;

power_off:
	cnss_power_off_device(plat_priv, 0);
out:
	return ret;
}

static void cnss_mhi_soc_reset(struct pci_dev *pci_dev)
{
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	struct cnss_plat_data *plat_priv =
		cnss_bus_dev_to_plat_priv(&pci_dev->dev);

	if (test_bit(CNSS_MHI_RDDM, &pci_priv->mhi_state)) {
		cnss_pr_info("MHI SOC_RESET is not required as MHI is already in RDDM state\n");
		clear_bit(CNSS_MHI_RDDM, &pci_priv->mhi_state);
		return;
	}

	cnss_pci_set_mhi_state(pci_priv, CNSS_MHI_SOC_RESET);
	if (!wait_for_completion_timeout(&plat_priv->soc_reset_request_complete,
				msecs_to_jiffies(MHI_SOC_RESET_DELAY))) {
		cnss_pr_err("%s: Failed to switch RDDM state\n", __func__);
		reinit_completion(&plat_priv->soc_reset_request_complete);
	}
}

static int cnss_qcn9000_shutdown(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = NULL;

	if (!pci_priv)
		return -ENODEV;

	plat_priv = pci_priv->plat_priv;

	/* If dump_data_valid is set, then shutdown would be triggered after
	 * ramdump upload is complete in cnss_pci_dev_ramdump.
	 * This is done to prevent the fbc_image and rddm_image segments
	 * from getting freed before getting uploaded to external server.
	 */
	if (plat_priv->ramdump_info_v2.dump_data_valid) {
		cnss_pr_info("Skipping shutdown to wait for dump collection\n");
		return ret;
	}

	cnss_mhi_soc_reset(plat_priv->pci_dev);

	cnss_pr_info("Shutting down %s\n", plat_priv->device_name);
	cnss_pci_pm_runtime_resume(pci_priv);

	cnss_request_bus_bandwidth(&plat_priv->plat_dev->dev,
				   CNSS_BUS_WIDTH_NONE);
	cnss_pci_set_monitor_wake_intr(pci_priv, false);
	cnss_pci_set_auto_suspended(pci_priv, 0);

	if ((test_bit(CNSS_DRIVER_LOADING, &plat_priv->driver_state) ||
	     test_bit(CNSS_DRIVER_UNLOADING, &plat_priv->driver_state) ||
	     test_bit(CNSS_DRIVER_IDLE_RESTART, &plat_priv->driver_state) ||
	     test_bit(CNSS_DRIVER_IDLE_SHUTDOWN, &plat_priv->driver_state)) &&
	    test_bit(CNSS_DEV_ERR_NOTIFY, &plat_priv->driver_state)) {
		del_timer(&pci_priv->dev_rddm_timer);
	}

	cnss_pci_power_off_mhi(pci_priv);
	ret = cnss_suspend_pci_link(pci_priv);
	if (ret)
		cnss_pr_err("Failed to suspend PCI link, err = %d\n", ret);

	cnss_pci_deinit_mhi(pci_priv);

	cnss_power_off_device(plat_priv, plat_priv->device_id);

	pci_priv->remap_window = 0;

	clear_bit(CNSS_FW_READY, &plat_priv->driver_state);
	clear_bit(CNSS_FW_MEM_READY, &plat_priv->driver_state);
	clear_bit(CNSS_DRIVER_UNLOADING, &plat_priv->driver_state);
	clear_bit(CNSS_DRIVER_IDLE_SHUTDOWN, &plat_priv->driver_state);
	cnss_pci_remove(plat_priv->pci_dev);
	plat_priv->driver_state = 0;

	return ret;
}

static void cnss_qcn9000_crash_shutdown(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	cnss_pr_err("Crash shutdown device %s with driver_state 0x%lx\n",
		    plat_priv->device_name, plat_priv->driver_state);

	cnss_pci_collect_dump_info(pci_priv, true);
}

#if (KERNEL_VERSION(5, 15, 0) > LINUX_VERSION_CODE)
static int cnss_qcn9000_ramdump(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct cnss_ramdump_info_v2 *info_v2 = &plat_priv->ramdump_info_v2;
	struct cnss_dump_data *dump_data = &info_v2->dump_data;
	struct cnss_dump_seg *dump_seg = info_v2->dump_data_vaddr;
	struct ramdump_segment *ramdump_segs, *s;
	struct cnss_dump_meta_info *meta_info;
	int i, ret = 0, idx = 0;

	if (!info_v2->dump_data_valid ||
	    dump_data->nentries == 0)
		return 0;

	/* First segment of the dump_data will have meta info in
	 * cnss_dump_meta_info structure format.
	 * Allocate extra segment for meta info and start filling the dump_seg
	 * entries from ramdump_segs + NUM_META_INFO_SEGMENTS.
	 */
	ramdump_segs = kcalloc(dump_data->nentries +
			       CNSS_NUM_META_INFO_SEGMENTS,
			       sizeof(*ramdump_segs),
			       GFP_KERNEL);
	if (!ramdump_segs)
		return -ENOMEM;

	meta_info = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!meta_info) {
		kfree(ramdump_segs);
		return -ENOMEM;
	}

	meta_info->magic = CNSS_RAMDUMP_MAGIC;
	meta_info->version = CNSS_RAMDUMP_VERSION_V2;
	meta_info->chipset = pci_priv->device_id;

	ramdump_segs->v_address = meta_info;
	ramdump_segs->size = sizeof(*meta_info);

	s = ramdump_segs + CNSS_NUM_META_INFO_SEGMENTS;
	for (i = 0; i < dump_data->nentries; i++) {
		if (dump_seg->type >= CNSS_FW_DUMP_TYPE_MAX) {
			cnss_pr_err("Unsupported dump type: %d\n",
				    dump_seg->type);
			continue;
		}

		if (dump_seg->type != meta_info->entry[idx].type)
			idx++;

		if (meta_info->entry[idx].entry_start == 0) {
			meta_info->entry[idx].type = dump_seg->type;
			meta_info->entry[idx].entry_start =
						i + CNSS_NUM_META_INFO_SEGMENTS;
		}
		meta_info->entry[idx].entry_num++;

		s->address = dump_seg->address;
		s->v_address = dump_seg->v_address;
		s->size = dump_seg->size;
		s++;
		dump_seg++;
	}

	meta_info->total_entries = idx + 1;

	cnss_pr_dbg("Dumping meta_info: total_entries: %d",
		    meta_info->total_entries);
	for (i = 0; i < meta_info->total_entries; i++)
		cnss_pr_dbg("entry %d type %d entry_start %d entry_num %d",
			    i, meta_info->entry[i].type,
			    meta_info->entry[i].entry_start,
			    meta_info->entry[i].entry_num);

#ifdef CONFIG_CNSS2_KERNEL_IPQ
	ret = create_ramdump_device_file(info_v2->ramdump_dev);
#endif
	if (ret)
		goto clear_dump_info;

	ret = do_elf_ramdump(info_v2->ramdump_dev, ramdump_segs,
			     dump_data->nentries + CNSS_NUM_META_INFO_SEGMENTS);

clear_dump_info:
	kfree(meta_info);
	kfree(ramdump_segs);

	return ret;
}
#else
/* Using completion event inside dynamically allocated ramdump_desc
 * may result a race between freeing the event after setting it to
 * complete inside dev coredump free callback and the thread that is
 * waiting for completion.
 */
DECLARE_COMPLETION(dump_done);
#define TIMEOUT_SAVE_DUMP_MS 30000

#define SIZEOF_ELF_STRUCT(__xhdr)					\
static inline size_t sizeof_elf_##__xhdr(unsigned char class)		\
{									\
	if (class == ELFCLASS32)					\
		return sizeof(struct elf32_##__xhdr);			\
	else								\
		return sizeof(struct elf64_##__xhdr);			\
}

SIZEOF_ELF_STRUCT(phdr)
SIZEOF_ELF_STRUCT(hdr)

#define set_xhdr_property(__xhdr, arg, class, member, value)		\
do {									\
	if (class == ELFCLASS32)					\
		((struct elf32_##__xhdr *)arg)->member = value;		\
	else								\
		((struct elf64_##__xhdr *)arg)->member = value;		\
} while (0)

#define set_ehdr_property(arg, class, member, value) \
	set_xhdr_property(hdr, arg, class, member, value)
#define set_phdr_property(arg, class, member, value) \
	set_xhdr_property(phdr, arg, class, member, value)

/* These replace qcom_ramdump driver APIs called from common API
 * cnss_do_elf_dump() by the ones defined here.
 */
#define qcom_dump_segment cnss_qcom_dump_segment
#define qcom_elf_dump cnss_qcom_elf_dump

struct cnss_qcom_dump_segment {
	struct list_head node;
	dma_addr_t da;
	void *va;
	size_t size;
};

struct cnss_qcom_ramdump_desc {
	void *data;
	struct completion dump_done;
};

static ssize_t cnss_qcom_devcd_readv(char *buffer, loff_t offset, size_t count,
				     void *data, size_t datalen)
{
	struct cnss_qcom_ramdump_desc *desc = data;

	return memory_read_from_buffer(buffer, count, &offset, desc->data,
				       datalen);
}

static void cnss_qcom_devcd_freev(void *data)
{
	struct cnss_qcom_ramdump_desc *desc = data;

	cnss_pr_vdbg("Free dump data for dev coredump\n");

	complete(&dump_done);
	vfree(desc->data);
	kfree(desc);
}
static int cnss_qcom_devcd_dump(struct device *dev, void *data, size_t datalen,
				gfp_t gfp)
{
	struct cnss_qcom_ramdump_desc *desc;
	unsigned int timeout = TIMEOUT_SAVE_DUMP_MS;
	int ret;
	struct cnss_plat_data *plat_priv = NULL;

	desc = kmalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	desc->data = data;
	reinit_completion(&dump_done);

	dev_coredumpm(dev, NULL, desc, datalen, gfp,
		      cnss_qcom_devcd_readv, cnss_qcom_devcd_freev);

	ret = wait_for_completion_timeout(&dump_done,
					  msecs_to_jiffies(timeout));
	if (!ret)
		cnss_pr_err("Timeout waiting (%dms) for saving dump to file system\n",
			    timeout);

	return ret ? 0 : -ETIMEDOUT;
}
/* Since the elf32 and elf64 identification is identical apart from
 * the class, use elf32 by default.
 */
static void init_elf_identification(struct elf32_hdr *ehdr, unsigned char class)
{
	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = class;
	ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELFOSABI_NONE;
}

int cnss_qcom_elf_dump(struct list_head *segs, struct device *dev,
		       unsigned char class)
{
	struct cnss_qcom_dump_segment *segment = NULL;
	void *phdr, *ehdr = NULL;
	size_t data_size = 0, offset = 0;
	int phnum = 0;
	void *data = NULL;
	void __iomem *ptr = NULL;

	if (!segs || list_empty(segs))
		return -EINVAL;

	data_size = sizeof_elf_hdr(class);
	list_for_each_entry(segment, segs, node) {
		data_size += sizeof_elf_phdr(class) + segment->size;
		phnum++;
	}
	data = vmalloc(data_size);
	if (!data)
		return -ENOMEM;

	cnss_pr_vdbg("Creating ELF file with size %zu\n", data_size);

	ehdr = data;
	memset(ehdr, 0, sizeof_elf_hdr(class));
	init_elf_identification(ehdr, class);
	set_ehdr_property(ehdr, class, e_type, ET_CORE);
	set_ehdr_property(ehdr, class, e_machine, EM_NONE);
	set_ehdr_property(ehdr, class, e_version, EV_CURRENT);
	set_ehdr_property(ehdr, class, e_phoff, sizeof_elf_hdr(class));
	set_ehdr_property(ehdr, class, e_ehsize, sizeof_elf_hdr(class));
	set_ehdr_property(ehdr, class, e_phentsize, sizeof_elf_phdr(class));
	set_ehdr_property(ehdr, class, e_phnum, phnum);

	phdr = data + sizeof_elf_hdr(class);
	offset = sizeof_elf_hdr(class) + sizeof_elf_phdr(class) * phnum;
	list_for_each_entry(segment, segs, node) {
		memset(phdr, 0, sizeof_elf_phdr(class));
		set_phdr_property(phdr, class, p_type, PT_LOAD);
		set_phdr_property(phdr, class, p_offset, offset);
		set_phdr_property(phdr, class, p_vaddr, segment->da);
		set_phdr_property(phdr, class, p_paddr, segment->da);
		set_phdr_property(phdr, class, p_filesz, segment->size);
		set_phdr_property(phdr, class, p_memsz, segment->size);
		set_phdr_property(phdr, class, p_flags, PF_R | PF_W | PF_X);
		set_phdr_property(phdr, class, p_align, 0);

		if (segment->va) {
			memcpy(data + offset, segment->va, segment->size);
		} else {
			ptr = devm_ioremap(dev, segment->da, segment->size);
			if (!ptr) {
				cnss_pr_vdbg("Invalid coredump segment (%pad, %zu)\n",
					    &segment->da, segment->size);
				memset(data + offset, 0xff, segment->size);
			} else {
				memcpy_fromio(data + offset, ptr,
					      segment->size);
			}
		}

		offset += segment->size;
		phdr += sizeof_elf_phdr(class);
	}

	return cnss_qcom_devcd_dump(dev, data, data_size, GFP_KERNEL);
}

int cnss_qcn9000_ramdump(struct  cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct cnss_ramdump_info_v2 *info_v2 = &plat_priv->ramdump_info_v2;
	struct cnss_dump_data *dump_data = &info_v2->dump_data;
	struct cnss_dump_seg *dump_seg = info_v2->dump_data_vaddr;
	struct qcom_dump_segment *seg;
	struct cnss_dump_meta_info *meta_info;
	struct list_head head;
	int i, ret = 0, idx = 0;

	if (!info_v2->dump_data_valid || dump_data->nentries == 0)
		return ret;

	INIT_LIST_HEAD(&head);

	meta_info = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!meta_info) {
		ret = -ENOMEM;
		goto free_seg_list;
	}

	seg = kcalloc(1, sizeof(*seg), GFP_KERNEL);
	if (!seg) {
		ret = -ENOMEM;
		goto free_seg_list;
	}

	meta_info->magic = CNSS_RAMDUMP_MAGIC;
	meta_info->version = CNSS_RAMDUMP_VERSION_V2;
	meta_info->chipset = plat_priv->device_id;
	seg->va = meta_info;
	seg->size = sizeof(meta_info);
	list_add(&seg->node, &head);

	for (i = 0; i < dump_data->nentries; i++) {
		if (dump_seg->type >= CNSS_FW_DUMP_TYPE_MAX) {
			cnss_pr_err("Unsupported dump type: %d",
				    dump_seg->type);
			continue;
		}

		seg = kcalloc(1, sizeof(*seg), GFP_KERNEL);
		if (!seg) {
			ret = -ENOMEM;
			goto free_seg_list;
		}

		if (dump_seg->type != meta_info->entry[idx].type)
			idx++;

		if (meta_info->entry[dump_seg->type].entry_start == 0) {
			meta_info->entry[dump_seg->type].type = dump_seg->type;
			meta_info->entry[dump_seg->type].entry_start = i + 1;
		}
		meta_info->entry[dump_seg->type].entry_num++;
		seg->da = dump_seg->address;
		seg->va = dump_seg->v_address;
		seg->size = dump_seg->size;
		list_add_tail(&seg->node, &head);
		dump_seg++;
	}

	meta_info->total_entries = idx + 1;

	cnss_pr_dbg("Dumping meta_info: total_entries: %d",
		    meta_info->total_entries);
	for (i = 0; i < meta_info->total_entries; i++)
		cnss_pr_dbg("entry %d type %d entry_start %d entry_num %d",
			    i, meta_info->entry[i].type,
			    meta_info->entry[i].entry_start,
			    meta_info->entry[i].entry_num);

	ret = qcom_elf_dump(&head, info_v2->ramdump_dev, ELF_CLASS);
free_seg_list:
	while (!list_empty(&head)) {
		seg = list_first_entry(&head, struct qcom_dump_segment, node);
		list_del(&seg->node);
		kfree(seg);
	}

	return ret;
}
#endif

int cnss_pci_dev_powerup(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = NULL;

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}
	plat_priv = pci_priv->plat_priv;

	switch (pci_priv->device_id) {
	case QCA6174_DEVICE_ID:
		ret = cnss_qca6174_powerup(pci_priv);
		break;
	case QCN9000_DEVICE_ID:
	case QCN9224_DEVICE_ID:
	case QCA6390_DEVICE_ID:
	case QCA6490_DEVICE_ID:
		ret = cnss_qcn9000_powerup(pci_priv);
		break;
	default:
		cnss_pr_err("Unknown device_id found: 0x%x\n",
			    pci_priv->device_id);
		ret = -ENODEV;
	}

	return ret;
}

int cnss_pci_dev_shutdown(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = NULL;

	if (!pci_priv) {
		pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}
	plat_priv = pci_priv->plat_priv;

	switch (pci_priv->device_id) {
	case QCA6174_DEVICE_ID:
		ret = cnss_qca6174_shutdown(pci_priv);
		break;
	case QCN9000_DEVICE_ID:
	case QCN9224_DEVICE_ID:
	case QCA6390_DEVICE_ID:
	case QCA6490_DEVICE_ID:
		ret = cnss_qcn9000_shutdown(pci_priv);
		break;
	default:
		cnss_pr_err("Unknown device_id found: 0x%x\n",
			    pci_priv->device_id);
		ret = -ENODEV;
	}

	return ret;
}

int cnss_pci_dev_crash_shutdown(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = NULL;

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}
	plat_priv = pci_priv->plat_priv;

	switch (pci_priv->device_id) {
	case QCA6174_DEVICE_ID:
		cnss_qca6174_crash_shutdown(pci_priv);
		break;
	case QCN9000_DEVICE_ID:
	case QCN9224_DEVICE_ID:
	case QCA6390_DEVICE_ID:
	case QCA6490_DEVICE_ID:
		cnss_qcn9000_crash_shutdown(pci_priv);
		break;
	default:
		cnss_pr_err("Unknown device_id found: 0x%x\n",
			    pci_priv->device_id);
		ret = -ENODEV;
	}

	return ret;
}

int cnss_pci_dev_ramdump(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = NULL;

	if (!pci_priv) {
		pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}
	plat_priv = pci_priv->plat_priv;

	switch (pci_priv->device_id) {
	case QCA6174_DEVICE_ID:
		ret = cnss_qca6174_ramdump(pci_priv);
		break;
	case QCN9000_DEVICE_ID:
	case QCN9224_DEVICE_ID:
	case QCA6390_DEVICE_ID:
	case QCA6490_DEVICE_ID:
		ret = cnss_qcn9000_ramdump(pci_priv);
		if (ret)
			cnss_pr_err("Failed to collect ramdump for %s\n",
				    plat_priv->device_name);
		cnss_pci_clear_dump_info(pci_priv);

		/* Shutdown was skipped in cnss_qcn9000_shutdown path
		 * earlier in target assert case to finish the ramdump
		 * collection.
		 * Now after ramdump is complete, shutdown the target
		 * if recovery is enabled, else wifi driver will take
		 * care of assert.
		 */
		if (plat_priv->recovery_enabled) {
			ret = cnss_qcn9000_shutdown(pci_priv);
			if (ret)
				cnss_pr_err("Failed to shutdown %s\n",
					    plat_priv->device_name);
		}
		break;
	default:
		cnss_pr_err("Unknown device_id found: 0x%x\n",
			    pci_priv->device_id);
		ret = -ENODEV;
	}

	return ret;
}

int cnss_pci_is_drv_connected(struct device *dev)
{
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(to_pci_dev(dev));

	if (!pci_priv)
		return -ENODEV;

	return pci_priv->drv_connected_last;
}
EXPORT_SYMBOL(cnss_pci_is_drv_connected);
#ifdef PCI_CNSS_WLAN_REGISTER_DRIVER
static int cnss_wlan_register_driver(struct cnss_wlan_driver *driver_ops)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = cnss_bus_dev_to_plat_priv(NULL);
	struct cnss_pci_data *pci_priv;
	unsigned int timeout;
	struct cnss_cal_info *cal_info;

	if (!plat_priv) {
		cnss_pr_err("plat_priv is NULL\n");
		return -ENODEV;
	}

	pci_priv = plat_priv->bus_priv;
	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}

	if (pci_priv->driver_ops) {
		cnss_pr_err("Driver has already registered\n");
		return -EEXIST;
	}

	if (!test_bit(CNSS_COLD_BOOT_CAL, &plat_priv->driver_state))
		goto register_driver;

	cal_info = kzalloc(sizeof(*cal_info), GFP_KERNEL);
	if (!cal_info)
		return -ENOMEM;

	cnss_pr_dbg("Start to wait for calibration to complete\n");

	timeout = cnss_get_boot_timeout(&pci_priv->pci_dev->dev);
	ret = wait_for_completion_timeout(&plat_priv->cal_complete,
					  msecs_to_jiffies(timeout));
	if (!ret) {
		cnss_pr_err("Timeout waiting for calibration to complete\n");
		cal_info->cal_status = CNSS_CAL_TIMEOUT;
		cnss_driver_event_post(plat_priv,
				       CNSS_DRIVER_EVENT_COLD_BOOT_CAL_DONE,
				       0, cal_info);
	}

register_driver:
	ret = cnss_driver_event_post(plat_priv,
				     CNSS_DRIVER_EVENT_REGISTER_DRIVER,
				     CNSS_EVENT_SYNC_UNINTERRUPTIBLE,
				     driver_ops);

	return ret;
}
EXPORT_SYMBOL(cnss_wlan_register_driver);

static void cnss_wlan_unregister_driver(struct cnss_wlan_driver *driver_ops)
{
	struct cnss_plat_data *plat_priv = cnss_bus_dev_to_plat_priv(NULL);
	int ret = 0;

	if (!plat_priv) {
		cnss_pr_err("plat_priv is NULL\n");
		return;
	}

	if (!test_bit(CNSS_DRIVER_RECOVERY, &plat_priv->driver_state) &&
	    !test_bit(CNSS_DEV_ERR_NOTIFY, &plat_priv->driver_state))
		goto skip_wait;

	reinit_completion(&plat_priv->recovery_complete);
	ret = wait_for_completion_timeout(&plat_priv->recovery_complete,
					  RECOVERY_TIMEOUT);
	if (!ret) {
		cnss_pr_err("Timeout waiting for recovery to complete\n");
		CNSS_ASSERT(0);
	}

skip_wait:
	cnss_driver_event_post(plat_priv,
			       CNSS_DRIVER_EVENT_UNREGISTER_DRIVER,
			       CNSS_EVENT_SYNC_UNINTERRUPTIBLE, NULL);
}
EXPORT_SYMBOL(cnss_wlan_unregister_driver);
#endif
int cnss_pci_register_driver_hdlr(struct cnss_pci_data *pci_priv,
				  void *data)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	set_bit(CNSS_DRIVER_LOADING, &plat_priv->driver_state);
	pci_priv->driver_ops = data;

	ret = cnss_pci_dev_powerup(pci_priv);
	if (ret) {
		clear_bit(CNSS_DRIVER_LOADING, &plat_priv->driver_state);
		pci_priv->driver_ops = NULL;
	}

	return ret;
}

int cnss_pci_unregister_driver_hdlr(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	set_bit(CNSS_DRIVER_UNLOADING, &plat_priv->driver_state);
	cnss_pci_dev_shutdown(pci_priv);
	pci_priv->driver_ops = NULL;

	return 0;
}

#ifdef CONFIG_CNSS2_PCI_MSM
static bool cnss_pci_is_drv_supported(struct cnss_pci_data *pci_priv)
{
	struct pci_dev *root_port = pci_find_pcie_root_port(pci_priv->pci_dev);
	struct device_node *root_of_node = root_port->dev.of_node;
	bool drv_supported = false;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	if (root_of_node->parent)
		drv_supported = of_property_read_bool(root_of_node->parent,
						      "qcom,drv-supported");

	cnss_pr_dbg("PCIe DRV is %s\n",
		    drv_supported ? "supported" : "not supported");

	return drv_supported;
}

static void cnss_pci_event_cb(struct msm_pcie_notify *notify)
{
	unsigned long flags;
	struct pci_dev *pci_dev;
	struct cnss_pci_data *pci_priv;
	struct cnss_plat_data *plat_priv;

	if (!notify)
		return;

	pci_dev = notify->user;
	if (!pci_dev)
		return;

	pci_priv = cnss_get_pci_priv(pci_dev);
	if (!pci_priv)
		return;

	plat_priv = pci_priv->plat_priv;
	switch (notify->event) {
	case MSM_PCIE_EVENT_LINKDOWN:
		if (test_bit(ENABLE_PCI_LINK_DOWN_PANIC,
			     &plat_priv->ctrl_params.quirks))
			panic("cnss: PCI link is down!\n");

		spin_lock_irqsave(&pci_link_down_lock, flags);
		if (pci_priv->pci_link_down_ind) {
			cnss_pr_dbg("PCI link down recovery is in progress, ignore!\n");
			spin_unlock_irqrestore(&pci_link_down_lock, flags);
			return;
		}
		pci_priv->pci_link_down_ind = true;
		spin_unlock_irqrestore(&pci_link_down_lock, flags);

		cnss_fatal_err("PCI link down, schedule recovery!\n");
		if (pci_dev->device == QCA6174_DEVICE_ID)
			disable_irq(pci_dev->irq);
		cnss_schedule_recovery(&pci_dev->dev, CNSS_REASON_LINK_DOWN);
		break;
	case MSM_PCIE_EVENT_WAKEUP:
		if (cnss_pci_get_monitor_wake_intr(pci_priv) &&
		    cnss_pci_get_auto_suspended(pci_priv)) {
			cnss_pci_set_monitor_wake_intr(pci_priv, false);
			cnss_pci_pm_request_resume(pci_priv);
		}
		break;
	case MSM_PCIE_EVENT_DRV_CONNECT:
		cnss_pr_dbg("DRV subsystem is connected\n");
		cnss_pci_set_drv_connected(pci_priv, 1);
		break;
	case MSM_PCIE_EVENT_DRV_DISCONNECT:
		cnss_pr_dbg("DRV subsystem is disconnected\n");
		cnss_pci_set_drv_connected(pci_priv, 0);
		break;
	default:
		cnss_pr_err("Received invalid PCI event: %d\n", notify->event);
	}
}

static int cnss_reg_pci_event(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct msm_pcie_register_event *pci_event;

	pci_event = &pci_priv->msm_pci_event;
	pci_event->events = MSM_PCIE_EVENT_LINKDOWN |
		MSM_PCIE_EVENT_WAKEUP;

	if (cnss_pci_is_drv_supported(pci_priv))
		pci_event->events = pci_event->events |
			MSM_PCIE_EVENT_DRV_CONNECT |
			MSM_PCIE_EVENT_DRV_DISCONNECT;

	pci_event->user = pci_priv->pci_dev;
	pci_event->mode = MSM_PCIE_TRIGGER_CALLBACK;
	pci_event->callback = cnss_pci_event_cb;
	pci_event->options = MSM_PCIE_CONFIG_NO_RECOVERY;

	ret = msm_pcie_register_event(pci_event);
	if (ret)
		cnss_pr_err("Failed to register MSM PCI event, err = %d\n",
			    ret);

	return ret;
}

static void cnss_dereg_pci_event(struct cnss_pci_data *pci_priv)
{
	msm_pcie_deregister_event(&pci_priv->msm_pci_event);
}
#endif

#ifdef CONFIG_PM_SLEEP
static int cnss_pci_suspend(struct device *dev)
{
#ifdef CONFIG_PCI_SUSPENDRESUME
	int ret = 0;
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	struct cnss_plat_data *plat_priv;
	struct cnss_wlan_driver *driver_ops;

	pm_message_t state = { .event = PM_EVENT_SUSPEND };

	if (!pci_priv)
		goto out;

	plat_priv = pci_priv->plat_priv;
	if (!plat_priv)
		goto out;

	if (!cnss_is_device_powered_on(plat_priv))
		goto out;

	set_bit(CNSS_IN_SUSPEND_RESUME, &plat_priv->driver_state);

	if (!test_bit(DISABLE_DRV, &plat_priv->ctrl_params.quirks))
		pci_priv->drv_connected_last =
			cnss_pci_get_drv_connected(pci_priv);

	driver_ops = pci_priv->driver_ops;
	if (driver_ops && driver_ops->suspend) {
		ret = driver_ops->suspend(pci_dev, state);
		if (ret) {
			cnss_pr_err("Failed to suspend host driver, err = %d\n",
				    ret);
			ret = -EAGAIN;
			goto clear_flag;
		}
	}

	if (pci_priv->pci_link_state == PCI_LINK_UP && !pci_priv->disable_pc) {
		if (cnss_pci_set_mhi_state(pci_priv, CNSS_MHI_SUSPEND)) {
			ret = -EAGAIN;
			goto resume_driver;
		}

		if (pci_priv->drv_connected_last)
			goto skip_disable_pci;

		pci_clear_master(pci_dev);
		cnss_set_pci_config_space(pci_priv, SAVE_PCI_CONFIG_SPACE);
		pci_disable_device(pci_dev);

		ret = pci_set_power_state(pci_dev, PCI_D3hot);
		if (ret)
			cnss_pr_err("Failed to set D3Hot, err = %d\n", ret);

skip_disable_pci:
		if (cnss_set_pci_link(pci_priv, PCI_LINK_DOWN)) {
			ret = -EAGAIN;
			goto resume_mhi;
		}
		pci_priv->pci_link_state = PCI_LINK_DOWN;
	}

	cnss_pci_set_monitor_wake_intr(pci_priv, false);

	return 0;

resume_mhi:
	if (pci_enable_device(pci_dev))
		cnss_pr_err("Failed to enable PCI device\n");
	if (pci_priv->saved_state)
		cnss_set_pci_config_space(pci_priv, RESTORE_PCI_CONFIG_SPACE);
	pci_set_master(pci_dev);
	cnss_pci_set_mhi_state(pci_priv, CNSS_MHI_RESUME);
resume_driver:
	if (driver_ops && driver_ops->resume)
		driver_ops->resume(pci_dev);
clear_flag:
	pci_priv->drv_connected_last = 0;
	clear_bit(CNSS_IN_SUSPEND_RESUME, &plat_priv->driver_state);
out:
	return ret;
#endif
	return 0;
}

static int cnss_pci_resume(struct device *dev)
{
#ifdef CONFIG_PCI_SUSPENDRESUME
	int ret = 0;
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	struct cnss_plat_data *plat_priv;
	struct cnss_wlan_driver *driver_ops;

	if (!pci_priv)
		goto out;

	plat_priv = pci_priv->plat_priv;
	if (!plat_priv)
		goto out;

	if (pci_priv->pci_link_down_ind)
		goto out;

	if (!cnss_is_device_powered_on(pci_priv->plat_priv))
		goto out;

	if (pci_priv->pci_link_state == PCI_LINK_DOWN &&
	    !pci_priv->disable_pc) {
		if (cnss_set_pci_link(pci_priv, PCI_LINK_UP)) {
			cnss_fatal_err("Failed to resume PCI link from suspend\n");
			cnss_pci_link_down(dev);
			ret = -EAGAIN;
			goto out;
		}
		pci_priv->pci_link_state = PCI_LINK_UP;

		if (pci_priv->drv_connected_last)
			goto skip_enable_pci;

		ret = pci_enable_device(pci_dev);
		if (ret)
			cnss_pr_err("Failed to enable PCI device, err = %d\n",
				    ret);

		if (pci_priv->saved_state)
			cnss_set_pci_config_space(pci_priv,
						  RESTORE_PCI_CONFIG_SPACE);
		pci_set_master(pci_dev);

skip_enable_pci:
		cnss_pci_set_mhi_state(pci_priv, CNSS_MHI_RESUME);
	}

	driver_ops = pci_priv->driver_ops;
	if (driver_ops && driver_ops->resume) {
		ret = driver_ops->resume(pci_dev);
		if (ret)
			cnss_pr_err("Failed to resume host driver, err = %d\n",
				    ret);
	}

	pci_priv->drv_connected_last = 0;
	clear_bit(CNSS_IN_SUSPEND_RESUME, &plat_priv->driver_state);

out:
	return ret;
#endif
	return 0;
}

static int cnss_pci_suspend_noirq(struct device *dev)
{
#ifdef CONFIG_PCI_SUSPENDRESUME
	int ret = 0;
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	struct cnss_wlan_driver *driver_ops;

	if (!pci_priv)
		goto out;

	driver_ops = pci_priv->driver_ops;
	if (driver_ops && driver_ops->suspend_noirq)
		ret = driver_ops->suspend_noirq(pci_dev);

out:
	return ret;
#endif
	return 0;
}

static int cnss_pci_resume_noirq(struct device *dev)
{
#ifdef CONFIG_PCI_SUSPENDRESUME
	int ret = 0;
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	struct cnss_wlan_driver *driver_ops;

	if (!pci_priv)
		goto out;

	driver_ops = pci_priv->driver_ops;
	if (driver_ops && driver_ops->resume_noirq &&
	    !pci_priv->pci_link_down_ind)
		ret = driver_ops->resume_noirq(pci_dev);

out:
	return ret;
#endif
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int cnss_pci_runtime_suspend(struct device *dev)
{
#ifdef CONFIG_PCI_SUSPENDRESUME
	int ret = 0;
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	struct cnss_plat_data *plat_priv;
	struct cnss_wlan_driver *driver_ops;

	if (!pci_priv)
		return -EAGAIN;

	plat_priv = pci_priv->plat_priv;
	if (!plat_priv)
		return -EAGAIN;

	if (!cnss_is_device_powered_on(pci_priv->plat_priv))
		return -EAGAIN;

	if (pci_priv->pci_link_down_ind) {
		cnss_pr_dbg("PCI link down recovery is in progress!\n");
		return -EAGAIN;
	}

	cnss_pr_vdbg("Runtime suspend start\n");

	if (!test_bit(DISABLE_DRV, &plat_priv->ctrl_params.quirks))
		pci_priv->drv_connected_last =
			cnss_pci_get_drv_connected(pci_priv);

	driver_ops = pci_priv->driver_ops;
	if (driver_ops && driver_ops->runtime_ops &&
	    driver_ops->runtime_ops->runtime_suspend)
		ret = driver_ops->runtime_ops->runtime_suspend(pci_dev);
	else
		ret = cnss_auto_suspend(dev);

	if (ret)
		pci_priv->drv_connected_last = 0;

	cnss_pr_vdbg("Runtime suspend status: %d\n", ret);

	return ret;
#endif
	return 0;
}

static int cnss_pci_runtime_resume(struct device *dev)
{
#ifdef CONFIG_PCI_SUSPENDRESUME
	int ret = 0;
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	struct cnss_wlan_driver *driver_ops;

	if (!pci_priv)
		return -EAGAIN;

	if (!cnss_is_device_powered_on(pci_priv->plat_priv))
		return -EAGAIN;

	if (pci_priv->pci_link_down_ind) {
		cnss_pr_dbg("PCI link down recovery is in progress!\n");
		return -EAGAIN;
	}

	cnss_pr_vdbg("Runtime resume start\n");

	driver_ops = pci_priv->driver_ops;
	if (driver_ops && driver_ops->runtime_ops &&
	    driver_ops->runtime_ops->runtime_resume)
		ret = driver_ops->runtime_ops->runtime_resume(pci_dev);
	else
		ret = cnss_auto_resume(dev);

	if (!ret)
		pci_priv->drv_connected_last = 0;

	cnss_pr_vdbg("Runtime resume status: %d\n", ret);

	return ret;
#endif
	return 0;
}

static int cnss_pci_runtime_idle(struct device *dev)
{
#ifdef CONFIG_PCI_SUSPENDRESUME
	cnss_pr_vdbg("Runtime idle\n");

	pm_request_autosuspend(dev);

	return -EBUSY;
#endif
	return 0;
}
#endif /* CONFIG_PM */

int cnss_wlan_pm_control(struct device *dev, bool vote)
{
#ifdef CONFIG_PCI_PM
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	int ret = 0;

	if (!pci_priv)
		return -ENODEV;

	ret = msm_pcie_pm_control(vote ? MSM_PCIE_DISABLE_PC :
				  MSM_PCIE_ENABLE_PC,
				  pci_dev->bus->number, pci_dev,
				  NULL, PM_OPTIONS_DEFAULT);
	if (ret)
		return ret;

	pci_priv->disable_pc = vote;
	cnss_pr_dbg("%s PCIe power collapse\n", vote ? "disable" : "enable");
#endif
	return 0;
}
EXPORT_SYMBOL(cnss_wlan_pm_control);

void cnss_pci_pm_runtime_show_usage_count(struct cnss_pci_data *pci_priv)
{
#ifdef CONFIG_PCI_PM
	struct device *dev;

	if (!pci_priv)
		return;

	dev = &pci_priv->pci_dev->dev;

	cnss_pr_dbg("Runtime PM usage count: %d\n",
		    atomic_read(&dev->power.usage_count));
#endif
}

int cnss_pci_pm_request_resume(struct cnss_pci_data *pci_priv)
{
#ifdef CONFIG_PCI_PM
	struct pci_dev *pci_dev;

	if (!pci_priv)
		return -ENODEV;

	pci_dev = pci_priv->pci_dev;
	if (!pci_dev)
		return -ENODEV;

	return pm_request_resume(&pci_dev->dev);
#endif
	return 0;
}

int cnss_pci_pm_runtime_resume(struct cnss_pci_data *pci_priv)
{
	struct pci_dev *pci_dev;

	if (!pci_priv)
		return -ENODEV;

	pci_dev = pci_priv->pci_dev;
	if (!pci_dev)
		return -ENODEV;

	return pm_runtime_resume(&pci_dev->dev);
}

int cnss_pci_pm_runtime_get(struct cnss_pci_data *pci_priv)
{
	if (!pci_priv)
		return -ENODEV;

	return pm_runtime_get(&pci_priv->pci_dev->dev);
}

int cnss_pci_pm_runtime_get_sync(struct cnss_pci_data *pci_priv)
{
	if (!pci_priv)
		return -ENODEV;

	return pm_runtime_get_sync(&pci_priv->pci_dev->dev);
}

void cnss_pci_pm_runtime_get_noresume(struct cnss_pci_data *pci_priv)
{
	if (!pci_priv)
		return;

	return pm_runtime_get_noresume(&pci_priv->pci_dev->dev);
}

int cnss_pci_pm_runtime_put_autosuspend(struct cnss_pci_data *pci_priv)
{
	if (!pci_priv)
		return -ENODEV;

	return pm_runtime_put_autosuspend(&pci_priv->pci_dev->dev);
}

void cnss_pci_pm_runtime_put_noidle(struct cnss_pci_data *pci_priv)
{
	if (!pci_priv)
		return;

	pm_runtime_put_noidle(&pci_priv->pci_dev->dev);
}

void cnss_pci_pm_runtime_mark_last_busy(struct cnss_pci_data *pci_priv)
{
	if (!pci_priv)
		return;

	pm_runtime_mark_last_busy(&pci_priv->pci_dev->dev);
}

int cnss_auto_suspend(struct device *dev)
{
#ifdef CONFIG_PCI_SUSPENDRESUME
	int ret = 0;
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	struct cnss_plat_data *plat_priv;
	struct cnss_bus_bw_info *bus_bw_info;

	if (!pci_priv)
		return -ENODEV;

	plat_priv = pci_priv->plat_priv;
	if (!plat_priv)
		return -ENODEV;

	if (pci_priv->pci_link_state == PCI_LINK_UP) {
		if (cnss_pci_set_mhi_state(pci_priv, CNSS_MHI_SUSPEND)) {
			ret = -EAGAIN;
			goto out;
		}

		if (pci_priv->drv_connected_last)
			goto skip_disable_pci;

		pci_clear_master(pci_dev);
		cnss_set_pci_config_space(pci_priv, SAVE_PCI_CONFIG_SPACE);
		pci_disable_device(pci_dev);

		ret = pci_set_power_state(pci_dev, PCI_D3hot);
		if (ret)
			cnss_pr_err("Failed to set D3Hot, err =  %d\n", ret);

skip_disable_pci:
		if (cnss_set_pci_link(pci_priv, PCI_LINK_DOWN)) {
			ret = -EAGAIN;
			goto resume_mhi;
		}

		pci_priv->pci_link_state = PCI_LINK_DOWN;
	}

	cnss_pci_set_auto_suspended(pci_priv, 1);
	cnss_pci_set_monitor_wake_intr(pci_priv, true);

	bus_bw_info = &plat_priv->bus_bw_info;
	msm_bus_scale_client_update_request(bus_bw_info->bus_client,
					    CNSS_BUS_WIDTH_NONE);

	return 0;

resume_mhi:
	if (pci_enable_device(pci_dev))
		cnss_pr_err("Failed to enable PCI device!\n");
	cnss_pci_set_mhi_state(pci_priv, CNSS_MHI_RESUME);
out:
	return ret;
#endif
	return 0;
}
EXPORT_SYMBOL(cnss_auto_suspend);

int cnss_auto_resume(struct device *dev)
{
#ifdef CONFIG_PCI_SUSPENDRESUME
	int ret = 0;
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	struct cnss_plat_data *plat_priv;
	struct cnss_bus_bw_info *bus_bw_info;

	if (!pci_priv)
		return -ENODEV;

	plat_priv = pci_priv->plat_priv;
	if (!plat_priv)
		return -ENODEV;

	if (pci_priv->pci_link_state == PCI_LINK_DOWN) {
		if (cnss_set_pci_link(pci_priv, PCI_LINK_UP)) {
			cnss_fatal_err("Failed to resume PCI link from suspend\n");
			cnss_pci_link_down(dev);
			ret = -EAGAIN;
			goto out;
		}
		pci_priv->pci_link_state = PCI_LINK_UP;

		if (pci_priv->drv_connected_last)
			goto skip_enable_pci;

		ret = pci_enable_device(pci_dev);
		if (ret)
			cnss_pr_err("Failed to enable PCI device, err = %d\n",
				    ret);

		cnss_set_pci_config_space(pci_priv, RESTORE_PCI_CONFIG_SPACE);
		pci_set_master(pci_dev);

skip_enable_pci:
		cnss_pci_set_mhi_state(pci_priv, CNSS_MHI_RESUME);
	}

	cnss_pci_set_auto_suspended(pci_priv, 0);

	bus_bw_info = &plat_priv->bus_bw_info;
	msm_bus_scale_client_update_request(bus_bw_info->bus_client,
					    bus_bw_info->current_bw_vote);
out:
	return ret;
#endif
	return 0;
}
EXPORT_SYMBOL(cnss_auto_resume);

int cnss_pci_force_wake_request(struct device *dev)
{
#ifdef FORCEWAKE
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	struct cnss_plat_data *plat_priv;
	struct mhi_controller *mhi_ctrl;

	if (!pci_priv)
		return -ENODEV;

	if (pci_priv->device_id != QCA6390_DEVICE_ID &&
	    pci_priv->device_id != QCA6490_DEVICE_ID)
		return 0;

	mhi_ctrl = pci_priv->mhi_ctrl;
	if (!mhi_ctrl)
		return -EINVAL;

	plat_priv = pci_priv->plat_priv;
	if (!plat_priv)
		return -ENODEV;

	if (test_bit(CNSS_DEV_ERR_NOTIFY, &plat_priv->driver_state))
		return -EAGAIN;

	mhi_device_get(mhi_ctrl->mhi_dev, MHI_VOTE_DEVICE);
#endif

	return 0;
}
EXPORT_SYMBOL(cnss_pci_force_wake_request);

static int cnss_pci_get_user_msi_assignment(struct device *dev, char *user_name,
				 int *num_vectors, u32 *user_base_data,
				 u32 *base_vector)
{
	int idx;
	u32 msi_ep_base_data = 0;
	struct pci_dev *pci_dev = NULL;
	struct cnss_pci_data *pci_priv = NULL;
	struct cnss_msi_config *msi_config = NULL;
	struct cnss_plat_data *plat_priv = cnss_bus_dev_to_plat_priv(dev);

	if (!plat_priv)
		return -ENODEV;

	pci_dev = to_pci_dev(dev);
	pci_priv = cnss_get_pci_priv(pci_dev);
	if (!pci_priv) {
		cnss_pr_err("pci_priv NULL");
		return -ENODEV;
	}

	msi_config = pci_priv->msi_config;
	if (!msi_config) {
		cnss_pr_err("MSI is not supported.\n");
		return -EINVAL;
	}
	msi_ep_base_data = pci_priv->msi_ep_base_data;

	for (idx = 0; idx < msi_config->total_users; idx++) {
		if (strcmp(user_name, msi_config->users[idx].name) == 0) {
			*num_vectors = msi_config->users[idx].num_vectors;
			*user_base_data = msi_config->users[idx].base_vector +
					  msi_ep_base_data;
			*base_vector = msi_config->users[idx].base_vector;

			cnss_pr_dbg("Assign MSI to user: %s, num_vectors: %d, user_base_data: %u, base_vector: %u\n",
				    user_name, *num_vectors, *user_base_data,
				    *base_vector);

			return 0;
		}
	}

	cnss_pr_err("Failed to find MSI assignment for %s!\n", user_name);

	return -EINVAL;
}

static int cnss_pci_get_msi_irq(struct device *dev, unsigned int vector)
{
	int irq_num = 0;
	struct pci_dev *pci_dev = NULL;

	pci_dev = to_pci_dev(dev);
	irq_num = pci_irq_vector(pci_dev, vector);

	return irq_num;
}

int cnss_pci_is_device_awake(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	struct mhi_controller *mhi_ctrl;

	if (!pci_priv)
		return -ENODEV;

	if (pci_priv->device_id != QCA6390_DEVICE_ID &&
	    pci_priv->device_id != QCA6490_DEVICE_ID)
		return true;

	mhi_ctrl = pci_priv->mhi_ctrl;
	if (!mhi_ctrl)
		return -EINVAL;

	return (mhi_ctrl->dev_state == MHI_STATE_M0);
}
EXPORT_SYMBOL(cnss_pci_is_device_awake);

int cnss_pci_force_wake_release(struct device *dev)
{
#ifdef FORCEWAKE
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	struct cnss_plat_data *plat_priv;
	struct mhi_controller *mhi_ctrl;

	if (!pci_priv)
		return -ENODEV;

	if (pci_priv->device_id != QCA6390_DEVICE_ID &&
	    pci_priv->device_id != QCA6490_DEVICE_ID)
		return 0;

	mhi_ctrl = pci_priv->mhi_ctrl;
	if (!mhi_ctrl)
		return -EINVAL;

	plat_priv = pci_priv->plat_priv;
	if (!plat_priv)
		return -ENODEV;

	if (test_bit(CNSS_DEV_ERR_NOTIFY, &plat_priv->driver_state))
		return -EAGAIN;

	mhi_device_put(mhi_ctrl->mhi_dev, MHI_VOTE_DEVICE);
#endif

	return 0;
}
EXPORT_SYMBOL(cnss_pci_force_wake_release);

int cnss_pci_alloc_fw_mem(struct cnss_plat_data *plat_priv)
{
	struct cnss_fw_mem *fw_mem = plat_priv->fw_mem;
	u32 addr = 0;
	u32 hremote_size = 0;
	u32 caldb_size = 0;
	u32 pageable_size = 0;
	struct device *dev;
	int i;
	struct cnss_pci_data *pci_priv = plat_priv->bus_priv;
	struct pci_dev *pci_dev = (struct pci_dev *)plat_priv->pci_dev;
	int ret;

	dev = &plat_priv->plat_dev->dev;

	if (plat_priv->dma_alloc_supported) {
		for (i = 0; i < plat_priv->fw_mem_seg_len; i++) {
			if (fw_mem[i].type ==
					QMI_WLFW_MLO_GLOBAL_MEM_V01 &&
					fw_mem[i].size) {
				ret = cnss_mlo_mem_alloc(plat_priv, i);
				if (ret != 0) {
					cnss_pr_err("Error(%d): mlo memory alloc failed.\n",
							ret);
					return ret;
				}
			}

			if (!fw_mem[i].va && fw_mem[i].size) {
				if (((fw_mem[i].type ==
					QMI_WLFW_MEM_CAL_V01) &&
					(!plat_priv->cold_boot_support)) ||
					(fw_mem[i].type ==
						QMI_WLFW_MLO_GLOBAL_MEM_V01)) {
					continue;
				}

				fw_mem[i].va =
					dma_alloc_attrs(&pci_dev->dev,
							fw_mem[i].size,
							&fw_mem[i].pa,
							GFP_KERNEL,
					    DMA_ATTR_FORCE_CONTIGUOUS);

				if (!fw_mem[i].va) {
					cnss_pr_err("Failed to allocate memory for FW, size: 0x%zx, type: %u\n",
							fw_mem[i].size,
							fw_mem[i].type);
					return -ENOMEM;
				}
			}
		}

		return 0;
	}

	/* IPQ Reserved memory model */
	for (i = 0; i < plat_priv->fw_mem_seg_len; i++) {
		switch (fw_mem[i].type) {
		case QMI_WLFW_MEM_CAL_V01:
			if (!plat_priv->cold_boot_support)
				break;

			if (of_property_read_u32(dev->of_node,
						 "caldb-size",
						 &caldb_size)) {
				cnss_pr_err("Error: No caldb-size in dts\n");
				CNSS_ASSERT(0);
				return -ENOMEM;
			}
			if (fw_mem[i].size > caldb_size) {
				cnss_pr_err("Error: Need more memory for caldb, fw req:0x%x max:0x%x\n",
					    (unsigned int)fw_mem[i].size,
					    caldb_size);
				CNSS_ASSERT(0);
			}
			if (of_property_read_u32(dev->of_node,
						 "caldb-addr",
						 &addr)) {
				cnss_pr_err("Error: No caldb-addr in dts\n");
				CNSS_ASSERT(0);
				return -ENOMEM;
			}
			fw_mem[i].pa = (phys_addr_t)addr;
			fw_mem[i].va = ioremap(fw_mem[i].pa, fw_mem[i].size);
			if (!fw_mem[i].va)
				cnss_pr_err("WARNING caldb remap failed\n");
			break;
		case QMI_WLFW_MEM_TYPE_DDR_V01:
			if (of_property_read_u32(dev->of_node,
						 "hremote-size",
						 &hremote_size)) {
				cnss_pr_err("Error: No hremote-size in dts\n\n");
				CNSS_ASSERT(0);
				return -ENOMEM;
			}

			if (fw_mem[i].size > hremote_size) {
				cnss_pr_err("Error: Need more memory %x\n",
					    (unsigned int)fw_mem[i].size);
				CNSS_ASSERT(0);
			}
			if (fw_mem[i].size < hremote_size) {
				cnss_pr_err("WARNING: More memory is reserved. Reserved size 0x%x, Requested size 0x%x.\n",
					    hremote_size,
					    (unsigned int)fw_mem[i].size);
			}
			if (of_property_read_u32(dev->of_node,
						 "base-addr",
						 &addr)) {
				cnss_pr_err("Error: No base-addr in dts\n");
				CNSS_ASSERT(0);
				return -ENOMEM;
			}
			fw_mem[i].pa = (phys_addr_t)addr;
			fw_mem[i].va = ioremap(fw_mem[i].pa, fw_mem[i].size);
			if (!fw_mem[i].va)
				cnss_pr_err("WARNING: Host DDR remap failed\n");
			break;
		case QMI_WLFW_MEM_M3_V01:
			if (fw_mem[i].size > Q6_M3_DUMP_SIZE_QCN9000) {
				cnss_pr_err("Error: Need more memory %x\n",
					    (unsigned int)fw_mem[i].size);
				CNSS_ASSERT(0);
			}
			if (of_property_read_u32(dev->of_node,
						 "m3-dump-addr",
						 &addr)) {
				cnss_pr_err("Error: No m3-dump-addr in dts\n");
				CNSS_ASSERT(0);
				return -ENOMEM;
			}
			fw_mem[i].pa = (phys_addr_t)addr;
			fw_mem[i].va = ioremap(fw_mem[i].pa, fw_mem[i].size);
			if (!fw_mem[i].va)
				cnss_pr_err("WARNING: M3 Dump addr remap failed\n");
			break;
		case QMI_WLFW_PAGEABLE_MEM_V01:
			if (of_property_read_u32(dev->of_node,
						 "pageable-size",
						 &pageable_size)) {
				cnss_pr_err("Error: No pageable-size in dts\n");
				CNSS_ASSERT(0);
				return -ENOMEM;
			}

			if (fw_mem[i].size > pageable_size) {
				cnss_pr_err("Error: Need more memory. Reserved size 0x%x, Requested size 0x%x.\n",
					    pageable_size,
					    (unsigned int)fw_mem[i].size);
				CNSS_ASSERT(0);
				return -ENOMEM;
			}
			if (fw_mem[i].size < pageable_size) {
				cnss_pr_warn("More memory is reserved. Reserved size 0x%x, Requested size 0x%x.\n",
					     pageable_size,
					     (unsigned int)fw_mem[i].size);
			}
			if (of_property_read_u32(dev->of_node,
						 "pageable-addr",
						 &addr)) {
				cnss_pr_err("Error: No pageable-addr in dts\n");
				CNSS_ASSERT(0);
				return -ENOMEM;
			}

			fw_mem[i].pa = (phys_addr_t)addr;
			fw_mem[i].va = ioremap(fw_mem[i].pa, fw_mem[i].size);
			if (!fw_mem[i].va) {
				cnss_pr_err("WARNING: Host DDR remap failed\n");
				CNSS_ASSERT(0);
				return -ENOMEM;
			}
			break;
		case QMI_WLFW_AFC_MEM_V01:
			if (fw_mem[i].size != AFC_MEM_SIZE) {
				cnss_pr_err("Error: less AFC mem req: 0x%x\n",
					    (unsigned int)fw_mem[i].size);
				CNSS_ASSERT(0);
			}
			if (fw_mem[i].va) {
				afc_memset(plat_priv, fw_mem[i].va, 0,
					   fw_mem[i].size);
				break;
			}

			fw_mem[i].va =
				dma_alloc_coherent(&pci_priv->pci_dev->dev,
						   fw_mem[i].size,
						   &fw_mem[i].pa, GFP_KERNEL);
			if (!fw_mem[i].va) {
				cnss_pr_err("AFC mem allocation failed\n");
				fw_mem[i].pa = 0;
				CNSS_ASSERT(0);
				return -ENOMEM;
			}
			break;
		case QMI_WLFW_MLO_GLOBAL_MEM_V01:
			cnss_mlo_mem_alloc(plat_priv, i);
			break;
		default:
			cnss_pr_err("Ignore mem req type %d\n", fw_mem[i].type);
			break;
		}
	}

	return 0;
}

int cnss_pci_alloc_qdss_mem(struct cnss_pci_data *pci_priv)
{
	u32 i, addr = 0;
	struct cnss_fw_mem *qdss_mem;
	struct device *dev;
	struct pci_dev *pci_dev;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	if (!plat_priv)
		return -ENODEV;

	qdss_mem = plat_priv->qdss_mem;
	dev = &plat_priv->plat_dev->dev;
	pci_dev = (struct pci_dev *)plat_priv->pci_dev;

	if (!pci_dev)
		return -ENODEV;

	if (plat_priv->device_id != QCN9000_DEVICE_ID &&
	    plat_priv->device_id != QCN9224_DEVICE_ID) {
		cnss_pr_err("%s: Unknown device id 0x%lx",
			    __func__, plat_priv->device_id);
		return -EINVAL;
	}

	if (plat_priv->qdss_mem_seg_len > 1) {
		cnss_pr_err("%s: FW requests %d segments, max allowed is 1",
			    __func__, plat_priv->qdss_mem_seg_len);
		return -EINVAL;
	}

	if (plat_priv->dma_alloc_supported) {
		for (i = 0; i < plat_priv->qdss_mem_seg_len; i++) {
			if (!qdss_mem[i].va && qdss_mem[i].size) {
				qdss_mem[i].va =
					dma_alloc_attrs(&pci_dev->dev,
						qdss_mem[i].size,
						&qdss_mem[i].pa,
						GFP_KERNEL,
						DMA_ATTR_FORCE_CONTIGUOUS);

				if (!qdss_mem[i].va) {
					cnss_pr_err("Failed to allocate memory for QDSS, size: 0x%zx, type: %u\n",
						    qdss_mem[i].size,
						    qdss_mem[i].type);
					return -ENOMEM;
				}
			}
		}

		return 0;
	}

	/* Currently we support qdss_mem_seg_len = 1 only, however, if required
	 * this can be extended to support multiple QDSS memory segments
	 */
	for (i = 0; i < plat_priv->qdss_mem_seg_len; i++) {
		if (plat_priv->qdss_etr_sg_mode)
			cnss_etr_sg_tbl_alloc(plat_priv);
		else {
			switch (qdss_mem[i].type) {
			case QMI_WLFW_MEM_QDSS_V01:
				if (qdss_mem[i].size >
					Q6_QDSS_ETR_SIZE_QCN9000) {
					cnss_pr_err("%s: FW requests more memory 0x%zx\n",
						__func__, qdss_mem[i].size);
					return -ENOMEM;
				}
				if (of_property_read_u32(dev->of_node,
							"etr-addr",
							&addr)) {
					cnss_pr_err("Error: No etr-addr: in dts\n");
					CNSS_ASSERT(0);
					return -ENOMEM;
				}

				qdss_mem[i].pa = (phys_addr_t)addr;
				qdss_mem[i].va = ioremap(qdss_mem[i].pa,
						qdss_mem[i].size);
				if (!qdss_mem[i].va) {
					cnss_pr_err("WARNING etr-addr remap failed\n");
					return -ENOMEM;
				}
				break;
			default:
				cnss_pr_err("%s: Unknown type %d\n",
						__func__, qdss_mem[i].type);
				break;
			}
		}
	}

	cnss_pr_dbg("%s: seg_len %d, type %d, size 0x%zx, pa: 0x%pa, va: 0x%p",
		    __func__, plat_priv->qdss_mem_seg_len, qdss_mem[0].type,
		    qdss_mem[0].size, &qdss_mem[0].pa, qdss_mem[0].va);

	return 0;
}

static int cnss_pci_alloc_m3_mem(struct cnss_plat_data *plat_priv)
{
	struct pci_dev *pci_dev;
	struct cnss_fw_mem *m3_mem;

	if (!plat_priv) {
		cnss_pr_err("%s: plat_priv is NULL\n", __func__);
		return -ENODEV;
	}

	pci_dev = plat_priv->pci_dev;
	if (!pci_dev) {
		cnss_pr_err("%s: pci_dev is NULL\n", __func__);
		return -ENODEV;
	}

	m3_mem = &plat_priv->m3_mem;

	if (m3_mem->va) {
		cnss_pr_info("%s: m3_mem is already allocated\n", __func__);
		return 0;
	}

	/* Allocate a block of 512K for M3 mem, but m3_mem->size is not
	 * updated as 512K as it would hold the actual size of the M3 bin
	 * to be sent to the FW
	 */
	m3_mem->va = dma_alloc_coherent(&pci_dev->dev,
					SZ_512K, &m3_mem->pa,
					GFP_KERNEL);
	if (!m3_mem->va) {
		cnss_pr_err("Failed to allocate memory for M3, size: 0x%x\n",
			    SZ_512K);
		return -ENOMEM;
	}

	cnss_pr_dbg("M3 mem va: %p, pa: %pa, size: %d\n",
		    m3_mem->va, &m3_mem->pa, SZ_512K);

	return 0;
}

int cnss_pci_load_m3(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct cnss_fw_mem *m3_mem = &plat_priv->m3_mem;
	char filename[MAX_M3_FILE_NAME_LENGTH];
	const struct firmware *fw_entry;
	int ret = 0;

	/* M3 Mem should have been allocated during cnss_pci_probe_basic */
	if (!m3_mem->va) {
		cnss_pr_err("M3 Memory not allocated");
		return -ENOMEM;
	}

	snprintf(filename, sizeof(filename),
		 "%s%s", cnss_get_fw_path(plat_priv),
		 DEFAULT_M3_FILE_NAME);

	ret = request_firmware_direct(&fw_entry, filename,
				      &pci_priv->pci_dev->dev);
	if (ret) {
		cnss_pr_err("Failed to load M3 image: %s ret: %d\n",
			    filename, ret);
		return ret;
	}

	if (fw_entry->size > SZ_512K) {
		cnss_pr_err("M3 size %zd more than allocated size %d",
			    fw_entry->size, SZ_512K);
		release_firmware(fw_entry);
		return -ENOMEM;
	}

	memcpy(m3_mem->va, fw_entry->data, fw_entry->size);
	m3_mem->size = fw_entry->size;
	release_firmware(fw_entry);

	return 0;
}

static void cnss_pci_free_m3_mem(struct cnss_plat_data *plat_priv)
{
	struct cnss_fw_mem *m3_mem;
	struct pci_dev *pci_dev;

	if (!plat_priv) {
		cnss_pr_err("%s: plat_priv is NULL", __func__);
		return;
	}

	pci_dev = plat_priv->pci_dev;
	if (!pci_dev) {
		cnss_pr_err("%s: pci_dev is NULL", __func__);
		return;
	}

	m3_mem = &plat_priv->m3_mem;
	if (m3_mem->va) {
		cnss_pr_dbg("Resetting memory for M3, va: 0x%pK, pa: %pa, size: 0x%x\n",
			    m3_mem->va, &m3_mem->pa, SZ_512K);
		dma_free_coherent(&pci_dev->dev, SZ_512K, m3_mem->va,
				  m3_mem->pa);
	}

	m3_mem->va = NULL;
	m3_mem->pa = 0;
	m3_mem->size = 0;
}

int cnss_pci_force_fw_assert_hdlr(struct cnss_pci_data *pci_priv)
{
	int ret;
	struct cnss_plat_data *plat_priv;

	if (!pci_priv)
		return -ENODEV;

	plat_priv = pci_priv->plat_priv;
	if (!plat_priv)
		return -ENODEV;

	cnss_fatal_err("Triggering CNSS_ASSERT\n");
	CNSS_ASSERT(0);

	cnss_auto_resume(&pci_priv->pci_dev->dev);
	cnss_pci_dump_shadow_reg(pci_priv);

	ret = cnss_pci_set_mhi_state(pci_priv, CNSS_MHI_TRIGGER_RDDM);
	if (ret) {
		cnss_fatal_err("Failed to trigger RDDM, err = %d\n", ret);
		cnss_schedule_recovery(&pci_priv->pci_dev->dev,
				       CNSS_REASON_DEFAULT);
		return ret;
	}

	if (!test_bit(CNSS_DEV_ERR_NOTIFY, &plat_priv->driver_state)) {
		mod_timer(&plat_priv->fw_boot_timer,
			  jiffies + msecs_to_jiffies(FW_ASSERT_TIMEOUT));
	}

	return 0;
}

void cnss_pci_fw_boot_timeout_hdlr(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = NULL;

	if (!pci_priv)
		return;

	plat_priv = pci_priv->plat_priv;
	cnss_fatal_err("Timeout waiting for FW ready indication\n");

	cnss_schedule_recovery(&pci_priv->pci_dev->dev,
			       CNSS_REASON_TIMEOUT);
}

#ifdef CONFIG_CNSS2_SMMU
static int cnss_pci_smmu_fault_handler(struct iommu_domain *domain,
				       struct device *dev, unsigned long iova,
				       int flags, void *handler_token)
{
	struct cnss_pci_data *pci_priv = handler_token;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	cnss_pr_err("SMMU fault happened with IOVA 0x%lx\n", iova);

	/* Return ENOSYS to initiate IOMMU default fault handler */
	return -ENOSYS;
}

static int cnss_pci_init_smmu(struct cnss_pci_data *pci_priv)
{
	struct pci_dev *pci_dev = pci_priv->pci_dev;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct device_node *of_node;
	struct resource *res;
	const char *iommu_dma_type;
	u32 addr_win[2];
	int ret = 0;

	of_node = of_parse_phandle(pci_dev->dev.of_node, "qcom,iommu-group", 0);
	if (!of_node) {
		cnss_pr_err("No iommu group configuration\n");
		return ret;
	}

	cnss_pr_dbg("Initializing SMMU\n");

	pci_priv->iommu_domain = iommu_get_domain_for_dev(&pci_dev->dev);
	if (!pci_priv->iommu_domain) {
		cnss_pr_err("No iommu domain found\n");
		return -EINVAL;
	}

	ret = of_property_read_string(of_node, "qcom,iommu-dma",
				      &iommu_dma_type);
	if (!ret && (!strcmp("fastmap", iommu_dma_type) ||
	    !strcmp("atomic", iommu_dma_type))) {
		cnss_pr_dbg("Enabling SMMU S1 stage\n");
		pci_priv->smmu_s1_enable = true;
		iommu_set_fault_handler(pci_priv->iommu_domain,
					cnss_pci_smmu_fault_handler, pci_priv);
	}

	ret = of_property_read_u32_array(of_node,  "qcom,iommu-dma-addr-pool",
					 addr_win, ARRAY_SIZE(addr_win));
	if (ret) {
		cnss_pr_err("Invalid SMMU size window, err = %d\n", ret);
		of_node_put(of_node);
		return ret;
	}

	pci_priv->smmu_iova_start = addr_win[0];
	pci_priv->smmu_iova_len = addr_win[1];
	cnss_pr_dbg("smmu_iova_start: %pa, smmu_iova_len: 0x%zx\n",
		    &pci_priv->smmu_iova_start,
		    pci_priv->smmu_iova_len);

	res = platform_get_resource_byname(plat_priv->plat_dev, IORESOURCE_MEM,
					   "smmu_iova_ipa");
	if (res) {
		pci_priv->smmu_iova_ipa_start = res->start;
		pci_priv->smmu_iova_ipa_current = res->start;
		pci_priv->smmu_iova_ipa_len = resource_size(res);
		cnss_pr_dbg("smmu_iova_ipa_start: %pa, smmu_iova_ipa_len: 0x%zx\n",
			    &pci_priv->smmu_iova_ipa_start,
			    pci_priv->smmu_iova_ipa_len);
	}

	of_node_put(of_node);

	return 0;
}

static void cnss_pci_deinit_smmu(struct cnss_pci_data *pci_priv)
{
	pci_priv->iommu_domain = NULL;
}
#endif

struct iommu_domain *cnss_smmu_get_domain(struct device *dev)
{
#ifdef CONFIG_CNSS2_SMMU
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(to_pci_dev(dev));

	if (!pci_priv)
		return NULL;

	return pci_priv->iommu_domain;
#endif
	return NULL;
}
EXPORT_SYMBOL(cnss_smmu_get_domain);

int cnss_smmu_map(struct device *dev,
		  phys_addr_t paddr, uint32_t *iova_addr, size_t size)
{
#ifdef CONFIG_CNSS2_SMMU
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(to_pci_dev(dev));
	struct cnss_plat_data *plat_priv;
	unsigned long iova;
	int flag = IOMMU_READ | IOMMU_WRITE;
	struct pci_dev *root_port;
	struct device_node *root_of_node;
	size_t len;
	int ret = 0;
	bool dma_coherent = false;

	if (!pci_priv)
		return -ENODEV;

	plat_priv = pci_priv->plat_priv;
	if (!iova_addr) {
		cnss_pr_err("iova_addr is NULL, paddr %pa, size %zu\n",
			    &paddr, size);
		return -EINVAL;
	}

	len = roundup(size + paddr - rounddown(paddr, PAGE_SIZE), PAGE_SIZE);
	iova = roundup(pci_priv->smmu_iova_ipa_current, PAGE_SIZE);

	if (iova >=
	    (pci_priv->smmu_iova_ipa_start + pci_priv->smmu_iova_ipa_len)) {
		cnss_pr_err("No IOVA space to map, iova %lx, smmu_iova_ipa_start %pad, smmu_iova_ipa_len %zu\n",
			    iova,
			    &pci_priv->smmu_iova_ipa_start,
			    pci_priv->smmu_iova_ipa_len);
		return -ENOMEM;
	}

	root_port = pcie_find_root_port(pci_priv->pci_dev);
	if (!root_port) {
		cnss_pr_err("Root port is null, so dma_coherent is disabled\n");
	} else {
		root_of_node = root_port->dev.of_node;
		if (root_of_node && root_of_node->parent) {
			dma_coherent =
				of_property_read_bool(root_of_node->parent,
							"dma-coherent");
			cnss_pr_dbg("dma-coherent is %s\n",
					dma_coherent ? "enabled" : "disabled");
			if (dma_coherent)
				flag |= IOMMU_CACHE;
		}
	}

	cnss_pr_dbg("IOMMU map: iova %lx, len %zu\n", iova, len);

	ret = iommu_map(pci_priv->iommu_domain, iova,
			rounddown(paddr, PAGE_SIZE), len, flag);
	if (ret) {
		cnss_pr_err("PA to IOVA mapping failed, ret %d\n", ret);
		return ret;
	}

	pci_priv->smmu_iova_ipa_current = iova + len;
	*iova_addr = (uint32_t)(iova + paddr - rounddown(paddr, PAGE_SIZE));
	cnss_pr_dbg("IOMMU map: iova_addr %x\n", *iova_addr);
#endif

	return 0;
}
EXPORT_SYMBOL(cnss_smmu_map);

int cnss_smmu_unmap(struct device *dev, uint32_t iova_addr, size_t size)
{
#ifdef CONFIG_CNSS2_SMMU
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(to_pci_dev(dev));
	struct cnss_plat_data *plat_priv;
	unsigned long iova;
	size_t unmapped;
	size_t len;

	if (!pci_priv)
		return -ENODEV;

	plat_priv = pci_priv->plat_priv;
	iova = rounddown(iova_addr, PAGE_SIZE);
	len = roundup(size + iova_addr - iova, PAGE_SIZE);

	if (iova >= pci_priv->smmu_iova_ipa_start +
		    pci_priv->smmu_iova_ipa_len) {
		cnss_pr_err("Out of IOVA space to unmap, iova %lx, smmu_iova_ipa_start %pad, smmu_iova_ipa_len %zu\n",
			    iova,
			    &pci_priv->smmu_iova_ipa_start,
			    pci_priv->smmu_iova_ipa_len);
		return -ENOMEM;
	}

	cnss_pr_dbg("IOMMU unmap: iova %lx, len %zu\n", iova, len);

	unmapped = iommu_unmap(pci_priv->iommu_domain, iova, len);
	if (unmapped != len) {
		cnss_pr_err("IOMMU unmap failed, unmapped = %zu, requested = %zu\n",
			    unmapped, len);
		return -EINVAL;
	}

	pci_priv->smmu_iova_ipa_current = iova;
#endif
	return 0;
}
EXPORT_SYMBOL(cnss_smmu_unmap);

static struct cnss_msi_config msi_config_cold_qcn9000_pci0 = {
	.total_vectors = 4,
	.total_users = 2,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
	},
};

static struct cnss_msi_config msi_config_cold_qcn9000_pci1 = {
	.total_vectors = 4,
	.total_users = 2,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
	},
};

static struct cnss_msi_config msi_config_cold_qcn9000_pci2 = {
	.total_vectors = 4,
	.total_users = 2,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
	},
};

static struct cnss_msi_config msi_config_cold_qcn9000_pci3 = {
	.total_vectors = 4,
	.total_users = 2,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
	},
};

static struct cnss_msi_config msi_config_cold_qcn9224_pci0 = {
	.total_vectors = 4,
	.total_users = 2,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
	},
};

static struct cnss_msi_config msi_config_cold_qcn9224_pci1 = {
	.total_vectors = 4,
	.total_users = 2,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
	},
};

static struct cnss_msi_config msi_config_cold_qcn9224_pci2 = {
	.total_vectors = 4,
	.total_users = 2,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
	},
};

static struct cnss_msi_config msi_config_cold_qcn9224_pci3 = {
	.total_vectors = 4,
	.total_users = 2,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
	},
};

static struct cnss_msi_config msi_config_qcn9000_pci0 = {
	.total_vectors = 16,
	.total_users = 4,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
		{ .name = "CE", .num_vectors = 5, .base_vector = 3 },
		{ .name = "DP", .num_vectors = 8, .base_vector = 8 },
	},
};

static struct cnss_msi_config msi_config_qcn9000_pci1 = {
	.total_vectors = 16,
	.total_users = 4,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
		{ .name = "CE", .num_vectors = 5, .base_vector = 3 },
		{ .name = "DP", .num_vectors = 8, .base_vector = 8 },
	},
};

static struct cnss_msi_config msi_config_qcn9000_pci2 = {
	.total_vectors = 16,
	.total_users = 4,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
		{ .name = "CE", .num_vectors = 5, .base_vector = 3 },
		{ .name = "DP", .num_vectors = 8, .base_vector = 8 },
	},
};

static struct cnss_msi_config msi_config_qcn9000_pci3 = {
	.total_vectors = 16,
	.total_users = 4,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
		{ .name = "CE", .num_vectors = 5, .base_vector = 3 },
		{ .name = "DP", .num_vectors = 8, .base_vector = 8 },
	},
};

static struct cnss_msi_config msi_config_qcn9224_pci0 = {
	.total_vectors = 16,
	.total_users = 4,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
		{ .name = "CE", .num_vectors = 5, .base_vector = 3 },
		{ .name = "DP", .num_vectors = 8, .base_vector = 8 },
	},
};

static struct cnss_msi_config msi_config_qcn9224_pci1 = {
	.total_vectors = 16,
	.total_users = 4,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
		{ .name = "CE", .num_vectors = 5, .base_vector = 3 },
		{ .name = "DP", .num_vectors = 8, .base_vector = 8 },
	},
};

static struct cnss_msi_config msi_config_qcn9224_pci2 = {
	.total_vectors = 16,
	.total_users = 4,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
		{ .name = "CE", .num_vectors = 5, .base_vector = 3 },
		{ .name = "DP", .num_vectors = 8, .base_vector = 8 },
	},
};

static struct cnss_msi_config msi_config_qcn9224_pci3 = {
	.total_vectors = 16,
	.total_users = 4,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 2, .base_vector = 0 },
		{ .name = "QDSS", .num_vectors = 1, .base_vector = 2 },
		{ .name = "CE", .num_vectors = 5, .base_vector = 3 },
		{ .name = "DP", .num_vectors = 8, .base_vector = 8 },
	},
};

static int cnss_pci_get_msi_assignment(struct cnss_pci_data *pci_priv)
{
	int qrtr_node_id = pci_priv->plat_priv->qrtr_node_id;

	switch (qrtr_node_id) {
	case QCN9000_0:
		if (pci_priv->plat_priv->cal_in_progress)
			pci_priv->msi_config = &msi_config_cold_qcn9000_pci0;
		else
			pci_priv->msi_config = &msi_config_qcn9000_pci0;
		break;
	case QCN9000_1:
		if (pci_priv->plat_priv->cal_in_progress)
			pci_priv->msi_config = &msi_config_cold_qcn9000_pci1;
		else
			pci_priv->msi_config = &msi_config_qcn9000_pci1;
		break;
	case QCN9000_2:
		if (pci_priv->plat_priv->cal_in_progress)
			pci_priv->msi_config = &msi_config_cold_qcn9000_pci2;
		else
			pci_priv->msi_config = &msi_config_qcn9000_pci2;
		break;
	case QCN9000_3:
		if (pci_priv->plat_priv->cal_in_progress)
			pci_priv->msi_config = &msi_config_cold_qcn9000_pci3;
		else
			pci_priv->msi_config = &msi_config_qcn9000_pci3;
		break;
	case QCN9224_0:
		if (pci_priv->plat_priv->cal_in_progress)
			pci_priv->msi_config = &msi_config_cold_qcn9224_pci0;
		else
			pci_priv->msi_config = &msi_config_qcn9224_pci0;
		break;
	case QCN9224_1:
		if (pci_priv->plat_priv->cal_in_progress)
			pci_priv->msi_config = &msi_config_cold_qcn9224_pci1;
		else
			pci_priv->msi_config = &msi_config_qcn9224_pci1;
		break;
	case QCN9224_2:
		if (pci_priv->plat_priv->cal_in_progress)
			pci_priv->msi_config = &msi_config_cold_qcn9224_pci2;
		else
			pci_priv->msi_config = &msi_config_qcn9224_pci2;
		break;
	case QCN9224_3:
		if (pci_priv->plat_priv->cal_in_progress)
			pci_priv->msi_config = &msi_config_cold_qcn9224_pci3;
		else
			pci_priv->msi_config = &msi_config_qcn9224_pci3;
		break;
	default:
		pr_err("Unknown qrtr_node_id 0x%X", qrtr_node_id);
		return -EINVAL;
	}

	if (pci_priv->plat_priv->cal_in_progress)
		return 0;

	cnss_override_msi_assignment(pci_priv->plat_priv, pci_priv->msi_config);
	return 0;
}

static int cnss_pci_enable_msi(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct pci_dev *pci_dev = pci_priv->pci_dev;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	int num_vectors;
	struct cnss_msi_config *msi_config;
	struct msi_desc *msi_desc;

	ret = cnss_pci_get_msi_assignment(pci_priv);
	if (ret) {
		cnss_pr_err("Failed to get MSI assignment, err = %d\n", ret);
		goto out;
	}

	msi_config = pci_priv->msi_config;
	if (!msi_config) {
		cnss_pr_err("msi_config is NULL!\n");
		ret = -EINVAL;
		goto out;
	}

	num_vectors = pci_alloc_irq_vectors(pci_dev,
					    msi_config->total_vectors,
					    msi_config->total_vectors,
					    PCI_IRQ_MSI);
	if (num_vectors != msi_config->total_vectors) {
		cnss_pr_err("Failed to get enough MSI vectors (%d), available vectors = %d",
			    msi_config->total_vectors, num_vectors);
		if (num_vectors >= 0)
			ret = -EINVAL;
		goto reset_msi_config;
	}

	msi_desc = irq_get_msi_desc(pci_dev->irq);
	if (!msi_desc) {
		cnss_pr_err("msi_desc is NULL!\n");
		ret = -EINVAL;
		goto free_msi_vector;
	}

	pci_priv->msi_ep_base_data = msi_desc->msg.data;

#ifdef CONFIG_CNSS2_KERNEL_IPQ
	if (!pci_priv->msi_ep_base_data) {
		cnss_pr_err("Got 0 MSI base data!\n");
		CNSS_ASSERT(0);
	}
#endif
	cnss_pr_dbg("MSI base data is %d\n", pci_priv->msi_ep_base_data);

	return 0;

free_msi_vector:
	pci_free_irq_vectors(pci_priv->pci_dev);
reset_msi_config:
	pci_priv->msi_config = NULL;
out:
	return ret;
}

static void cnss_pci_disable_msi(struct cnss_pci_data *pci_priv)
{
	pci_free_irq_vectors(pci_priv->pci_dev);
}

int cnss_get_pci_slot(struct device *dev)
{
	struct cnss_plat_data *plat_priv =
		cnss_bus_dev_to_plat_priv(dev);
	uint32_t pci_slot_id = 0;

	if (!plat_priv)
		return -ENODEV;

	switch (plat_priv->device_id) {
	case QCN9000_DEVICE_ID:
		return plat_priv->qrtr_node_id - QCN9000_0;
	case QCN9224_DEVICE_ID:
		return plat_priv->qrtr_node_id - QCN9224_0;
	case QCN6122_DEVICE_ID:
	case QCN9160_DEVICE_ID:
		return plat_priv->userpd_id - USERPD_0;
	case QCN6432_DEVICE_ID:
		of_property_read_u32(dev->of_node, "qcom,pci_slot_id",
					&pci_slot_id);
		return pci_slot_id;
default:
		cnss_pr_info("PCI slot is 0 for target 0x%lx",
			     plat_priv->device_id);
		return 0;
	}
}
EXPORT_SYMBOL(cnss_get_pci_slot);

static unsigned int cnss_pci_get_wake_msi(struct cnss_pci_data *pci_priv)
{
	int ret, num_vectors;
	unsigned int user_base_data, base_vector;
	struct cnss_plat_data *plat_priv = NULL;

	if (!pci_priv) {
		cnss_pr_err("%s: pci_priv is NULL\n", __func__);
		return -ENODEV;
	}

	plat_priv = pci_priv->plat_priv;
	ret = cnss_pci_get_user_msi_assignment(&pci_priv->pci_dev->dev,
					   WAKE_MSI_NAME, &num_vectors,
					   &user_base_data, &base_vector);
	if (ret) {
		cnss_pr_err("WAKE MSI is not valid\n");
		return 0;
	}

	return user_base_data;
}

void *cnss_get_pci_mem(struct pci_dev *pci_dev)
{
	return cnss_get_pci_priv(pci_dev)->bar;
}
EXPORT_SYMBOL(cnss_get_pci_mem);

static int cnss_pci_enable_bus(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct pci_dev *pci_dev = pci_priv->pci_dev;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	u16 device_id;

	pci_read_config_word(pci_dev, PCI_DEVICE_ID, &device_id);

	if (device_id != pci_priv->pci_device_id->device)  {
		cnss_pr_err("PCI device ID mismatch, config ID: 0x%x, probe ID: 0x%x\n",
			    device_id, pci_priv->pci_device_id->device);
		ret = -EIO;
		goto out;
	}

	ret = pci_assign_resource(pci_dev, PCI_BAR_NUM);
	if (ret) {
		pr_err("Failed to assign PCI resource, err = %d\n", ret);
		goto out;
	}

	ret = pci_enable_device(pci_dev);
	if (ret) {
		cnss_pr_err("Failed to enable PCI device, err = %d\n", ret);
		goto out;
	}

	ret = pci_request_region(pci_dev, PCI_BAR_NUM, "cnss");
	if (ret) {
		cnss_pr_err("Failed to request PCI region, err = %d\n", ret);
		goto disable_device;
	}

#if defined(CONFIG_CNSS2_KERNEL_MSM) || defined(CONFIG_CNSS2_KERNEL_5_15)
	pci_priv->dma_bit_mask = PCI_DMA_MASK_36_BIT;
#else
	pci_priv->dma_bit_mask = PCI_DMA_MASK_64_BIT;
#endif

#if (KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE)
	ret = dma_set_mask(&pci_dev->dev, pci_priv->dma_bit_mask);
#else
	ret = pci_set_dma_mask(pci_dev, pci_priv->dma_bit_mask);
#endif
	if (ret) {
		cnss_pr_err("Failed to set PCI DMA mask (%lld), err = %d\n",
			    pci_priv->dma_bit_mask, ret);
		goto release_region;
	}

#if (KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE)
	ret = dma_set_coherent_mask(&pci_dev->dev, pci_priv->dma_bit_mask);
#else
	ret = pci_set_consistent_dma_mask(pci_dev, pci_priv->dma_bit_mask);
#endif
	if (ret) {
		cnss_pr_err("Failed to set PCI consistent DMA mask (%lld), err = %d\n",
			    pci_priv->dma_bit_mask, ret);
		goto release_region;
	}

	pci_set_master(pci_dev);

	pci_priv->bar = pci_iomap(pci_dev, PCI_BAR_NUM, 0);
	if (!pci_priv->bar) {
		cnss_pr_err("Failed to do PCI IO map!\n");
		ret = -EIO;
		goto clear_master;
	}

#ifdef CONFIG_CNSS2_LEGACY_IRQ
	if (plat_priv->enable_intx)
		set_lvirq_bar(plat_priv->lvirq, pci_priv->bar);
#endif
	return 0;

clear_master:
	pci_clear_master(pci_dev);
release_region:
	pci_release_region(pci_dev, PCI_BAR_NUM);
disable_device:
	pci_disable_device(pci_dev);
out:
	return ret;
}

#if (KERNEL_VERSION(5, 15, 0) > LINUX_VERSION_CODE)
void cnss_pci_global_reset(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	int resetcount = 0, tx_count = 0;
	u32 errdbg1 = 0;
#if defined(CONFIG_CNSS2_QCOM_KERNEL_DEPENDENCY) && IS_ENABLED(CONFIG_PCIE_QCOM)
	u32 pcie_cfg_pcie_status = 0;
	int ret = 0;
#endif
#if defined(CONFIG_CNSS2_KERNEL_MSM)
	int current_ee;
#else
	enum mhi_ee_type current_ee;
#endif

	current_ee = mhi_get_exec_env(pci_priv->mhi_ctrl);

	/* Wait for the Ramdump transfer to complete */
	if (current_ee == MHI_EE_RDDM) {
		errdbg1 = readl_relaxed(pci_priv->mhi_ctrl->bhi + BHI_ERRDBG1);
		while (errdbg1 != MHI_RAMDUMP_DUMP_COMPLETE &&
		       tx_count < MAX_RAMDUMP_TRANSFER_WAIT_CNT) {
			msleep(20);
			errdbg1 = readl_relaxed(pci_priv->mhi_ctrl->bhi +
						BHI_ERRDBG1);
			tx_count++;
		}
	}

	if (tx_count > 25)
		cnss_pr_warn("Dump time exceeds %d mseconds\n", tx_count * 20);

#if defined(CONFIG_CNSS2_QCOM_KERNEL_DEPENDENCY) && IS_ENABLED(CONFIG_PCIE_QCOM)
	ret = pcie_parf_read(pci_priv->pci_dev, PCIE_CFG_PCIE_STATUS,
			     &pcie_cfg_pcie_status);
	if (ret)
		cnss_pr_err("%s failed, err = %d\n", __func__, ret);
	else
		cnss_pr_info("%s: PCIE_CFG_PCIE_STATUS: 0x%08x\n", __func__,
			    pcie_cfg_pcie_status);
#endif

	/* Reset the Target */
	do {
		writel_relaxed(QCN9000_PCIE_SOC_GLOBAL_RESET_VALUE,
			       QCN9000_PCIE_SOC_GLOBAL_RESET_ADDRESS +
			       pci_priv->bar);
		resetcount++;
		msleep(20);
		current_ee = mhi_get_exec_env(pci_priv->mhi_ctrl);
	} while (current_ee != MHI_EE_PBL &&
		 resetcount < MAX_SOC_GLOBAL_RESET_WAIT_CNT);

	if (resetcount > 10)
		cnss_pr_warn("SOC_GLOBAL_RESET at rst cnt %d tx_cnt %d\n",
			     resetcount, tx_count);
}
#else
#define PCIE_SOC_GLOBAL_RESET                   0x3008
#define PCIE_SOC_GLOBAL_RESET_V                1
void cnss_pci_global_reset(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	u32 val, delay, iRet = 0;
#if defined(CONFIG_CNSS2_QCOM_KERNEL_DEPENDENCY) && IS_ENABLED(CONFIG_PCIE_QCOM)
	u32 pcie_cfg_pcie_status = 0;
	int ret = 0;

	ret = pcie_parf_read(pci_priv->pci_dev, PCIE_CFG_PCIE_STATUS,
			     &pcie_cfg_pcie_status);
	if (ret)
		cnss_pr_err("%s failed, err = %d\n", __func__, ret);
	else
		cnss_pr_info("%s: PCIE_CFG_PCIE_STATUS: 0x%08x\n", __func__,
			    pcie_cfg_pcie_status);
#endif

	iRet = cnss_pci_reg_read(plat_priv, PCIE_SOC_GLOBAL_RESET, &val);
	if (iRet != 0)
		cnss_pr_err("Error(%d): %s failed.\n", iRet, __func__);

	val |= PCIE_SOC_GLOBAL_RESET_V;

	iRet = cnss_pci_reg_write(pci_priv, PCIE_SOC_GLOBAL_RESET, val);
	if (iRet != 0)
		cnss_pr_err("Error(%d): %s failed.\n", iRet, __func__);

	/* exact time to sleep is uncertain */
	delay = 10;
	mdelay(delay);

	/* Need to toggle V bit back otherwise stuck in reset status */
	val &= ~PCIE_SOC_GLOBAL_RESET_V;

	iRet = cnss_pci_reg_write(pci_priv, PCIE_SOC_GLOBAL_RESET, val);
	if (iRet != 0)
		cnss_pr_err("Error(%d): %s failed.\n", iRet, __func__);

	mdelay(delay);

	iRet = cnss_pci_reg_read(plat_priv, PCIE_SOC_GLOBAL_RESET, &val);
	if (iRet != 0)
		cnss_pr_err("Error(%d): %s failed.\n", iRet, __func__);

	if (val == 0xffffffff)
		cnss_pr_warn("link down error during global reset\n");
}
#endif

#if defined(CONFIG_CNSS2_KERNEL_MSM) || \
	(KERNEL_VERSION(5, 7, 0) <= LINUX_VERSION_CODE)
static void cnss_reset_mhi_state(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	u32 val = 0;

	cnss_pci_reg_read(plat_priv, MHISTATUS, &val);
	cnss_pr_info("Setting MHI State to reset, current state: 0x%x", val);
	cnss_pci_reg_write(pci_priv, MHICTRL, MHICTRL_RESET_MASK);
}
#endif

static void cnss_pci_disable_bus(struct cnss_pci_data *pci_priv)
{
	struct pci_dev *pci_dev = pci_priv->pci_dev;
#ifdef CONFIG_CNSS2_LEGACY_IRQ
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
#endif

	/* Call global reset here */
	cnss_pci_global_reset(pci_priv);

	/* On SOC_GLOBAL_RESET, target waits in PBL for host to set the
	 * MHI_RESET bit to 1.
	 */
#if defined(CONFIG_CNSS2_KERNEL_MSM) || \
	(KERNEL_VERSION(5, 7, 0) <= LINUX_VERSION_CODE)
	cnss_reset_mhi_state(pci_priv);
#else
	mhi_set_mhi_state(pci_priv->mhi_ctrl, MHI_STATE_RESET);
#endif
	if (pci_priv->bar) {
		pci_iounmap(pci_dev, pci_priv->bar);
		pci_priv->bar = NULL;
	}
#ifdef CONFIG_CNSS2_LEGACY_IRQ
	if (plat_priv->enable_intx)
		clear_lvirq_bar(plat_priv->lvirq);
#endif

	pci_clear_master(pci_dev);
	pci_release_region(pci_dev, PCI_BAR_NUM);
	if (pci_is_enabled(pci_dev))
		pci_disable_device(pci_dev);
}

static void cnss_pci_dump_qdss_reg(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	int i, array_size = ARRAY_SIZE(qdss_csr) - 1;
	gfp_t gfp = GFP_KERNEL;
	u32 reg_offset;

	if (in_interrupt() || irqs_disabled())
		gfp = GFP_ATOMIC;

	if (!plat_priv->qdss_reg) {
		plat_priv->qdss_reg = devm_kzalloc(&pci_priv->pci_dev->dev,
						   sizeof(*plat_priv->qdss_reg)
						   * array_size, gfp);
		if (!plat_priv->qdss_reg)
			return;
	}

	for (i = 0; i < array_size && qdss_csr[i].name; i++) {
		reg_offset = QDSS_APB_DEC_CSR_BASE + qdss_csr[i].offset;
		if (cnss_pci_reg_read(plat_priv, reg_offset,
				      &plat_priv->qdss_reg[i]))
			return;
		cnss_pr_dbg("%s[0x%x] = 0x%x\n", qdss_csr[i].name, reg_offset,
			    plat_priv->qdss_reg[i]);
	}
}

#define MAX_RAMDUMP_TABLE_SIZE	6
#define COREDUMP_DESC		"Q6-COREDUMP"
#define Q6_SFR_DESC		"Q6-SFR"
struct cnss_ramdump_entry {
	__le64 base_address;
	__le64 actual_phys_address;
	__le64 size;
	char description[20];
	char file_name[20];
};

struct cnss_ramdump_header {
	__le32 version;
	__le32 header_size;
	struct cnss_ramdump_entry ramdump_table[MAX_RAMDUMP_TABLE_SIZE];
};

void cnss_get_crash_reason(struct cnss_pci_data *pci_priv)
{
	int i;
	uint64_t coredump_offset = 0;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct mhi_controller *mhi_cntrl;
	struct mhi_buf *mhi_buf;
	struct image_info *rddm_image;
	struct cnss_ramdump_header *ramdump_header;
	struct cnss_ramdump_entry *ramdump_table;
	char *msg = ERR_PTR(-EPROBE_DEFER);
	struct pci_dev *pci_dev = plat_priv->pci_dev;
	struct device *dev;

	mhi_cntrl = pci_priv->mhi_ctrl;
	rddm_image = mhi_cntrl->rddm_image;
	mhi_buf = rddm_image->mhi_buf;
	dev = &pci_dev->dev;

	dev_err(dev, "CRASHED - [DID:DOMAIN:BUS:SLOT] - %x:%04u:%02u:%02u\n",
		    pci_dev->device, pci_dev->bus->domain_nr,
		    pci_dev->bus->number, PCI_SLOT(pci_dev->devfn));

	/* Get RDDM header size */
	ramdump_header = (struct cnss_ramdump_header *)mhi_buf[0].buf;
	ramdump_table = ramdump_header->ramdump_table;
	coredump_offset += le32_to_cpu(ramdump_header->header_size);

	/* Traverse ramdump table to get coredump offset */
	i = 0;
	while (i < MAX_RAMDUMP_TABLE_SIZE) {
		if (!strncmp(ramdump_table->description, COREDUMP_DESC,
			     sizeof(COREDUMP_DESC)) ||
		    !strncmp(ramdump_table->description, Q6_SFR_DESC,
			     sizeof(Q6_SFR_DESC))) {
			break;
		}
		coredump_offset += le64_to_cpu(ramdump_table->size);
		ramdump_table++;
		i++;
	}

	if (i == MAX_RAMDUMP_TABLE_SIZE) {
		cnss_pr_err("Cannot find '%s' entry in ramdump\n",
			    COREDUMP_DESC);
		return;
	}

	/* Locate coredump data from the ramdump segments */
	for (i = 0; i < rddm_image->entries; i++) {
		if (coredump_offset < mhi_buf[i].len) {
			msg = mhi_buf[i].buf + coredump_offset;
			break;
		}

		coredump_offset -= mhi_buf[i].len;
	}

	if (!IS_ERR(msg) && msg && msg[0])
		dev_err(dev, "Fatal error received from wcss software!\n%s\n",
			    msg);
}

void cnss_pci_collect_dump_info(struct cnss_pci_data *pci_priv, bool in_panic)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct cnss_dump_data *dump_data =
		&plat_priv->ramdump_info_v2.dump_data;
	struct cnss_dump_seg *dump_seg =
		plat_priv->ramdump_info_v2.dump_data_vaddr;
	struct image_info *fw_image, *rddm_image;
	struct cnss_fw_mem *fw_mem = plat_priv->fw_mem;
	struct cnss_fw_mem *qdss_mem = plat_priv->qdss_mem;
	int ret, i, skip_count = 0;

	if (test_bit(CNSS_RDDM_DUMP_IN_PROGRESS, &plat_priv->driver_state)) {
		cnss_pr_dbg("RAM dump is in progress for PCI%d, skip\n",
			    plat_priv->pci_slot_id);
		return;
	}
	set_bit(CNSS_RDDM_DUMP_IN_PROGRESS, &plat_priv->driver_state);

	if (test_bit(CNSS_MHI_RDDM_DONE, &pci_priv->mhi_state)) {
		cnss_pr_dbg("RAM dump is already collected, skip\n");
		clear_bit(CNSS_RDDM_DUMP_IN_PROGRESS, &plat_priv->driver_state);
		return;
	}

	if (cnss_pci_check_link_status(pci_priv)) {
		clear_bit(CNSS_RDDM_DUMP_IN_PROGRESS, &plat_priv->driver_state);
		return;
	}

	plat_priv->target_assert_timestamp = ktime_to_ms(ktime_get());

#ifdef CONFIG_CNSS2_KERNEL_MSM
	ret = mhi_download_rddm_img(pci_priv->mhi_ctrl, in_panic);
#else
	ret = mhi_download_rddm_image(pci_priv->mhi_ctrl, in_panic);
#endif
	if (ret) {
		cnss_fatal_err("Failed to download RDDM image, err = %d\n",
			       ret);
		cnss_pci_dump_qdss_reg(pci_priv);
		cnss_dump_all_ce_reg(plat_priv);
		clear_bit(CNSS_RDDM_DUMP_IN_PROGRESS, &plat_priv->driver_state);
		return;
	}

	cnss_get_crash_reason(pci_priv);

	fw_image = pci_priv->mhi_ctrl->fbc_image;
	rddm_image = pci_priv->mhi_ctrl->rddm_image;
	dump_data->nentries = 0;

	cnss_pr_dbg("Collect FW image dump segment, nentries %d\n",
		    fw_image->entries);

	for (i = 0; i < fw_image->entries; i++) {
		if (!fw_image->mhi_buf[i].dma_addr) {
			skip_count++;
			continue;
		}

		dump_seg->address = fw_image->mhi_buf[i].dma_addr;
		dump_seg->v_address = fw_image->mhi_buf[i].buf;
		dump_seg->size = fw_image->mhi_buf[i].len;
		dump_seg->type = CNSS_FW_IMAGE;
		cnss_pr_dbg("seg-%d: address 0x%lx, v_address %pK, size 0x%lx\n",
			    i, dump_seg->address,
			    dump_seg->v_address, dump_seg->size);
		dump_seg++;
	}

	dump_data->nentries += fw_image->entries - skip_count;

	cnss_pr_dbg("Collect RDDM image dump segment, nentries %d\n",
		    rddm_image->entries);

	for (i = 0; i < rddm_image->entries; i++) {
		dump_seg->address = rddm_image->mhi_buf[i].dma_addr;
		dump_seg->v_address = rddm_image->mhi_buf[i].buf;
		dump_seg->size = rddm_image->mhi_buf[i].len;
		dump_seg->type = CNSS_FW_RDDM;
		cnss_pr_dbg("seg-%d: address 0x%lx, v_address %pK, size 0x%lx\n",
			    i, dump_seg->address,
			    dump_seg->v_address, dump_seg->size);
		dump_seg++;
	}

	dump_data->nentries += rddm_image->entries;

	cnss_pr_dbg("Collect remote heap dump segment\n");
	for (i = 0; i < plat_priv->fw_mem_seg_len; i++) {
		if (fw_mem[i].type == CNSS_MEM_TYPE_DDR) {
			if (!fw_mem[i].pa)
				continue;
			dump_seg->address = fw_mem[i].pa;
			dump_seg->v_address = fw_mem[i].va;
			dump_seg->size = fw_mem[i].size;
			dump_seg->type = CNSS_FW_REMOTE_HEAP;
			cnss_pr_dbg("seg-%d: address 0x%lx, v_address %pK, size 0x%lx\n",
				    i, dump_seg->address, dump_seg->v_address,
				    dump_seg->size);
			dump_seg++;
			dump_data->nentries++;
		}
	}

	cnss_pr_dbg("Collect PCSS SSR dump region\n");
	for (i = 0; i < plat_priv->fw_mem_seg_len; i++) {
		if (fw_mem[i].type == CNSS_MEM_M3) {
			if (!fw_mem[i].pa)
				continue;
			dump_seg->address = fw_mem[i].pa;
			dump_seg->v_address = fw_mem[i].va;
			dump_seg->size = fw_mem[i].size;
			dump_seg->type = CNSS_FW_REMOTE_M3_DUMP;
			cnss_pr_dbg("seg-%d: address 0x%lx, v_address %pK, size 0x%lx\n",
				    i, dump_seg->address, dump_seg->v_address,
				    dump_seg->size);
			dump_seg++;
			dump_data->nentries++;
		}
	}

	cnss_pr_dbg("Collect QDSS dump segment\n");
	for (i = 0; i < plat_priv->qdss_mem_seg_len; i++) {
		if (qdss_mem[i].type == CNSS_MEM_ETR) {
			if (!qdss_mem[i].pa)
				continue;
			dump_seg->address = qdss_mem[i].pa;
			dump_seg->v_address = qdss_mem[i].va;
			dump_seg->size = qdss_mem[i].size;
			dump_seg->type = CNSS_FW_REMOTE_ETR;
			cnss_pr_dbg("QDSS seg-%d: address 0x%lx, v_address %pK, size 0x%lx\n",
				    i, dump_seg->address, dump_seg->v_address,
				    dump_seg->size);
			dump_seg++;
			dump_data->nentries++;
		}
	}

	cnss_pr_dbg("Collect Caldb dump segment\n");
	for (i = 0; i < plat_priv->fw_mem_seg_len; i++) {
		if (fw_mem[i].type == CNSS_MEM_CAL_V01) {
			if (!fw_mem[i].pa)
				continue;
			dump_seg->address = fw_mem[i].pa;
			dump_seg->v_address = fw_mem[i].va;
			dump_seg->size = fw_mem[i].size;
			dump_seg->type = CNSS_FW_REMOTE_CALDB;
			cnss_pr_dbg("seg-%d: address 0x%lx, v_address %pK, size 0x%lx\n",
				    i, dump_seg->address, dump_seg->v_address,
				    dump_seg->size);
			dump_seg++;
			dump_data->nentries++;
		}
	}

	cnss_pr_dbg("Collect AFC memory dump segment\n");
	for (i = 0; i < plat_priv->fw_mem_seg_len; i++) {
		if (fw_mem[i].type == CNSS_MEM_AFC) {
			if (!fw_mem[i].pa)
				continue;
			dump_seg->address = fw_mem[i].pa;
			dump_seg->v_address = fw_mem[i].va;
			dump_seg->size = fw_mem[i].size;
			dump_seg->type = CNSS_FW_REMOTE_AFC;
			cnss_pr_dbg("seg-%d: address 0x%lx, v_address %pK, size 0x%lx\n",
				    i, dump_seg->address, dump_seg->v_address,
				    dump_seg->size);
			dump_seg++;
			dump_data->nentries++;
		}
	}

	cnss_pr_dbg("Collect MLO global memory dump segment\n");
	for (i = 0; i < plat_priv->fw_mem_seg_len; i++) {
		if (fw_mem[i].type == CNSS_MEM_MLO_GLOBAL) {
			if (!fw_mem[i].pa)
				continue;
			dump_seg->address = fw_mem[i].pa;
			dump_seg->v_address = fw_mem[i].va;
			dump_seg->size = fw_mem[i].size;
			dump_seg->type = CNSS_FW_REMOTE_MLO_GLOBAL;
			cnss_pr_dbg("seg-%d: address 0x%lx, v_address %pK, size 0x%lx\n",
				    i, dump_seg->address, dump_seg->v_address,
				    dump_seg->size);
			dump_seg++;
			dump_data->nentries++;
		}
	}

	cnss_pr_dbg("Collect pageable memory dump segment\n");
	for (i = 0; i < plat_priv->fw_mem_seg_len; i++) {
		if (fw_mem[i].type == CNSS_MEM_PAGEABLE) {
			if (!fw_mem[i].pa)
				continue;
			dump_seg->address = fw_mem[i].pa;
			dump_seg->v_address = fw_mem[i].va;
			dump_seg->size = fw_mem[i].size;
			dump_seg->type = CNSS_FW_PAGEABLE;
			cnss_pr_dbg("seg-%d: address 0x%lx, v_address %pK, size 0x%lx\n",
				    i, dump_seg->address,
				    dump_seg->v_address,
				    dump_seg->size);
			dump_seg++;
			dump_data->nentries++;
		}
	}

	cnss_pr_info("Dump Collection Completed, total entries is %d\n",
			dump_data->nentries);
	if (dump_data->nentries > 0)
		plat_priv->ramdump_info_v2.dump_data_valid = true;

	cnss_pci_set_mhi_state(pci_priv, CNSS_MHI_RDDM_DONE);
	clear_bit(CNSS_RDDM_DUMP_IN_PROGRESS, &plat_priv->driver_state);
	complete(&plat_priv->rddm_complete);
}

void cnss_pci_clear_dump_info(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	plat_priv->ramdump_info_v2.dump_data.nentries = 0;
	plat_priv->ramdump_info_v2.dump_data_valid = false;
}

#ifdef CONFIG_CNSS2_KERNEL_MSM
static int cnss_mhi_pm_runtime_get(struct mhi_controller *mhi_ctrl, void *priv)
{
	struct cnss_pci_data *pci_priv = priv;

	return cnss_pci_pm_runtime_get(pci_priv);
}
#else
static int cnss_mhi_pm_runtime_get(struct mhi_controller *mhi_ctrl)
{
	struct cnss_pci_data *pci_priv = dev_get_drvdata(mhi_ctrl->cntrl_dev);

	return cnss_pci_pm_runtime_get(pci_priv);
}
#endif

#ifdef CONFIG_CNSS2_KERNEL_MSM
static void cnss_mhi_pm_runtime_put_noidle(struct mhi_controller *mhi_ctrl,
					   void *priv)
{
	struct cnss_pci_data *pci_priv = priv;

	cnss_pci_pm_runtime_put_noidle(pci_priv);
}
#else
static void cnss_mhi_pm_runtime_put_noidle(struct mhi_controller *mhi_ctrl)
{
	struct cnss_pci_data *pci_priv = dev_get_drvdata(mhi_ctrl->cntrl_dev);

	cnss_pci_pm_runtime_put_noidle(pci_priv);
}
#endif

#ifdef CONFIG_CNSS2_KERNEL_MSM
static char *cnss_mhi_notify_status_to_str(enum MHI_CB status)
#else
static char *cnss_mhi_notify_status_to_str(enum mhi_callback status)
#endif
{
	switch (status) {
	case MHI_CB_IDLE:
		return "IDLE";
	case MHI_CB_EE_RDDM:
		return "RDDM";
	case MHI_CB_SYS_ERROR:
		return "SYS_ERROR";
	case MHI_CB_FATAL_ERROR:
		return "FATAL_ERROR";
	case MHI_CB_EE_MISSION_MODE:
		return "MISSION MODE";
	default:
		return "UNKNOWN";
	}
};

static void cnss_dev_rddm_timeout_hdlr(struct timer_list *timer)
{
	struct cnss_plat_data *plat_priv = NULL;
	struct cnss_pci_data *pci_priv =
			from_timer(pci_priv, timer, dev_rddm_timer);

	if (!pci_priv)
		return;

	plat_priv = pci_priv->plat_priv;
	cnss_fatal_err("Timeout waiting for RDDM notification\n");

	cnss_schedule_recovery(&pci_priv->pci_dev->dev, CNSS_REASON_TIMEOUT);
}

#ifdef CONFIG_CNSS2_KERNEL_MSM
static int cnss_mhi_link_status(struct mhi_controller *mhi_ctrl, void *priv)
{
	struct cnss_pci_data *pci_priv = priv;

	if (!pci_priv) {
		pr_err("%s: pci_priv is NULL\n", __func__);
		return -EINVAL;
	}

	return cnss_pci_check_link_status(pci_priv);
}
#endif

#ifdef CONFIG_CNSS2_KERNEL_MSM
static void cnss_mhi_notify_status(struct mhi_controller *mhi_ctrl, void *priv,
				   enum MHI_CB reason)
#else
static void cnss_mhi_notify_status(struct mhi_controller *mhi_ctrl,
				   enum mhi_callback reason)
#endif
{
#ifdef CONFIG_CNSS2_KERNEL_MSM
	struct cnss_pci_data *pci_priv = priv;
#else
	struct cnss_pci_data *pci_priv = dev_get_drvdata(mhi_ctrl->cntrl_dev);
#endif
	struct cnss_plat_data *plat_priv = NULL;
	enum cnss_recovery_reason cnss_reason;

	if (!pci_priv) {
		cnss_pr_err("%s: pci_priv is NULL\n", __func__);
		return;
	}

	plat_priv = pci_priv->plat_priv;

	if (reason == MHI_CB_EE_RDDM) {
		/* In-case of RDDM switched by MHI SoC Reset */
		if (test_bit(CNSS_MHI_SOC_RESET, &pci_priv->mhi_state)) {
			cnss_pr_info("Switched from MHI SOC_RESET to RDDM state\n");
			complete(&plat_priv->soc_reset_request_complete);
			clear_bit(CNSS_MHI_SOC_RESET, &pci_priv->mhi_state);
			return;
		}

		/* check duplicate RDDM received from MHI */
		if (mhi_get_exec_env(pci_priv->mhi_ctrl) == mhi_ctrl->ee) {
			cnss_pr_dbg("Skip duplicate %s(%d) received from MHI for the same SoC\n",
				    cnss_mhi_notify_status_to_str(reason),
				    reason);
			return;
		}

		/* In-case of Target Assert */
		cnss_pci_set_mhi_state_bit(pci_priv, CNSS_MHI_RDDM);
	}

	if (reason != MHI_CB_IDLE)
		cnss_pr_info("MHI status cb is called with reason %s(%d)\n",
			     cnss_mhi_notify_status_to_str(reason), reason);

	if (reason == MHI_CB_FATAL_ERROR || reason == MHI_CB_SYS_ERROR ||
	    (reason == MHI_CB_EE_RDDM && !plat_priv->target_asserted)) {
		cnss_pr_err("XXX TARGET ASSERTED XXX\n");
		cnss_pr_err("XXX TARGET %s instance_id 0x%x plat_env idx %d XXX\n",
			    plat_priv->device_name,
			    plat_priv->wlfw_service_instance_id,
			    cnss_get_plat_env_index_from_plat_priv(plat_priv));
		plat_priv->target_asserted = 1;
		plat_priv->target_assert_timestamp = ktime_to_ms(ktime_get());

		/* Target Assert happens in down path and call BUG_ON instead
		 * of CNSS_ASSERT. This immediate BUG_ON will help to stop
		 * concurrent execution of dirver shutdown flow in another
		 * context. QMI history is not required in down path.
		 */
		if (test_bit(CNSS_DRIVER_UNLOADING, &plat_priv->driver_state) ||
		    test_bit(CNSS_DRIVER_IDLE_SHUTDOWN,
			     &plat_priv->driver_state)) {
			cnss_pr_err("Driver unload or shutdown is in progress, called Host Assert\n");
			BUG_ON(1);
			return;
		}

		/* If target recovery is enabled in the wifi driver, deliver
		 * the fatal notification immediately here.
		 * For recovery disabled case, fatal notification will be sent
		 * to the wifi driver after RDDM content is uploaded to host
		 * DDR from the target. This is done to make sure that FW SRAM
		 * contents are copied into the host DDR at the time of host
		 * crash and reboot.
		 */
		if (plat_priv->recovery_enabled)
			cnss_pci_update_status(pci_priv, CNSS_FW_DOWN);
	}

	switch (reason) {
	case MHI_CB_IDLE:
		return;
	case MHI_CB_FATAL_ERROR:
		set_bit(CNSS_DEV_ERR_NOTIFY, &plat_priv->driver_state);
		del_timer(&plat_priv->fw_boot_timer);
		cnss_reason = CNSS_REASON_DEFAULT;
		break;
	case MHI_CB_SYS_ERROR:
		set_bit(CNSS_DEV_ERR_NOTIFY, &plat_priv->driver_state);
		del_timer(&plat_priv->fw_boot_timer);
		mod_timer(&pci_priv->dev_rddm_timer, jiffies +
			  msecs_to_jiffies(DEV_RDDM_TIMEOUT * timeout_factor));
		return;
	case MHI_CB_EE_RDDM:
		set_bit(CNSS_DEV_ERR_NOTIFY, &plat_priv->driver_state);
		del_timer(&plat_priv->fw_boot_timer);
		del_timer(&pci_priv->dev_rddm_timer);
		cnss_reason = CNSS_REASON_RDDM;
		break;
	case MHI_CB_EE_MISSION_MODE:
		del_timer(&pci_priv->boot_debug_timer);
		cnss_pci_set_mhi_state_bit(pci_priv, CNSS_MHI_MISSION_MODE);
		return;
	default:
		cnss_pr_err("Unsupported MHI status cb reason: %d\n", reason);
		return;
	}

	cnss_schedule_recovery(&pci_priv->pci_dev->dev, cnss_reason);
}

#if defined(CONFIG_CNSS2_KERNEL_IPQ) || defined(CONFIG_CNSS2_KERNEL_5_15)
#define CNSS_PCI_INVALID_READ(val) (val == U32_MAX)
static int __must_check cnss_mhi_read_reg(struct mhi_controller *mhi_cntrl,
					  void __iomem *addr, u32 *out)
{
	u32 tmp = readl(addr);

	/* If the value is invalid, the link is down */
	if (CNSS_PCI_INVALID_READ(tmp))
		return -EIO;

	*out = tmp;

	return 0;
}

static void cnss_mhi_write_reg(struct mhi_controller *mhi_cntrl,
			       void __iomem *addr, u32 val)
{
	writel(val, addr);
}
#endif

static int cnss_pci_get_qdss_msi(struct cnss_pci_data *pci_priv)
{
	int ret = 0, num_vectors, i;
	u32 user_base_data, base_vector;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	int qdss_irq = 0;

	num_vectors = 1;
	if (!plat_priv->enable_intx) {
		ret = cnss_pci_get_user_msi_assignment(&pci_priv->pci_dev->dev,
						   QDSS_MSI_NAME,
						   &num_vectors,
						   &user_base_data,
						   &base_vector);
		if (ret)
			return ret;

		cnss_pr_dbg("Number of assigned MSI for QDSS is %d, base vector is %d\n",
				num_vectors, base_vector);
	}

	for (i = 0; i < num_vectors; i++) {
		if (!plat_priv->enable_intx) {
			qdss_irq = cnss_pci_get_msi_irq(&pci_priv->pci_dev->dev,
						  base_vector + i);
		} else {
			cnss_pr_err("QDSS msi irq failed\n");
		}
	}
	pci_priv->qdss_irq = qdss_irq;

	return 0;
}

static int cnss_pci_disable_qdss_msi(struct cnss_pci_data *pci_priv)
{
	if (pci_priv->qdss_irq)
		free_irq(pci_priv->qdss_irq, pci_priv);

	return 0;
}
static int cnss_pci_get_mhi_msi(struct cnss_pci_data *pci_priv)
{
	int ret = 0, num_vectors, i;
	u32 user_base_data, base_vector;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	int *irq;

	num_vectors = 2;
	if (!plat_priv->enable_intx) {
		ret = cnss_pci_get_user_msi_assignment(&pci_priv->pci_dev->dev,
						   MHI_MSI_NAME,
						   &num_vectors,
						   &user_base_data,
						   &base_vector);
		if (ret)
			return ret;

		cnss_pr_dbg("Number of assigned MSI for MHI is %d, base vector is %d\n",
				num_vectors, base_vector);
	}

	irq = kcalloc(num_vectors, sizeof(int), GFP_KERNEL);
	if (!irq)
		return -ENOMEM;

	for (i = 0; i < num_vectors; i++) {
		if (!plat_priv->enable_intx) {
			irq[i] = cnss_pci_get_msi_irq(&pci_priv->pci_dev->dev,
						  base_vector + i);
		} else {
			char interrupt_name[5];

			snprintf(interrupt_name, sizeof(interrupt_name),
				 "mhi%d", i);
			irq[i] = platform_get_irq_byname(plat_priv->plat_dev,
							 interrupt_name);
		}
	}

	pci_priv->mhi_ctrl->irq = irq;
#ifdef CONFIG_CNSS2_KERNEL_MSM
	pci_priv->mhi_ctrl->msi_allocated = num_vectors;
#else
	pci_priv->mhi_ctrl->nr_irqs = num_vectors;
#endif

	return 0;
}

static void cnss_update_soc_version(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct mhi_controller *mhi_ctrl = pci_priv->mhi_ctrl;

	plat_priv->device_version.family_number = mhi_ctrl->family_number;
	plat_priv->device_version.device_number = mhi_ctrl->device_number;
	plat_priv->device_version.major_version = mhi_ctrl->major_version;
	plat_priv->device_version.minor_version = mhi_ctrl->minor_version;

	cnss_pr_info("SOC VERSION INFO: Family num: 0x%x, Device num: 0x%x, Major Ver: 0x%x, Minor Ver: 0x%x\n",
		     plat_priv->device_version.family_number,
		     plat_priv->device_version.device_number,
		     plat_priv->device_version.major_version,
		     plat_priv->device_version.minor_version);
}

static irqreturn_t qdss_irq_handler(int irq, void *context)
{
	struct cnss_pci_data *pci_priv = (struct cnss_pci_data *)context;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct qdss_stream_data *qdss_stream = &plat_priv->qdss_stream;
	u32 val;
	unsigned long flags;

	cnss_pci_reg_read(plat_priv, PCIE_SOC_PCIE_REG_PCIE_SCRATCH_0, &val);
	spin_lock_irqsave(&qdss_lock, flags);
	atomic_add(val, &qdss_stream->seq_no);
	schedule_work(&qdss_stream->qld_stream_work);
	spin_unlock_irqrestore(&qdss_lock, flags);

	return IRQ_HANDLED;
}

static int cnss_pci_register_mhi(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct pci_dev *pci_dev = pci_priv->pci_dev;
	struct mhi_controller *mhi_ctrl;
#ifdef CONFIG_CNSS2_KERNEL_MSM
	char cnss_mhi_log_buf_name[20];
#endif
#ifndef CONFIG_CNSS2_SMMU
	struct device_node *dev_node;
	struct resource memory;
#endif

#ifdef CONFIG_CNSS2_KERNEL_MSM
	mhi_ctrl = mhi_alloc_controller(0);
#else
	mhi_ctrl = mhi_alloc_controller();
#endif
	if (!mhi_ctrl) {
		cnss_pr_err("Invalid MHI controller context\n");
		return -EINVAL;
	}

	pci_priv->mhi_ctrl = mhi_ctrl;
#if (KERNEL_VERSION(5, 15, 0) > LINUX_VERSION_CODE)
	mhi_ctrl->dev_id = pci_priv->device_id;
#endif
#ifdef CONFIG_CNSS2_KERNEL_MSM
	mhi_ctrl->priv_data = pci_priv;
	mhi_ctrl->dev = &pci_dev->dev;
	mhi_ctrl->of_node = (&plat_priv->plat_dev->dev)->of_node;
	mhi_ctrl->domain = pci_domain_nr(pci_dev->bus);
	mhi_ctrl->bus = pci_dev->bus->number;
	mhi_ctrl->slot = PCI_SLOT(pci_dev->devfn);
	mhi_ctrl->len = pci_resource_len(pci_priv->pci_dev, PCI_BAR_NUM);
	cnss_pr_dbg("BAR length %x\n", mhi_ctrl->len);

	mhi_ctrl->link_status = cnss_mhi_link_status;
	mhi_ctrl->rddm_supported = true;
#else
#ifndef CONFIG_CNSS2_KERNEL_5_15
	dev_set_drvdata(&pci_dev->dev, pci_priv);
#endif
	mhi_ctrl->cntrl_dev = &pci_dev->dev;
	mhi_ctrl->read_reg = cnss_mhi_read_reg;
	mhi_ctrl->write_reg = cnss_mhi_write_reg;
	cnss_pci_mhi_config.timeout_ms *= timeout_factor;
#endif

	cnss_pr_dbg("Setting MHI fw image %s\n", plat_priv->firmware_name);
	mhi_ctrl->fw_image = plat_priv->firmware_name;
	mhi_ctrl->regs = pci_priv->bar;
#if (KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE)
	mhi_ctrl->reg_len = pci_resource_len(pci_priv->pci_dev, PCI_BAR_NUM);
#endif
	cnss_pr_dbg("BAR starts at %pa\n",
		    &pci_resource_start(pci_priv->pci_dev, PCI_BAR_NUM));

	ret = cnss_pci_get_mhi_msi(pci_priv);
	if (ret) {
		cnss_pr_err("Failed to get MSI for MHI\n");
		goto free_mhi_ctrl;
	}

	if ((pci_dev->device == QCN9224_DEVICE_ID) ||
		(pci_dev->device == QCN9000_DEVICE_ID)) {
		ret = cnss_pci_get_qdss_msi(pci_priv);
		if (ret) {
			cnss_pr_err("Failed to get MSI for QDSS\n");
			goto free_mhi_ctrl;
		}

		if (pci_priv->qdss_irq) {
			ret = request_irq(pci_priv->qdss_irq, qdss_irq_handler,
					IRQF_SHARED, "qdss", pci_priv);
			if (ret) {
				cnss_pr_err("dummy request_irq fails %d\n",
						ret);
				goto free_mhi_ctrl;
			}
		}
	}
#ifdef CONFIG_CNSS2_SMMU
	if (pci_priv->smmu_s1_enable) {
		mhi_ctrl->iova_start = pci_priv->smmu_iova_start;
		mhi_ctrl->iova_stop = pci_priv->smmu_iova_start +
					pci_priv->smmu_iova_len;
	} else {
		mhi_ctrl->iova_start = 0;
		mhi_ctrl->iova_stop = pci_priv->dma_bit_mask;
	}
#else
	dev_node = of_find_node_by_type(NULL, "memory");
	if (dev_node) {
		if (of_address_to_resource(dev_node, 0, &memory)) {
			cnss_pr_err("%s: Unable to get resource: memory",
				    __func__);
			goto free_qdss_irq;
		}

		mhi_ctrl->iova_start = (dma_addr_t)memory.start;
		mhi_ctrl->iova_stop = (dma_addr_t)(memory.start +
						  (resource_size(&memory) - 1));
	} else {
		/* No Memory DT node, assign full 32-bit region as iova */
		mhi_ctrl->iova_start = 0;
		mhi_ctrl->iova_stop = 0xFFFFFFFF;
	}
#endif

	mhi_ctrl->status_cb = cnss_mhi_notify_status;
	mhi_ctrl->runtime_get = cnss_mhi_pm_runtime_get;
	mhi_ctrl->runtime_put = cnss_mhi_pm_runtime_put_noidle;

	mhi_ctrl->rddm_size = pci_priv->plat_priv->ramdump_info_v2.ramdump_size;
	mhi_ctrl->sbl_size = SZ_512K;
	mhi_ctrl->seg_len = SZ_512K;
	mhi_ctrl->fbc_download = true;

#ifdef CONFIG_CNSS2_KERNEL_MSM
	snprintf(cnss_mhi_log_buf_name, sizeof(cnss_mhi_log_buf_name),
		 "cnss-mhi_%x", plat_priv->wlfw_service_instance_id);

	mhi_ctrl->log_buf = ipc_log_context_create(CNSS_IPC_LOG_PAGES,
					(const char *)cnss_mhi_log_buf_name, 0);
	if (!mhi_ctrl->log_buf)
		cnss_pr_info("MHI IPC Logging is disabled!\n");

	ret = of_register_mhi_controller(mhi_ctrl);
#else
	ret = mhi_register_controller(mhi_ctrl, &cnss_pci_mhi_config);
#endif

	if (ret) {
		cnss_pr_err("Failed to register to MHI bus, err = %d\n", ret);
		CNSS_ASSERT(0);
		goto free_qdss_irq;
	}

#if (KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE)
	mhi_ctrl->rddm_prealloc = false;
	mhi_ctrl->rddm_seg_len = SZ_4K;
#endif
	cnss_update_soc_version(pci_priv);

	return 0;

free_qdss_irq:
	if ((pci_dev->device == QCN9224_DEVICE_ID) ||
		(pci_dev->device == QCN9000_DEVICE_ID))
		cnss_pci_disable_qdss_msi(pci_priv);
free_mhi_ctrl:
	mhi_free_controller(mhi_ctrl);
	pci_priv->mhi_ctrl = NULL;
	return ret;
}

static void cnss_pci_unregister_mhi(struct cnss_pci_data *pci_priv)
{
	struct mhi_controller *mhi_ctrl = pci_priv->mhi_ctrl;

#ifdef CONFIG_CNSS2_KERNEL_MSM
	mhi_unregister_mhi_controller(mhi_ctrl);
	ipc_log_context_destroy(mhi_ctrl->log_buf);
#else
	mhi_unregister_controller(mhi_ctrl);
#endif
	kfree(mhi_ctrl->irq);
}

static void cnss_pci_free_mhi_controller(struct cnss_pci_data *pci_priv)
{
	struct mhi_controller *mhi_ctrl = pci_priv->mhi_ctrl;

	mhi_free_controller(mhi_ctrl);
	pci_priv->mhi_ctrl = NULL;
}

static void cnss_boot_debug_timeout_hdlr(struct timer_list *timer)
{
	struct cnss_plat_data *plat_priv = NULL;
	struct cnss_pci_data *pci_priv = from_timer(pci_priv, timer,
						    boot_debug_timer);

	if (!pci_priv)
		return;

	plat_priv = pci_priv->plat_priv;
	if (cnss_pci_check_link_status(pci_priv))
		return;

	if (cnss_pci_is_device_down(&pci_priv->pci_dev->dev))
		return;

	if (test_bit(CNSS_MHI_POWER_ON, &pci_priv->mhi_state))
		return;

	if (test_bit(CNSS_MHI_MISSION_MODE, &pci_priv->mhi_state)) {
		cnss_pr_err("%s: %s is already in Mission mode\n",
			    __func__, plat_priv->device_name);
		return;
	}

	if (cnss_mhi_scan_rddm_cookie(pci_priv,
				 DEVICE_RDDM_COOKIE))
		return;

	cnss_pr_dbg("Dump MHI/PBL/SBL debug data every %ds during MHI power on\n",
		    BOOT_DEBUG_TIMEOUT_MS / 1000);

	cnss_mhi_debug_reg_dump(pci_priv);
	cnss_pci_dump_bl_sram_mem(pci_priv);

	mod_timer(&pci_priv->boot_debug_timer,
		  jiffies + msecs_to_jiffies(BOOT_DEBUG_TIMEOUT_MS));
}

#ifdef CONFIG_CNSS2_DMA_ALLOC
static
int cnss_pci_of_reserved_mem_device_init(struct cnss_plat_data *plat_priv)
{
	struct pci_dev *pci_dev = (struct pci_dev *)plat_priv->pci_dev;
	struct device *dev = &pci_dev->dev;
	int ret;

	/* Use of_reserved_mem_device_init_by_idx() if reserved memory is
	 * attached to platform device of_node.
	 */
	ret = of_reserved_mem_device_init(dev);
	if (ret)
		cnss_pr_err("Failed to init reserved mem device, err = %d\n",
			    ret);
	if (dev->cma_area)
		cnss_pr_info("CMA area is %s\n",
			    cma_get_name(dev->cma_area));

	return ret;
}
#endif

#ifdef CONFIG_CNSS2_KERNEL_5_15
void cnss_set_pci_link_speed_width(struct device *dev, u16 link_speed,
					u16 link_width)
{
}
EXPORT_SYMBOL(cnss_set_pci_link_speed_width);
#else
void cnss_set_pci_link_speed_width(struct device *dev, u16 link_speed,
					u16 link_width)
{
	struct cnss_plat_data *plat_priv;
	struct pci_dev *root_port, *pci_dev;
	struct cnss_pci_data *pci_priv;
#if defined(CONFIG_CNSS2_QCOM_KERNEL_DEPENDENCY) && IS_ENABLED(CONFIG_PCIE_QCOM)
	int ret = 0;
#endif

	plat_priv = cnss_bus_dev_to_plat_priv(dev);
	if (!plat_priv) {
		cnss_pr_err("The plat_priv is NULL\n");
		return;
	}

	pci_dev = plat_priv->pci_dev;
	if (!pci_dev) {
		cnss_pr_err("Pci dev is NULL\n");
		return;
	}

	pci_priv = cnss_get_pci_priv(pci_dev);
	if (!pci_priv) {
		cnss_pr_err("Pci priv is NULL\n");
		return;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0))
	root_port = pci_find_pcie_root_port(pci_priv->pci_dev);
	if (!root_port) {
		cnss_pr_info("The root port is NULL\n");
		return;
	}
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
	root_port = pcie_find_root_port(pci_priv->pci_dev);
	if (!root_port) {
		cnss_pr_info("The root port is NULL\n");
		return;
	}
#endif

#if defined(CONFIG_CNSS2_QCOM_KERNEL_DEPENDENCY) && IS_ENABLED(CONFIG_PCIE_QCOM)
	cnss_pr_dbg("Selected link speed is %d, link_width is %d\n",
			link_speed, link_width);
	ret = pcie_set_link_speed(root_port, link_speed);
	if (ret)
		cnss_pr_err("%s Failed to set link speed %d\n", __func__, ret);
	else
		cnss_pr_info("%s The PCI Generation is %d\n", __func__,
				link_speed);

	ret = pcie_set_link_width(root_port, link_width);
	if (ret)
		cnss_pr_err("%s Failed to set link width %d\n", __func__, ret);
	else
		cnss_pr_info("%s The PCI link width is %d\n", __func__,
				link_width);
#endif
}
EXPORT_SYMBOL(cnss_set_pci_link_speed_width);
#endif

int cnss_pci_probe(struct pci_dev *pci_dev,
		   const struct pci_device_id *id,
		   struct cnss_plat_data *plat_priv)
{
	int ret = 0;
	u32 val = 0;
	struct cnss_pci_data *pci_priv;
	if (!pci_dev) {
		pr_err("%s: ERROR: PCI device is NULL\n", __func__);
		return -EINVAL;
	}

	if (!plat_priv) {
		pr_err("%s +%d plat_priv is NULL\n", __func__, __LINE__);
		return -EINVAL;
	}

	cnss_pr_dbg("PCI is probing, vendor ID: 0x%x, device ID: 0x%x\n",
		    id->vendor, pci_dev->device);

	pci_priv = devm_kzalloc(&pci_dev->dev, sizeof(*pci_priv),
				GFP_KERNEL);
	if (!pci_priv) {
		ret = -ENOMEM;
		goto out;
	}

#ifdef CONFIG_CNSS2_KERNEL_MSM
	pci_dev->no_d3hot = true;
#endif
	pci_priv->pci_link_state = PCI_LINK_UP;
	pci_priv->plat_priv = plat_priv;
	pci_priv->pci_dev = pci_dev;
	pci_priv->driver_ops = plat_priv->driver_ops;
	pci_priv->pci_device_id = id;
	pci_priv->device_id = pci_dev->device;
	cnss_set_pci_priv(pci_dev, pci_priv);
	plat_priv->device_id = pci_dev->device;
	plat_priv->bus_priv = pci_priv;
	reinit_completion(&plat_priv->soc_reset_request_complete);

#ifdef CONFIG_CNSS2_LEGACY_IRQ
	if (plat_priv->enable_intx) {
		pci_priv->os_legacy_irq =
			platform_get_irq_byname(plat_priv->plat_dev, "inta");
		if (pci_priv->os_legacy_irq < 0) {
			pr_err("ERR: error identifying legacy irq ERR %d\n",
			       pci_priv->os_legacy_irq);
			return -EINVAL;
		}

		if (qcn9224_register_legacy_irq(plat_priv->lvirq,
						pci_priv->os_legacy_irq)) {
			pr_err("ERR: error registering legacy irq\n");
			return -EINVAL;
		}
	}
#endif

	ret = cnss_register_ramdump(plat_priv);
	if (ret)
		goto unregister_subsys;

#ifdef CONFIG_CNSS2_SMMU
	ret = cnss_pci_init_smmu(pci_priv);
	if (ret)
		goto unregister_ramdump;
#endif

#ifdef CONFIG_CNSS2_PCI_MSM
	ret = cnss_reg_pci_event(pci_priv);
	if (ret) {
		cnss_pr_err("Failed to register PCI event, err = %d\n", ret);
		goto deinit_smmu;
	}
#endif

	ret = cnss_pci_enable_bus(pci_priv);
	if (ret)
		goto dereg_pci_event;

	pci_save_state(pci_dev);
	pci_priv->default_state = pci_store_saved_state(pci_dev);

	switch (pci_dev->device) {
	case QCA6174_DEVICE_ID:
		pci_read_config_word(pci_dev, QCA6174_REV_ID_OFFSET,
				     &pci_priv->revision_id);
		ret = cnss_suspend_pci_link(pci_priv);
		if (ret)
			cnss_pr_err("Failed to suspend PCI link, err = %d\n",
				    ret);
		cnss_power_off_device(plat_priv, 0);
		break;
	case QCN9224_DEVICE_ID:
		cnss_pci_reg_read(plat_priv,
				  QCN9224_QFPROM_RAW_RFA_PDET_ROW13_LSB,
				  &val);
		pci_priv->otp_board_id = (val & OTP_BOARD_ID_MASK);
		cnss_pr_dbg("%s: OTP fused board id is 0x%x\n",
			    __func__, pci_priv->otp_board_id);
		/* fall through */
	case QCN9000_EMULATION_DEVICE_ID:
	case QCN9000_DEVICE_ID:
	case QCA6390_DEVICE_ID:
	case QCA6490_DEVICE_ID:
		timer_setup(&pci_priv->dev_rddm_timer,
			    cnss_dev_rddm_timeout_hdlr, 0);
		timer_setup(&pci_priv->boot_debug_timer,
			    cnss_boot_debug_timeout_hdlr, 0);
		INIT_DELAYED_WORK(&pci_priv->time_sync_work,
				  cnss_pci_time_sync_work_hdlr);

		if (!plat_priv->enable_intx) {
			ret = cnss_pci_enable_msi(pci_priv);
			if (ret)
				goto disable_bus;
#ifdef CONFIG_CNSS2_LEGACY_IRQ
		} else {
			cnss_pci_enable_legacy_intx(pci_priv->bar, pci_dev);
#endif
		}

		ret = cnss_pci_register_mhi(pci_priv);
		if (ret) {
			cnss_pci_disable_msi(pci_priv);
			goto disable_bus;
		}
		cnss_pci_get_link_status(pci_priv);
		if (EMULATION_HW)
			break;
		ret = cnss_suspend_pci_link(pci_priv);
		if (ret)
			cnss_pr_err("Failed to suspend PCI link, err = %d\n",
				    ret);
		cnss_power_off_device(plat_priv, plat_priv->device_id);
		break;
	default:
		cnss_pr_err("Unknown PCI device found: 0x%x\n",
			    pci_dev->device);
		ret = -ENODEV;
		goto disable_bus;
	}

	return 0;

disable_bus:
	cnss_pci_disable_bus(pci_priv);
dereg_pci_event:
#ifdef CONFIG_CNSS2_PCI_MSM
	cnss_dereg_pci_event(pci_priv);
deinit_smmu:
#endif
#ifdef CONFIG_CNSS2_SMMU
	cnss_pci_deinit_smmu(pci_priv);
unregister_ramdump:
#endif
	cnss_unregister_ramdump(plat_priv);
unregister_subsys:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0))
	cnss_unregister_subsys(plat_priv);
#else
	cnss_bus_dev_shutdown(plat_priv);
#endif
	plat_priv->bus_priv = NULL;
out:
	return ret;
}
EXPORT_SYMBOL(cnss_pci_probe);


void cnss_pci_remove(struct pci_dev *pci_dev)
{
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	struct cnss_plat_data *plat_priv =
		cnss_bus_dev_to_plat_priv(&pci_dev->dev);

	if (!plat_priv) {
		pr_err("%s: plat_priv is NULL", __func__);
		return;
	}

	/* For the platforms that support dma_alloc, FW mem free won't
	 * happen during wifi down
	 */
	if (!plat_priv->dma_alloc_supported) {
		cnss_bus_free_fw_mem(plat_priv);
		cnss_bus_free_qdss_mem(plat_priv);
	}
	switch (pci_dev->device) {
	case QCN9000_EMULATION_DEVICE_ID:
	case QCN9000_DEVICE_ID:
	case QCN9224_DEVICE_ID:
	case QCA6390_DEVICE_ID:
	case QCA6490_DEVICE_ID:
		cnss_pci_unregister_mhi(pci_priv);
		cnss_pci_disable_qdss_msi(pci_priv);
		cnss_pci_disable_msi(pci_priv);
		del_timer(&pci_priv->boot_debug_timer);
		del_timer(&pci_priv->dev_rddm_timer);
		break;
	default:
		break;
	}

	pci_load_and_free_saved_state(pci_dev, &pci_priv->default_state);

	cnss_pci_disable_bus(pci_priv);
#ifdef CONFIG_CNSS2_LEGACY_IRQ
	if (plat_priv->enable_intx) {
		qcn9224_unregister_legacy_irq(plat_priv->lvirq,
					      pci_priv->os_legacy_irq);
	}
#endif
#ifdef CONFIG_CNSS2_PCI_MSM
	cnss_dereg_pci_event(pci_priv);
#endif
#ifdef CONFIG_CNSS2_SMMU
	if (pci_priv->smmu_mapping)
		cnss_pci_deinit_smmu(pci_priv);
#endif
#ifdef CONFIG_CNSS2_KERNEL_IPQ
	cnss_unregister_ramdump(plat_priv);
#endif
	cnss_pci_free_mhi_controller(pci_priv);
	plat_priv->bus_priv = NULL;
}
EXPORT_SYMBOL(cnss_pci_remove);

static const struct pci_device_id cnss_pci_id_table[] = {
	{ QCATHR_VENDOR_ID, QCA6174_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID },
	{ QCATHR_VENDOR_ID, QCN9000_EMULATION_DEVICE_ID,
		PCI_ANY_ID, PCI_ANY_ID },
	{ QCN_VENDOR_ID, QCN9000_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID },
	{ QCN_VENDOR_ID, QCN9224_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID },
	{ QCN_VENDOR_ID, QCA6390_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID },
	{ QCN_VENDOR_ID, QCA6490_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, cnss_pci_id_table);

static const struct dev_pm_ops cnss_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cnss_pci_suspend, cnss_pci_resume)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(cnss_pci_suspend_noirq,
				      cnss_pci_resume_noirq)
	SET_RUNTIME_PM_OPS(cnss_pci_runtime_suspend, cnss_pci_runtime_resume,
			   cnss_pci_runtime_idle)
};

int cnss_pci_get_bar_info(struct cnss_pci_data *pci_priv, void __iomem **va,
			  phys_addr_t *pa)
{
	if (!pci_priv)
		return -ENODEV;

	*va = pci_priv->bar;
	*pa = pci_resource_start(pci_priv->pci_dev, PCI_BAR_NUM);
	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
static int cnss_get_qrtr_instance_id(struct pci_dev *pci_dev, u32 *node_id)
{
	struct cnss_plat_data *plat_priv = NULL;
	*node_id = (pci_domain_nr(pci_dev->bus) & 0xF);

	switch (pci_dev->device) {
	case QCN9000_DEVICE_ID:
		*node_id = *node_id + QCN9000_0;
		break;
	case QCN9224_DEVICE_ID:
		*node_id = *node_id + QCN9224_0;
		break;
	default:
		cnss_pr_dbg("Invalid device id 0x%lx",
			     (unsigned long)pci_dev->device);
		break;
	}

	return 0;
}
#else
static int cnss_get_qrtr_instance_id(struct pci_dev *pci_dev, u32 *node_id)
{
	int ret;

	ret = of_property_read_u32(pci_dev->dev.of_node,
				   "qrtr_node_id", node_id);
	if (ret) {
		pr_err("Failed to get Instance ID %d\n", ret);
		return ret;
	}

	return 0;
}
#endif

int cnss_pci_probe_basic(struct pci_dev *pci_dev,
			 const struct pci_device_id *id)
{
	struct cnss_plat_data *plat_priv = NULL;
	u32 qrtr_instance = 0;
	int ret;

	ret = of_property_read_u32(pci_dev->dev.of_node,
				   "qrtr_instance_id", &qrtr_instance);
	if (ret) {
		/* Temporarily look for qrtr_node_id as fallback for QCN9224
		 * till we have a better way to link pci_dev to plat_priv
		 */
		if (cnss_get_qrtr_instance_id(pci_dev, &qrtr_instance))
			return ret;
	}

	plat_priv = cnss_get_plat_priv_by_qrtr_node_id(qrtr_instance);
	if (!plat_priv) {
		pr_err("cnss_pci_probe_basic plat_priv is NULL!\n");
		return -EINVAL;
	}

	plat_priv->pci_dev = (struct platform_device *)pci_dev;
	plat_priv->pci_dev_id = (struct platform_device_id *)id;

#ifdef CONFIG_CNSS2_DMA_ALLOC
	ret = cnss_pci_of_reserved_mem_device_init(plat_priv);
	if (ret) {
		cnss_pr_err("Failed to initialize reserved mem %d", ret);
		return ret;
	}
#endif

#if defined(CONFIG_CNSS2_KERNEL_MSM) || defined(CONFIG_TARGET_SDX75)
	cnss_pr_info("Taking PM vote for %s", plat_priv->device_name);
	device_set_wakeup_enable(&pci_dev->dev, true);
	pm_stay_awake(&pci_dev->dev);
#endif

	cnss_pr_info("PCI device %pK probed successfully\n",
		     plat_priv->pci_dev);

	ret = cnss_pci_alloc_m3_mem(plat_priv);
	if (ret) {
		cnss_pr_err("%s: Failed to allocate M3 mem\n", __func__);
		return ret;
	}

	return 0;
}

void cnss_pci_remove_basic(struct pci_dev *pci_dev)
{
	struct cnss_plat_data *plat_priv = NULL;
	u32 qrtr_instance = 0;
	int ret;

	ret = of_property_read_u32(pci_dev->dev.of_node,
				   "qrtr_instance_id", &qrtr_instance);
	if (ret) {
		/* Temporarily look for qrtr_node_id as fallback for QCN9224
		 * till we have a better way to link pci_dev to plat_priv
		 */
		if (cnss_get_qrtr_instance_id(pci_dev, &qrtr_instance))
			return;
	}

	plat_priv = cnss_get_plat_priv_by_qrtr_node_id(qrtr_instance);
	if (!plat_priv) {
		cnss_pr_err("%s plat_priv is NULL!\n", __func__);
		return;
	}

	cnss_pr_info("Removing PCI device %p\n", plat_priv->pci_dev);
	cnss_pci_free_m3_mem(plat_priv);
	plat_priv->pci_dev_id = NULL;
	plat_priv->pci_dev = NULL;
}

struct pci_driver cnss_pci_driver = {
	.name     = "cnss_pci",
	.id_table = cnss_pci_id_table,
	.probe    = cnss_pci_probe_basic,
	.remove   = cnss_pci_remove_basic,
	.driver = {
		.pm = &cnss_pm_ops,
	},
};

int cnss_pci_init(struct cnss_plat_data *plat_priv)
{
	int ret = 0;
#ifdef CONFIG_CNSS2_PCI_MSM
	struct device *dev = &plat_priv->plat_dev->dev;
	u32 rc_num;

	ret = of_property_read_u32(dev->of_node, "qcom,wlan-rc-num", &rc_num);
	if (ret) {
		cnss_pr_err("Failed to find PCIe RC number, err = %d\n", ret);
		goto out;
	}

	ret = msm_pcie_enumerate(rc_num);
	if (ret) {
		cnss_pr_err("Failed to enable PCIe RC%x, err = %d\n",
			    rc_num, ret);
		goto out;
	}
#endif
	if ((plat_priv && plat_priv->pci_dev) || cnss_pci_registered)
		return 0;

	ret = pci_register_driver(&cnss_pci_driver);
	if (ret) {
		cnss_pr_err("Failed to register to PCI framework, err = %d\n",
			    ret);
		goto out;
	}
	cnss_pci_registered = true;

	return 0;
out:
	return ret;
}

void cnss_pci_deinit(struct cnss_plat_data *plat_priv)
{
	if (cnss_pci_registered) {

		pci_unregister_driver(&cnss_pci_driver);
		cnss_pci_registered = false;
	}
}

static int cnss_pci_load_m3_cb(struct cnss_plat_data *plat_priv)
{
	if (!plat_priv)
		return -ENODEV;

	return cnss_pci_load_m3(plat_priv->bus_priv);
}

static int cnss_pci_alloc_qdss_mem_cb(struct cnss_plat_data *plat_priv)
{
	if (!plat_priv)
		return -ENODEV;

	return cnss_pci_alloc_qdss_mem(plat_priv->bus_priv);
}

static u32 cnss_pci_get_wake_irq(struct cnss_plat_data *plat_priv)
{
	if (!plat_priv)
		return -ENODEV;

	return cnss_pci_get_wake_msi(plat_priv->bus_priv);
}

static int cnss_pci_force_fw_assert_hdlr_cb(struct cnss_plat_data *plat_priv)
{
	if (!plat_priv)
		return -ENODEV;

	return cnss_pci_force_fw_assert_hdlr(plat_priv->bus_priv);
}

static void cnss_pci_fw_boot_timeout_hdlr_cb(struct timer_list *timer)
{
	struct cnss_plat_data *plat_priv =
			from_timer(plat_priv, timer, fw_boot_timer);
	if (!plat_priv)
		return;

	cnss_pci_fw_boot_timeout_hdlr(plat_priv->bus_priv);
}

static void cnss_pci_collect_dump_info_cb(struct cnss_plat_data *plat_priv,
					bool in_panic)
{
	if (!plat_priv)
		return;

	return cnss_pci_collect_dump_info(plat_priv->bus_priv, in_panic);
}

static int cnss_pci_call_driver_probe_cb(struct cnss_plat_data *plat_priv)
{
	if (!plat_priv)
		return -ENODEV;

	return cnss_pci_call_driver_probe(plat_priv->bus_priv);
}

static int cnss_pci_call_driver_remove_cb(struct cnss_plat_data *plat_priv)
{
	if (!plat_priv)
		return -ENODEV;

	return cnss_pci_call_driver_remove(plat_priv->bus_priv);
}

static int cnss_pci_dev_powerup_cb(struct cnss_plat_data *plat_priv)
{
	if (!plat_priv)
		return -ENODEV;

	return cnss_pci_dev_powerup(plat_priv->bus_priv);
}

static int cnss_pci_dev_shutdown_cb(struct cnss_plat_data *plat_priv)
{
	if (!plat_priv)
		return -ENODEV;

	return cnss_pci_dev_shutdown(plat_priv->bus_priv);
}

static int cnss_pci_dev_crash_shutdown_cb(struct cnss_plat_data *plat_priv)
{
	if (!plat_priv)
		return -ENODEV;

	return cnss_pci_dev_crash_shutdown(plat_priv->bus_priv);
}

static int cnss_pci_dev_ramdump_cb(struct cnss_plat_data *plat_priv)
{
	if (!plat_priv)
		return -ENODEV;

	return cnss_pci_dev_ramdump(plat_priv->bus_priv);
}

static int cnss_pci_register_driver_hdlr_cb(struct cnss_plat_data *plat_priv,
					void *data)
{
	if (!plat_priv)
		return -ENODEV;

	return cnss_pci_register_driver_hdlr(plat_priv->bus_priv, data);
}

static int cnss_pci_unregister_driver_hdlr_cb(struct cnss_plat_data *plat_priv)
{
	if (!plat_priv)
		return -ENODEV;

	return cnss_pci_unregister_driver_hdlr(plat_priv->bus_priv);
}

static int cnss_pci_call_driver_modem_status_cb
		(struct cnss_plat_data *plat_priv, int modem_current_status)
{
	if (!plat_priv)
		return -ENODEV;

	return cnss_pci_call_driver_modem_status(plat_priv->bus_priv,
							 modem_current_status);
}

static int cnss_pci_update_status_cb(struct cnss_plat_data *plat_priv,
			   enum cnss_driver_status status)
{
	if (!plat_priv)
		return -ENODEV;

	return cnss_pci_update_status(plat_priv->bus_priv, status);
}

static u64 cnss_pci_get_q6_time_cb(struct device *dev)
{
	struct cnss_plat_data *plat_priv = cnss_bus_dev_to_plat_priv(dev);
	struct cnss_pci_data *pci_priv;

	if (!plat_priv) {
		pr_err("Plat Priv is null\n");
		return 0;
	}

	if (!test_bit(CNSS_FW_READY, &plat_priv->driver_state)) {
		cnss_pr_err("Invalid state to get the Q6 timestamp: 0x%lx\n",
			    plat_priv->driver_state);
		return 0;
	}

	pci_priv = plat_priv->bus_priv;

	if (!pci_priv) {
		cnss_pr_dbg("Pci Priv is null\n");
		return 0;
	}
	return cnss_pci_get_q6_time(pci_priv);
}

static int cnss_pci_reg_read_cb(struct device *dev, u32 addr, u32 *val,
			void __iomem *base)
{
	struct cnss_plat_data *plat_priv = cnss_bus_dev_to_plat_priv(dev);
	struct cnss_pci_data *pci_priv;

	if (!plat_priv) {
		pr_err("Plat Priv is null\n");
		return -ENODEV;
	}

	pci_priv = plat_priv->bus_priv;

	if (!pci_priv) {
		cnss_pr_err("Pci Priv is null\n");
		return -ENODEV;
	}

	if (cnss_pci_reg_read(plat_priv, addr, val)) {
		cnss_pr_err("PCI register read failed.\n");
		return -EIO;
	}

	return 0;
}

static int cnss_pci_reg_write_cb(struct device *dev, u32 addr, u32 val,
			void __iomem *base)
{
	struct cnss_plat_data *plat_priv = cnss_bus_dev_to_plat_priv(dev);
	struct cnss_pci_data *pci_priv;

	if (!plat_priv) {
		pr_err("Plat Priv is null\n");
		return -ENODEV;
	}

	pci_priv = plat_priv->bus_priv;

	if (!pci_priv) {
		cnss_pr_err("Pci Priv is null\n");
		return -ENODEV;
	}

	return cnss_pci_reg_write(pci_priv, addr, val);
}

static
int cnss_pci_get_soc_info(struct device *dev, struct cnss_soc_info *info)
{
	struct cnss_plat_data *plat_priv = cnss_bus_dev_to_plat_priv(dev);
	struct cnss_pci_data *pci_priv;

	if (!plat_priv)
		return -ENODEV;

	pci_priv = cnss_get_pci_priv(to_pci_dev(dev));

	if (!pci_priv)
		return -ENODEV;

	info->va = pci_priv->bar;
	info->pa = pci_resource_start(pci_priv->pci_dev, PCI_BAR_NUM);

	memcpy(&info->device_version, &plat_priv->device_version,
	       sizeof(info->device_version));

	memcpy(&info->dev_mem_info, &plat_priv->dev_mem_info,
	       sizeof(info->dev_mem_info));

	return 0;
}

static void cnss_pci_free_fw_mem(struct cnss_plat_data *plat_priv)
{
	struct cnss_pci_data *pci_priv = NULL;
	struct cnss_fw_mem *fw_mem = NULL;
	struct device *dev = NULL;
	int i;

	if (!plat_priv) {
		cnss_pr_err("%s: plat_priv is NULL\n", __func__);
		return;
	}

	fw_mem = plat_priv->fw_mem;
	pci_priv = plat_priv->bus_priv;
	if (!pci_priv) {
		cnss_pr_err("%s: pci_priv is NULL\n", __func__);
		return;
	}

	dev = &pci_priv->pci_dev->dev;
	if (plat_priv->dma_alloc_supported) {
		for (i = 0; i < plat_priv->fw_mem_seg_len; i++) {
			if (fw_mem[i].va && fw_mem[i].size) {
				cnss_pr_dbg("Freeing memory for FW, va: 0x%pK, pa: 0x%pa, size: 0x%zx, type: %u\n",
					    fw_mem[i].va, &fw_mem[i].pa,
					    fw_mem[i].size, fw_mem[i].type);
				dma_free_attrs(dev, fw_mem[i].size,
					       fw_mem[i].va, fw_mem[i].pa,
					       DMA_ATTR_FORCE_CONTIGUOUS);
				fw_mem[i].va = NULL;
				fw_mem[i].pa = 0;
				fw_mem[i].size = 0;
				fw_mem[i].type = 0;
			}
		}
		goto out;
	}

	for (i = 0; i < plat_priv->fw_mem_seg_len; i++) {
		if (fw_mem[i].va) {
			cnss_pr_dbg("Freeing FW mem of type %d\n",
				    fw_mem[i].type);
			if (fw_mem[i].type == QMI_WLFW_AFC_MEM_V01) {
				afc_memset(plat_priv, fw_mem[i].va, 0,
					   AFC_MEM_SIZE);
			} else if (fw_mem[i].type ==
				   QMI_WLFW_MLO_GLOBAL_MEM_V01) {
				/* Do not iounmap or reset */
			} else {
				iounmap(fw_mem[i].va);
				fw_mem[i].va = NULL;
				fw_mem[i].size = 0;
			}
		}
	}
out:
	plat_priv->fw_mem_seg_len = 0;
}

static void cnss_pci_free_qdss_mem(struct cnss_plat_data *plat_priv)
{
	int i;
	struct cnss_fw_mem *qdss_mem;
	struct qdss_stream_data *qdss_stream;
	struct cnss_pci_data *pci_priv;
	struct device *dev;

	if (!plat_priv) {
		cnss_pr_err("%s: plat_priv is NULL\n", __func__);
		return;
	}

	qdss_mem = plat_priv->qdss_mem;
	qdss_stream = &plat_priv->qdss_stream;

	pci_priv = plat_priv->bus_priv;

	if (!pci_priv) {
		cnss_pr_err("%s: pci_priv is NULL\n", __func__);
		return;
	}

	dev = &pci_priv->pci_dev->dev;

	if (plat_priv->dma_alloc_supported) {
		for (i = 0; i < plat_priv->qdss_mem_seg_len; i++) {
			if (qdss_mem[i].va && qdss_mem[i].size) {
				cnss_pr_dbg("Freeing memory for QDSS, va: 0x%pK, pa: 0x%pa, size: 0x%zx, type: %u\n",
					    qdss_mem[i].va, &qdss_mem[i].pa,
					    qdss_mem[i].size, qdss_mem[i].type);
				dma_free_attrs(dev, qdss_mem[i].size,
					       qdss_mem[i].va, qdss_mem[i].pa,
					       DMA_ATTR_FORCE_CONTIGUOUS);
				qdss_mem[i].va = NULL;
				qdss_mem[i].pa = 0;
				qdss_mem[i].size = 0;
				qdss_mem[i].type = 0;
			}
		}
	}

	for (i = 0; i < plat_priv->qdss_mem_seg_len; i++) {
		if (plat_priv->qdss_etr_sg_mode) {
			cnss_etr_sg_tbl_free(
				(uint32_t *)qdss_stream->qdss_vaddr,
				plat_priv,
				DIV_ROUND_UP(qdss_mem[i].size, PAGE_SIZE));
		} else {
			if (qdss_mem[i].va) {
				cnss_pr_dbg("Freeing QDSS Memory\n");
				iounmap(qdss_mem[i].va);
				qdss_mem[i].va = NULL;
				qdss_mem[i].size = 0;
			}
		}
	}

	plat_priv->qdss_mem_seg_len = 0;
}

static void cnss_pci_get_msi_address(struct device *dev, u32 *msi_addr_low,
			  u32 *msi_addr_high)
{
	struct pci_dev *pci_dev = NULL;
	struct cnss_plat_data *plat_priv = cnss_bus_dev_to_plat_priv(dev);

	if (!plat_priv) {
		cnss_pr_err("%s: plat_priv is NULL", __func__);
		return;
	}

	pci_dev = to_pci_dev(dev);
	pci_read_config_dword(pci_dev, pci_dev->msi_cap +
			      PCI_MSI_ADDRESS_LO, msi_addr_low);
	pci_read_config_dword(pci_dev, pci_dev->msi_cap +
			      PCI_MSI_ADDRESS_HI, msi_addr_high);

	/* Since q6 supports only 32 bit addresses, mask the msi_addr_high
	 * value. If this is programmed into the register, q6 interprets it
	 * as an internal address and causes unwanted writes/reads.
	 */
	*msi_addr_high = 0;
}

static struct cnss_bus_ops pci_ops = {
	.cnss_bus_init = cnss_pci_init,
	.cnss_bus_deinit = cnss_pci_deinit,
	.cnss_bus_alloc_fw_mem = cnss_pci_alloc_fw_mem,
	.cnss_bus_free_fw_mem = cnss_pci_free_fw_mem,
	.cnss_bus_alloc_qdss_mem = cnss_pci_alloc_qdss_mem_cb,
	.cnss_bus_free_qdss_mem = cnss_pci_free_qdss_mem,
	.cnss_bus_driver_probe = cnss_pci_call_driver_probe_cb,
	.cnss_bus_driver_remove = cnss_pci_call_driver_remove_cb,
	.cnss_bus_dev_powerup = cnss_pci_dev_powerup_cb,
	.cnss_bus_dev_shutdown = cnss_pci_dev_shutdown_cb,
	.cnss_bus_load_m3 = cnss_pci_load_m3_cb,
	.cnss_bus_get_wake_irq = cnss_pci_get_wake_irq,
	.cnss_bus_force_fw_assert_hdlr = cnss_pci_force_fw_assert_hdlr_cb,
	.cnss_bus_fw_boot_timeout_hdlr = cnss_pci_fw_boot_timeout_hdlr_cb,
	.cnss_bus_dev_crash_shutdown = cnss_pci_dev_crash_shutdown_cb,
	.cnss_bus_collect_dump_info = cnss_pci_collect_dump_info_cb,
	.cnss_bus_dev_ramdump = cnss_pci_dev_ramdump_cb,
	.cnss_bus_register_driver_hdlr = cnss_pci_register_driver_hdlr_cb,
	.cnss_bus_unregister_driver_hdlr = cnss_pci_unregister_driver_hdlr_cb,
	.cnss_bus_driver_modem_status =
				cnss_pci_call_driver_modem_status_cb,
	.cnss_bus_update_status = cnss_pci_update_status_cb,
	.cnss_bus_reg_read = cnss_pci_reg_read_cb,
	.cnss_bus_reg_write = cnss_pci_reg_write_cb,
	.cnss_bus_get_soc_info = cnss_pci_get_soc_info,
	.cnss_bus_get_q6_time = cnss_pci_get_q6_time_cb,
	.cnss_bus_get_msi_irq = cnss_pci_get_msi_irq,
	.cnss_bus_get_msi_address = cnss_pci_get_msi_address,
	.cnss_bus_get_user_msi_assignment = cnss_pci_get_user_msi_assignment,
};

struct cnss_bus_ops *cnss_pci_get_ops(void)
{
	return &pci_ops;
}
