#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=opensc
PKG_VERSION:=0.26.0
PKG_RELEASE:=1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://github.com/OpenSC/OpenSC/releases/download/$(PKG_VERSION)/
PKG_HASH:=837baead45e1505260d868871056150ede6e73d35460a470f2595a9e5e75f82b

PKG_MAINTAINER:=OpenWrt Team
PKG_LICENSE:=LGPL-2.1-or-later
PKG_LICENSE_FILES:=COPYING

PKG_FIXUP:=autoreconf
PKG_INSTALL:=1
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/opensc
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Smart card library and utilities (pkcs11-tool)
  URL:=https://github.com/OpenSC/OpenSC
  DEPENDS:=+libopenssl +zlib
endef

define Package/opensc/description
  OpenSC provides a set of libraries and utilities to work with smart cards.
  This is a minimal build that includes pkcs11-tool utility for testing
  PKCS#11 modules like libtropic-pkcs11.
  
  Built without PC/SC middleware support for reduced dependencies.
endef

define Package/libopensc
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=OpenSC PKCS#11 library (minimal build)
  URL:=https://github.com/OpenSC/OpenSC
  DEPENDS:=+libopenssl +zlib
endef

define Package/libopensc/description
  OpenSC PKCS#11 library for smart card operations.
endef

CONFIGURE_ARGS += \
	--disable-assert \
	--disable-strict \
	--disable-pedantic \
	--disable-doc \
	--enable-openssl \
	--disable-pcsc \
	--disable-readline \
	--enable-zlib \
	--disable-notify \
	--disable-sm \
	--enable-ctapi \
	--disable-openct \
	--disable-man

# Build without PC/SC middleware to reduce dependencies
# pkcs11-tool can still be used to test external PKCS#11 modules

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include
	$(CP) $(PKG_INSTALL_DIR)/usr/include/opensc $(1)/usr/include/
	$(CP) $(PKG_INSTALL_DIR)/usr/include/libopensc $(1)/usr/include/
	
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libopensc*.so* $(1)/usr/lib/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libsmm-local*.so* $(1)/usr/lib/
	
	$(INSTALL_DIR) $(1)/usr/lib/pkgconfig
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/pkgconfig/*.pc $(1)/usr/lib/pkgconfig/
endef

define Package/libopensc/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libopensc*.so* $(1)/usr/lib/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libsmm-local*.so* $(1)/usr/lib/
	
	$(INSTALL_DIR) $(1)/usr/lib/pkcs11
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/pkcs11/*.so* $(1)/usr/lib/pkcs11/ 2>/dev/null || true
endef

define Package/opensc/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/opensc-tool $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/pkcs11-tool $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/pkcs15-tool $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/opensc-asn1 $(1)/usr/bin/
	
	$(INSTALL_DIR) $(1)/etc/opensc
	$(CP) $(PKG_INSTALL_DIR)/etc/opensc.conf $(1)/etc/ 2>/dev/null || true
endef

$(eval $(call BuildPackage,libopensc))
$(eval $(call BuildPackage,opensc))
