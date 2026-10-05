#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=pkcs11-test
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

PKG_MAINTAINER:=Tropic Square
PKG_LICENSE:=GPL-2.0
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/pkcs11-test
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Simple PKCS#11 module testing utility
  URL:=https://openwrt.org
endef

define Package/pkcs11-test/description
  A lightweight utility for testing PKCS#11 modules in embedded environments.
  This is a minimal alternative to pkcs11-tool from OpenSC, designed for
  basic module testing without the full OpenSC dependency stack.
  
  Features:
  - Load and test PKCS#11 modules
  - Display module information
  - Test initialization/finalization
  - Very small footprint (~10KB)
  
  Perfect for testing libtropic-pkcs11 and other PKCS#11 modules on OpenWrt.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) \
		-o $(PKG_BUILD_DIR)/pkcs11-test \
		$(PKG_BUILD_DIR)/pkcs11-test.c \
		$(TARGET_LDFLAGS) -ldl
endef

define Package/pkcs11-test/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/pkcs11-test $(1)/usr/bin/
endef

$(eval $(call BuildPackage,pkcs11-test))
