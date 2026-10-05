#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=pkcs11-helper
PKG_VERSION:=1.30.0
PKG_RELEASE:=1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.bz2
PKG_SOURCE_URL:=https://github.com/OpenSC/pkcs11-helper/releases/download/$(PKG_NAME)-$(PKG_VERSION)/
PKG_HASH:=4c5815ba910cabf26df08d449ca2909daf4538c9899aa7f7fadc61229d3488a9

PKG_MAINTAINER:=OpenWrt Team
PKG_LICENSE:=BSD-3-Clause GPL-2.0-or-later
PKG_LICENSE_FILES:=COPYING

PKG_FIXUP:=autoreconf
PKG_INSTALL:=1
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/pkcs11-helper
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=PKCS#11 helper library
  URL:=https://github.com/OpenSC/pkcs11-helper
  DEPENDS:=+libopenssl
endef

define Package/pkcs11-helper/description
  pkcs11-helper is a library that simplifies the interaction with PKCS#11
  providers for applications such as OpenVPN. It handles loading of PKCS#11
  modules, session management, certificate enumeration and key-based operations
  needed for TLS mutual authentication.
endef

CONFIGURE_ARGS += \
	--disable-crypto-engine-gnutls \
	--disable-crypto-engine-nss \
	--disable-crypto-engine-mbedtls \
	--enable-crypto-engine-openssl \
	--disable-doc \
	--disable-tests

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include
	$(CP) $(PKG_INSTALL_DIR)/usr/include/pkcs11-helper-1.0 $(1)/usr/include/
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libpkcs11-helper.a $(1)/usr/lib/ 2>/dev/null || true
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libpkcs11-helper.so* $(1)/usr/lib/
	$(INSTALL_DIR) $(1)/usr/lib/pkgconfig
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/pkgconfig/libpkcs11-helper-1.pc \
		$(1)/usr/lib/pkgconfig/
endef

define Package/pkcs11-helper/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libpkcs11-helper.so.* $(1)/usr/lib/
endef

$(eval $(call BuildPackage,pkcs11-helper))
