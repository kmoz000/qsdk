include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk
include $(INCLUDE_DIR)/version.mk

PKG_NAME:=u-boot-2016
PKG_SOURCE_PROTO:=git
PKG_BRANCH:=master
PKG_RELEASE:=1

include $(INCLUDE_DIR)/local-development.mk
ifeq ($(DUMP)$(PKG_VERSION),)
  PKG_REV:=$(shell git ls-remote $(PKG_SOURCE_URL) $(PKG_BRANCH) | cut -b -7)
  PKG_VERSION:=g$(PKG_REV)
endif

PKG_BUILD_DIR:=$(KERNEL_BUILD_DIR)/$(PKG_NAME)-$(BUILD_VARIANT)/$(PKG_NAME)-$(PKG_VERSION)
CONFIG_NAME=$(shell echo $(BUILD_VARIANT) | sed 's/.*-//')
DEBUG_NAME=$(shell echo $(BUILD_VARIANT) | sed 's/-.*//')

include $(INCLUDE_DIR)/package.mk

ifneq ($(CONFIG_TARGET_ar71xx),)
KCPPFLAGS+=$(foreach var,$(strip $(shell cat ./configs/$(shell echo $(BUILD_VARIANT) | sed 's/-qca95[0-9]x//')-$(BOARD)$(if $(SUBTARGET),_$(SUBTARGET)).mk)), -D${var})
endif

HOSTLDFLAGS+= \
	      -L$(STAGING_DIR)/usr/lib \
	      -znow -zrelro -pie

define Package/uboot-2016-ipq806x
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq806x||TARGET_ipq
  TITLE:=U-boot for IPQ806x based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq806x
endef

define Package/uboot-2016-ipq806x_standard
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq806x||TARGET_ipq
  TITLE:=U-boot(Standard Variant) for IPQ806x based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq806x_standard
endef

define Package/uboot-2016-ipq807x
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq806x||TARGET_ipq807x||TARGET_ipq
  TITLE:=U-boot for IPQ807x based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq807x
endef

define Package/uboot-2016-ipq807x-debug
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq807x||TARGET_ipq
  TITLE:=U-boot debug for IPQ807x based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=debug-ipq807x
endef

define Package/uboot-2016-ipq807x_tiny
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq807x||TARGET_ipq
  TITLE:=U-boot (tiny) for IPQ807x based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq807x_tiny
endef

define Package/uboot-2016-ipq40xx
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq806x||TARGET_ipq
  TITLE:=U-boot for IPQ40xx based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq40xx
endef

define Package/uboot-2016-ipq40xx_standard
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq806x||TARGET_ipq
  TITLE:=U-boot(Standard Variant) for IPQ40xx based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq40xx_standard
endef

define Package/uboot-2016-ipq5018
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq50xx||TARGET_ipq
  TITLE:=U-boot for IPQ5018 based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq5018
endef

define Package/uboot-2016-ipq5018-debug
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq50xx||TARGET_ipq
  TITLE:=U-boot debug for IPQ5018 based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=debug-ipq5018
endef

define Package/uboot-2016-ipq5018_tiny
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq50xx||TARGET_ipq
  TITLE:=U-boot (tiny) for IPQ5018 based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq5018_tiny
endef

define Package/uboot-2016-ipq5018_tiny_debug
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq50xx||TARGET_ipq
  TITLE:=U-boot (tiny_debug) for IPQ5018 based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq5018_tiny_debug
endef

define Package/uboot-2016-ipq5332
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq53xx||TARGET_ipq
  TITLE:=U-boot for IPQ53xx based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq5332
endef

define Package/uboot-2016-ipq5332-debug
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq53xx||TARGET_ipq
  TITLE:=U-boot debug for IPQ53xx based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=debug-ipq5332
endef

define Package/uboot-2016-ipq5332_tiny
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq53xx
  TITLE:=U-boot for IPQ53xx based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq5332_tiny
endef

define Package/uboot-2016-ipq5332_tiny_nor
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq53xx
  TITLE:=U-boot for IPQ53xx based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq5332_tiny_nor
endef

define Package/uboot-2016-ipq5332_tiny_debug
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq53xx
  TITLE:=U-boot for IPQ53xx based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq5332_tiny_debug
endef

define Package/uboot-2016-ipq5332_tiny2
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq53xx
  TITLE:=U-boot for IPQ53xx based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq5332_tiny2
endef

define Package/uboot-2016-ipq6018
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq806x||TARGET_ipq||TARGET_ipq60xx
  TITLE:=U-boot for IPQ6018 based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq6018
endef

define Package/uboot-2016-ipq6018-debug
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq60xx||TARGET_ipq
  TITLE:=U-boot debug for IPQ6018 based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=debug-ipq6018
endef

define Package/uboot-2016-ipq6018_tiny
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq60xx||TARGET_ipq
  TITLE:=U-boot (tiny) for IPQ6018 based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq6018_tiny
endef

define Package/uboot-2016-ipq9574
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq95xx||TARGET_ipq
  TITLE:=U-boot for IPQ9574 based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=ipq9574
