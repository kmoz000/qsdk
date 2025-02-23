include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=qca-nss-ppe-rule
PKG_SOURCE_PROTO:=git
PKG_BRANCH:=master
PKG_RELEASE:=2
LOCAL_SRC:=$(TOPDIR)/qca/src/qca-nss-ppe

include $(INCLUDE_DIR)/local-development.mk
ifeq ($(DUMP)$(PKG_VERSION),)
  PKG_VERSION:=$(shell git ls-remote $(PKG_SOURCE_URL) $(PKG_BRANCH) | cut -b -7)
endif

PKG_BUILD_DEPENDS :=
MAKE_OPTS:=

include $(INCLUDE_DIR)/package.mk

ifeq ($(CONFIG_TARGET_ipq),y)
subtarget:=$(SUBTARGET)
else
subtarget:=$(CONFIG_TARGET_BOARD)
endif

define KernelPackage/qca-nss-ppe-rule
  SECTION:=kernel
  CATEGORY:=Kernel modules
  SUBMENU:=Network Devices
  TITLE:=Kernel driver for PPE rule module
  DEPENDS:=@TARGET_ipq95xx||TARGET_ipq53xx \
		+kmod-qca-nss-ppe +kmod-qca-nss-ppe-vp
  FILES:=$(PKG_BUILD_DIR)/drv/ppe_rule/qca-nss-ppe-rule.ko
  AUTOLOAD:=$(call AutoLoad,52,qca-nss-ppe-rule)
endef

define KernelPackage/qca-nss-ppe-rule/Description
   Kernel module for PPE rule driver
endef

define KernelPackage/qca-nss-ppe-rule/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) ./files/acl_dump.sh $(1)/usr/bin/
endef

define Build/InstallDev/qca-nss-ppe-rule
	$(INSTALL_DIR) $(1)/usr/include/qca-nss-ppe
	$(CP) $(PKG_BUILD_DIR)/drv/exports/* $(1)/usr/include/qca-nss-ppe/
endef

define Build/InstallDev
	$(call Build/InstallDev/qca-nss-ppe-rule,$(1))
endef

EXTRA_CFLAGS+= \
	-I$(STAGING_DIR)/usr/include/qca-ssdk \
	-I$(STAGING_DIR)/usr/include/qca-ssdk/fal \
	-I$(STAGING_DIR)/usr/include/qca-ssdk/init \

ifeq ($(CONFIG_LOWMEM_FLASH),y)
MAKE_OPTS+=PPE_LOWMEM_PROFILE_16M=y
EXTRA_CFLAGS+= -DPPE_LOWMEM_PROFILE_16M
endif

ifeq ($(CONFIG_KERNEL_IPQ_MEM_PROFILE),256)
MAKE_OPTS+=PPE_LOWMEM_PROFILE_256M=y
EXTRA_CFLAGS+= -DPPE_MEM_PROFILE_LOW_256M
endif

MAKE_OPTS+=ppe-rule=y
MAKE_OPTS+=PPE_RFS_ENABLED=y

ifneq ($(CONFIG_LOWMEM_FLASH),y)
MAKE_OPTS+=PPE_ACL_ENABLED=y
MAKE_OPTS+=PPE_POLICER_ENABLED=y
endif

ifneq ($(CONFIG_KERNEL_IPQ_MEM_PROFILE),256)
ifneq ($(CONFIG_LOWMEM_FLASH),y)
MAKE_OPTS+=PPE_PRIORITY_ENABLED=y
endif
endif

ifneq ($(CONFIG_TARGET_ipq53xx),)
MAKE_OPTS+=PPE_RULE_IPQ53XX=y
endif

ifneq ($(CONFIG_KERNEL_IPQ_MEM_PROFILE),256)
ifneq ($(CONFIG_LOWMEM_FLASH),y)
MAKE_OPTS+=PPE_MIRROR_ENABLED=y
endif
endif

define Build/Compile
	$(KERNEL_MAKE) $(strip $(MAKE_OPTS)) \
		M="$(PKG_BUILD_DIR)" \
		EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
		SoC="$(subtarget)" \
		modules
endef

$(eval $(call KernelPackage,qca-nss-ppe-rule))
