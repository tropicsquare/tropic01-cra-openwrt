#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=tropic01-pairing
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_MAINTAINER:=Tropic Square
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/tropic01-pairing
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=CRA Annex I 2(d) guarded provisioning of a per-unit TROPIC01 pairing key
  URL:=https://github.com/tropicsquare/tropic01-cra-openwrt
  DEPENDS:=+tropicctl
endef

define Package/tropic01-pairing/description
  Writes a unit's own pairing key into a TROPIC01 pairing slot and takes the
  published shared key (slot 0) its rights, using tropicctl. Every chip write
  is off by default and needs its own flag; the result is verified and the
  previous configuration restored if the check fails. Slot 0 is restricted,
  never invalidated. See this feature's README.md and docs/pairing-key-design.md.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

# Nothing to compile -- a script only.
define Build/Compile
endef

define Package/tropic01-pairing/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/usr/sbin/tropic01-pairing $(1)/usr/sbin/tropic01-pairing
endef

$(eval $(call BuildPackage,tropic01-pairing))