endef

define Package/uboot-2016-ipq9574-debug
  SECTION:=boot
  CATEGORY:=Boot Loaders
  DEPENDS:=@TARGET_ipq95xx||TARGET_ipq
  TITLE:=U-boot debug for IPQ9574 based platforms
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=debug-ipq9574
endef

define Package/uboot-2016-ar71xx/common
 define Package/uboot-2016-ar71xx-$(1)
  SECTION:=boot
  CATEGORY:=Boot Loaders
  TITLE:=U-boot for $(1) platform
  DEPENDS:=@TARGET_ar71xx
  URL:=http://www.denx.de/wiki/U-Boot
  VARIANT:=$(1)-$(2)
 endef
endef

define Package/fwupgrade-tools
  SECTION:=boot
  CATEGORY:=Boot Loaders
  TITLE:=U-boot images tools (dumpimage, mkimage)
  URL:=http://www.denx.de/wiki/U-Boot
  DEPENDS:=+losetup
endef

define Package/uboot-2016-ar71xx/nor
 $(call Package/uboot-2016-ar71xx/common,$(1),$(2))

 define Package/uboot-2016-ar71xx-$(1)/install
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ar71xx-u-boot-$(1).elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ar71xx-u-boot-$(1).bin
 endef
 $$(eval $$(call BuildPackage,uboot-2016-ar71xx-$(1)))
endef

ifneq ($(CONFIG_TARGET_ipq_ipq50xx),)
MAX_PAGESIZE=-z max-page-size=4096
endif

ifeq ($(CONFIG_GCC_USE_VERSION_12),y)
KCPPFLAGS += \
	-Wno-error=maybe-uninitialized \
	-Wno-error=address-of-packed-member \
	-Wno-error=attributes \
	-Wno-error=misleading-indentation \
	-Wno-error=address \
	-Wno-error=dangling-pointer
endif

SHA_FLAG = "-DUSE_SHA256"

ifneq ($(CONFIG_TARGET_ipq95xx)$(CONFIG_TARGET_ipq807x)$(CONFIG_TARGET_ipq50xx),)
  SHA_FLAG = "-DUSE_SHA384"
endif

define Build/Configure/debug
	echo "CONFIG_SKIP_RESET=y" >> $(PKG_BUILD_DIR)/configs/$(CONFIG_NAME)_defconfig;
endef

define Build/Configure
	$(call Build/Configure/$(DEBUG_NAME))
	$(MAKE) -C $(PKG_BUILD_DIR) $(CONFIG_NAME)_defconfig CROSS_COMPILE=$(TARGET_CROSS) TARGETCC=$(TARGET_CC);
endef

define Build/Compile
	VERSION_CODE='$(VERSION_NICK) $(VERSION_NUMBER)' \
	REVISION='$(REVISION)' \
	$(MAKE) -C $(PKG_BUILD_DIR) HOSTSTRIP=true CROSS_COMPILE=$(TARGET_CROSS) \
		TARGETCC="$(TARGET_CC) $(SHA_FLAG)" \
		KCPPFLAGS="$(KCPPFLAGS)" \
		HOSTLDFLAGS="$(HOSTLDFLAGS)" \
		LDFLAGS="$(MAX_PAGESIZE)" \
		DTC=${LINUX_DIR}/scripts/dtc/dtc \
		all
endef

define Package/uboot-2016-ipq806x/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq806x-u-boot-2016.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq806x-u-boot-2016.img
	$(CP) $(PKG_BUILD_DIR)/u-boot.mbn $(BIN_DIR)/openwrt-ipq806x-u-boot-2016.mbn
endef

define Package/uboot-2016-ipq806x_standard/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq806x_standard-u-boot-2016.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq806x_standard-u-boot-2016.img
	$(CP) $(PKG_BUILD_DIR)/u-boot.mbn $(BIN_DIR)/openwrt-ipq806x_standard-u-boot-2016.mbn
endef

define Package/uboot-2016-ipq807x/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq807x-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq807x-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq807x-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq807x-u-boot.img
endef

define Package/uboot-2016-ipq807x-debug/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq807x-debug-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq807x-debug-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq807x-debug-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq807x-debug-u-boot.img
endef

define Package/uboot-2016-ipq807x_tiny/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq807x_tiny-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq807x_tiny-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq807x_tiny-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq807x_tiny-u-boot.img
endef

define Package/uboot-2016-ipq40xx/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq40xx-u-boot-2016.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq40xx-u-boot-2016-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq40xx-u-boot-2016-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq40xx-u-boot-2016.img
endef

define Package/uboot-2016-ipq40xx_standard/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq40xx_standard-u-boot-2016.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq40xx_standard-u-boot-2016-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-$(BUILD_VARIANT)-u-boot-2016-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq40xx_standard-u-boot-2016.img
endef

define Package/uboot-2016-ipq5018/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5018-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5018-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq5018-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq5018-u-boot.img
endef

define Package/uboot-2016-ipq5018-debug/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5018-debug-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5018-debug-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq5018-debug-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq5018-debug-u-boot.img
endef

