#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=libtropic-pkcs11
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/tropicsquare/libtropic-pkcs11.git
# Pinned to the commit this package was actually built and tested
# against, not HEAD (which moves). NOTE: this is 6accc15, the parent of
# 3ddecde ("deps(libtropic): bump up to feature branch with ECDSA fix"),
# not 3ddecde itself -- that commit only bumps the libtropic submodule
# pointer (d037c6e) and touches nothing else, so its outer tree is
# identical to 6accc15's, but the tested build's cached tarball has the
# submodule at the *older* pin (541a7e3, 6accc15's). Verified byte-for-byte
# identical (except .gitmodules/.git) to
# dl/libtropic-pkcs11-0.1.0.tar.zst from the original fork, including the
# libtropic submodule content.
PKG_SOURCE_VERSION:=6accc15c73ab0ed454f4813bfb061d323ee4b869
PKG_SOURCE_DATE:=2026-02-19
PKG_SOURCE:=$(PKG_NAME)-$(PKG_SOURCE_DATE)-$(PKG_SOURCE_VERSION).tar.zst
PKG_MIRROR_HASH:=1a5ecde3b6892af5b55ca89fab7052ec949695f9da56d975c62ce834bbc1f2e7
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

PKG_MAINTAINER:=Tropic Square
PKG_LICENSE:=BSD-3-Clause-Clear
PKG_LICENSE_FILES:=LICENSE.md

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/libtropic-pkcs11
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=PKCS#11 module for TROPIC01 secure element
  URL:=https://github.com/tropicsquare/libtropic-pkcs11
  DEPENDS:=+libtropic +libopenssl
endef

define Package/libtropic-pkcs11/description
  A PKCS#11 Cryptoki module for the TROPIC01 secure element by Tropic Square.
  This library provides a standard cryptographic token interface (PKCS#11) to
  access TROPIC01's features including hardware random number generation, secure
  user data storage, ECC key generation (P-256 and Ed25519), and digital
  signatures (ECDSA/EdDSA). Private keys never leave the chip.
  
  This version is configured for SPI interface (MediaTek SoC integration).
  Works with standard PKCS#11 tools like pkcs11-tool, OpenSSL, and other
  cryptographic applications.
endef

# CMake configuration options
CMAKE_OPTIONS += \
	-DLT_PKCS11_LOG_EN=ON \
	-DLT_SH0_KEYS="eng_sample" \
	-DTS_SPI_DEV="/dev/spidev2.0" \
	-DTS_GPIO_DEV="/dev/gpiochip0" \
	-DTS_GPIO_CS_PIN=25 \
	-DTS_SPI_SPEED=1000000 \
	-DCMAKE_BUILD_TYPE=Release

# Libtropic is included as a submodule in libtropic-pkcs11
# The tarball source already includes the submodule content, so we don't need
# to do anything special in the prepare phase
define Build/Prepare
	$(call Build/Prepare/Default)
endef

# CMakeLists.txt doesn't have install target, we need to install manually
define Build/Install
	true
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_BUILD_DIR)/libtropic_pkcs11.so $(1)/usr/lib/
	
	$(INSTALL_DIR) $(1)/usr/include
	$(CP) $(PKG_BUILD_DIR)/src/pkcs11.h $(1)/usr/include/
endef

define Package/libtropic-pkcs11/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_BUILD_DIR)/libtropic_pkcs11.so $(1)/usr/lib/
endef

$(eval $(call BuildPackage,libtropic-pkcs11))
