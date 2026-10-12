#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=tropic01-secure-update
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_MAINTAINER:=Tropic Square
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/tropic01-secure-update
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=CRA Annex I 2(c)/2(f) signed sysupgrade with a root key and rollback floor in TROPIC01
  URL:=https://github.com/tropicsquare/tropic01-cra-openwrt
  DEPENDS:=+tropicctl +ucert +usign +fwtool
endef

define Package/tropic01-secure-update/description
  sysupgrade accepts an image only if its ucert signature chain leads to a root
  public key held in TROPIC01 R-MEM and its signed security version is not
  below the unit's rollback floor, kept in a TROPIC01 monotonic counter. A
  refused image is marked broken, so sysupgrade -F cannot install it. The floor
  is raised at boot to the running image's security version.

  Not secure boot: writing the flash directly bypasses it. See this feature's
  README.md and docs/design.md.
endef

define Package/tropic01-secure-update/conffiles
/etc/config/tropic01-update
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

# Nothing to compile -- scripts and config only.
define Build/Compile
endef

define Package/tropic01-secure-update/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/usr/sbin/tropic01-update $(1)/usr/sbin/tropic01-update
	$(INSTALL_DIR) $(1)/lib/upgrade
	$(INSTALL_DATA) ./files/lib/upgrade/zz-tropic01-secure-update.sh $(1)/lib/upgrade/zz-tropic01-secure-update.sh
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/tropic01-update $(1)/etc/config/tropic01-update
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/tropic01-update $(1)/etc/init.d/tropic01-update
	$(INSTALL_DIR) $(1)/etc/rc.d
	$(LN) ../init.d/tropic01-update $(1)/etc/rc.d/S86tropic01-update
endef

$(eval $(call BuildPackage,tropic01-secure-update))