define Package/uboot-2016-ipq5018_tiny/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5018_tiny-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5018_tiny-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq5018_tiny-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq5018_tiny-u-boot.img
endef

define Package/uboot-2016-ipq5018_tiny_debug/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5018_tiny_debug-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5018_tiny_debug-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq5018_tiny_debug-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq5018_tiny_debug-u-boot.img
endef

define Package/uboot-2016-ipq5332/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5332-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5332-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq5332-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq5332-u-boot.img
endef

define Package/uboot-2016-ipq5332-debug/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5332-debug-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5332-debug-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq5332-debug-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq5332-debug-u-boot.img
endef

define Package/uboot-2016-ipq5332_tiny/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5332_tiny-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5332_tiny-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq5332_tiny-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq5332_tiny-u-boot.img
endef

define Package/uboot-2016-ipq5332_tiny_nor/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5332_tiny_nor-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5332_tiny_nor-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq5332_tiny_nor-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq5332_tiny_nor-u-boot.img
endef

define Package/uboot-2016-ipq5332_tiny_debug/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5332_tiny_debug-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5332_tiny_debug-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq5332_tiny_debug-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq5332_tiny_debug-u-boot.img
endef

define Package/uboot-2016-ipq5332_tiny2/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5332_tiny2-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq5332_tiny2-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq5332_tiny2-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq5332_tiny2-u-boot.img
endef

define Package/uboot-2016-ipq6018/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq6018-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq6018-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq6018-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq6018-u-boot.img
endef

define Package/uboot-2016-ipq6018-debug/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq6018-debug-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq6018-debug-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq6018-debug-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq6018-debug-u-boot.img
endef

define Package/uboot-2016-ipq9574/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq9574-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq9574-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq9574-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq9574-u-boot.img
endef

define Package/uboot-2016-ipq9574-debug/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq9574-debug-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq9574-debug-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq9574-debug-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq9574-debug-u-boot.img
endef

define Package/fwupgrade-tools/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/tools/mkimage $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/tools/dumpimage $(1)/usr/bin
endef

define Package/uboot-2016-ipq6018_tiny/install
	$(INSTALL_DIR) $(1)
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq6018_tiny-u-boot.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot $(BIN_DIR)/openwrt-ipq6018_tiny-u-boot-stripped.elf
	$(STRIP) $(BIN_DIR)/openwrt-ipq6018_tiny-u-boot-stripped.elf
	$(CP) $(PKG_BUILD_DIR)/u-boot.bin $(BIN_DIR)/openwrt-ipq6018_tiny-u-boot.img
endef

$(eval $(call BuildPackage,uboot-2016-ipq807x))
$(eval $(call BuildPackage,uboot-2016-ipq807x-debug))
$(eval $(call BuildPackage,uboot-2016-ipq807x_tiny))
$(eval $(call BuildPackage,uboot-2016-ipq40xx))
$(eval $(call BuildPackage,uboot-2016-ipq40xx_standard))
$(eval $(call BuildPackage,uboot-2016-ipq806x))
$(eval $(call BuildPackage,uboot-2016-ipq806x_standard))
$(eval $(call BuildPackage,uboot-2016-ipq5018))
$(eval $(call BuildPackage,uboot-2016-ipq5018-debug))
$(eval $(call BuildPackage,uboot-2016-ipq5018_tiny))
$(eval $(call BuildPackage,uboot-2016-ipq5018_tiny_debug))
$(eval $(call BuildPackage,uboot-2016-ipq5332))
$(eval $(call BuildPackage,uboot-2016-ipq5332-debug))
$(eval $(call BuildPackage,uboot-2016-ipq5332_tiny))
$(eval $(call BuildPackage,uboot-2016-ipq5332_tiny_nor))
$(eval $(call BuildPackage,uboot-2016-ipq5332_tiny_debug))
$(eval $(call BuildPackage,uboot-2016-ipq5332_tiny2))
$(eval $(call BuildPackage,uboot-2016-ipq6018))
$(eval $(call BuildPackage,uboot-2016-ipq6018-debug))
$(eval $(call BuildPackage,uboot-2016-ipq9574))
$(eval $(call BuildPackage,uboot-2016-ipq9574-debug))
$(eval $(call BuildPackage,uboot-2016-ipq6018_tiny))
$(eval $(call BuildPackage,fwupgrade-tools))
$(eval $(call Package/uboot-2016-ar71xx/nor,ap147-8M,qca953x))
$(eval $(call Package/uboot-2016-ar71xx/nor,ap147-16M,qca953x))
$(eval $(call Package/uboot-2016-ar71xx/nor,ap135,qca955x))
$(eval $(call Package/uboot-2016-ar71xx/nor,ap152-8M,qca956x))
$(eval $(call Package/uboot-2016-ar71xx/nor,ap152-16M,qca956x))
$(eval $(call Package/uboot-2016-ar71xx/nor,ap151-8M,qca956x))
$(eval $(call Package/uboot-2016-ar71xx/nor,ap151-16M,qca956x))

