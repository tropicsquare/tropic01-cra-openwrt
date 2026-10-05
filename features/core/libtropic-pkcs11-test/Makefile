#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=libtropic-pkcs11-test
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

PKG_MAINTAINER:=Tropic Square
PKG_LICENSE:=GPL-2.0
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/libtropic-pkcs11-test
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Comprehensive PKCS#11 test utility for libtropic-pkcs11
  URL:=https://openwrt.org
  DEPENDS:=+libtropic-pkcs11
endef

define Package/libtropic-pkcs11-test/description
  Comprehensive test utility for libtropic-pkcs11 PKCS#11 module.
  Tests all PKCS#11 functionality including:
  - Module initialization and information
  - Slot and token enumeration
  - Mechanism discovery
  - Random number generation (various sizes)
  - Session management
  - Performance testing
  
  Perfect for validating libtropic-pkcs11 integration with TROPIC01 secure element.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include \
		-o $(PKG_BUILD_DIR)/libtropic-pkcs11-test \
		$(PKG_BUILD_DIR)/libtropic-pkcs11-test.c \
		$(TARGET_LDFLAGS) -ldl
endef

define Package/libtropic-pkcs11-test/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/libtropic-pkcs11-test $(1)/usr/bin/
endef

$(eval $(call BuildPackage,libtropic-pkcs11-test))
