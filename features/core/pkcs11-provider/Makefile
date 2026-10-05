#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=pkcs11-provider
PKG_VERSION:=0.6
PKG_RELEASE:=1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.xz
PKG_SOURCE_URL:=https://github.com/latchset/pkcs11-provider/releases/download/v$(PKG_VERSION)/
PKG_HASH:=f20e85c1e1ae819ddb440bdba47fa3b44943aaebaf05e59b04630126497a6ed6

PKG_MAINTAINER:=OpenWrt Team
PKG_LICENSE:=Apache-2.0
PKG_LICENSE_FILES:=LICENSES/Apache-2.0.txt

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/meson.mk

define Package/pkcs11-provider
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=OpenSSL 3.x PKCS#11 provider
  URL:=https://github.com/latchset/pkcs11-provider
  DEPENDS:=+libopenssl
endef

define Package/pkcs11-provider/description
  An OpenSSL 3.x provider that enables use of PKCS#11 tokens (hardware
  security modules, smart cards, TPMs, etc.) through the standard OpenSSL
  provider interface.  Applications can load private keys and certificates
  via pkcs11: URIs using OSSL_STORE_open() or ENGINE-equivalent calls.
endef

MESON_ARGS += \
	-Dmodulesdir=$(STAGING_DIR)/usr/lib/ossl-modules

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/lib/ossl-modules
	$(CP) $(PKG_BUILD_DIR)/openwrt-build/src/pkcs11.so \
		$(1)/usr/lib/ossl-modules/pkcs11.so
endef

define Package/pkcs11-provider/install
	$(INSTALL_DIR) $(1)/usr/lib/ossl-modules
	$(CP) $(PKG_BUILD_DIR)/openwrt-build/src/pkcs11.so \
		$(1)/usr/lib/ossl-modules/pkcs11.so
endef

$(eval $(call BuildPackage,pkcs11-provider))
