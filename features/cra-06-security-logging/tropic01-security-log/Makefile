#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=tropic01-security-log
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_MAINTAINER:=Tropic Square
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/tropic01-security-log
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=CRA Annex I 2(l) security logging
  URL:=https://github.com/tropicsquare/tropic01-cra-openwrt
  DEPENDS:=+logd
endef

define Package/tropic01-security-log/description
  Keeps a size-capped record of security-relevant events (logins and failed
  logins over SSH, the console and LuCI, password changes, firmware updates,
  this project's own security components) on flash, and logs which
  security-relevant files and service enablements changed (names only, never
  contents). The user can switch it off (`tropic01-security-log disable`),
  look at it (`show`) and erase it (`clear`).

  This is policy and configuration. It does not use TROPIC01 and it is not
  tamper-proof: root can edit or delete the log. See this feature's
  README.md for what it covers and what it does not.
endef

define Package/tropic01-security-log/conffiles
/etc/config/tropic01-security-log
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

# Nothing to compile -- scripts and config only.
define Build/Compile
endef

define Package/tropic01-security-log/install
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/tropic01-security-log $(1)/etc/config/tropic01-security-log
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/tropic01-security-log $(1)/etc/init.d/tropic01-security-log
	$(INSTALL_DIR) $(1)/usr/libexec
	$(INSTALL_BIN) ./files/usr/libexec/tropic01-config-audit $(1)/usr/libexec/tropic01-config-audit
	$(INSTALL_BIN) ./files/usr/libexec/tropic01-log-recorder $(1)/usr/libexec/tropic01-log-recorder
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) ./files/usr/bin/tropic01-security-log $(1)/usr/bin/tropic01-security-log
	# enabled by default: S96 symlink, same as `/etc/init.d/... enable` would make
	$(INSTALL_DIR) $(1)/etc/rc.d
	$(LN) ../init.d/tropic01-security-log $(1)/etc/rc.d/S96tropic01-security-log
endef

$(eval $(call BuildPackage,tropic01-security-log))
