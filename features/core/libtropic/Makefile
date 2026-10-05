#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=libtropic
PKG_VERSION:=3.0.0
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/tropicsquare/libtropic.git
# Commit that tag v3.0.0 points to (git ls-remote ...refs/tags/v3.0.0^{}).
# Pinned to a commit, not the tag, because a tag can be moved or deleted;
# verified byte-identical to the tarball this package was built and
# tested against (dl/libtropic-3.0.0.tar.zst from the original fork).
PKG_SOURCE_VERSION:=e730ebfb585483be2347e8a96d1722806ca8d2ca
PKG_SOURCE_DATE:=2025-12-02
PKG_MIRROR_HASH:=c20b2aaad028ffb8ae5f2edec1630f6eea0e428b93877414991d00ba2e21c805

PKG_MAINTAINER:=Tropic Square
PKG_LICENSE:=BSD-3-Clause-Clear
PKG_LICENSE_FILES:=LICENSE.md

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/libtropic
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=TROPIC01 SDK library
  URL:=https://github.com/tropicsquare/libtropic
  DEPENDS:=+libmbedtls
endef

define Package/libtropic/description
  Libtropic is a C SDK library for application development with TROPIC01
  secure element. It provides APIs for secure communication, cryptographic
  operations, and hardware abstraction for TROPIC01 chip integration.
endef

# CMake configuration options
CMAKE_OPTIONS += \
	-DLT_BUILD_EXAMPLES=OFF \
	-DLT_BUILD_TESTS=OFF \
	-DLT_HELPERS=ON \
	-DLT_LOG_LVL=None \
	-DLT_USE_INT_PIN=OFF \
	-DLT_SEPARATE_L3_BUFF=OFF \
	-DLT_PRINT_SPI_DATA=OFF

# Note: HAL (Hardware Abstraction Layer) and CAL (Crypto Abstraction Layer)
# need to be configured by the consumer application as per libtropic design.
# This package builds libtropic as a static library without HAL/CAL.

define Build/Install
	# No install target in CMake, library is built in-tree
	true
endef

define Build/InstallDev
	# Install headers
	$(INSTALL_DIR) $(1)/usr/include/libtropic
	$(CP) $(PKG_BUILD_DIR)/include/*.h $(1)/usr/include/libtropic/
	
	# Install library
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_BUILD_DIR)/libtropic.a $(1)/usr/lib/
	
	# Install CAL (Crypto Abstraction Layer) headers and sources for mbedtls
	$(INSTALL_DIR) $(1)/usr/include/libtropic/cal/mbedtls_v4
	$(CP) $(PKG_BUILD_DIR)/cal/mbedtls_v4/*.h $(1)/usr/include/libtropic/cal/mbedtls_v4/ 2>/dev/null || true
	$(CP) $(PKG_BUILD_DIR)/cal/mbedtls_v4/*.c $(1)/usr/include/libtropic/cal/mbedtls_v4/ 2>/dev/null || true
	$(CP) $(PKG_BUILD_DIR)/cal/mbedtls_v4/CMakeLists.txt $(1)/usr/include/libtropic/cal/mbedtls_v4/ 2>/dev/null || true
	
	# Install HAL (Hardware Abstraction Layer) for Linux SPI
	$(INSTALL_DIR) $(1)/usr/include/libtropic/hal/linux/spi
	$(CP) $(PKG_BUILD_DIR)/hal/linux/spi/*.h $(1)/usr/include/libtropic/hal/linux/spi/ 2>/dev/null || true
	$(CP) $(PKG_BUILD_DIR)/hal/linux/spi/*.c $(1)/usr/include/libtropic/hal/linux/spi/ 2>/dev/null || true
	$(CP) $(PKG_BUILD_DIR)/hal/linux/spi/CMakeLists.txt $(1)/usr/include/libtropic/hal/linux/spi/ 2>/dev/null || true
	
	# Install HAL for POSIX systems (TCP)
	$(INSTALL_DIR) $(1)/usr/include/libtropic/hal/posix/tcp
	$(CP) $(PKG_BUILD_DIR)/hal/posix/tcp/*.h $(1)/usr/include/libtropic/hal/posix/tcp/ 2>/dev/null || true
	$(CP) $(PKG_BUILD_DIR)/hal/posix/tcp/*.c $(1)/usr/include/libtropic/hal/posix/tcp/ 2>/dev/null || true
	$(CP) $(PKG_BUILD_DIR)/hal/posix/tcp/CMakeLists.txt $(1)/usr/include/libtropic/hal/posix/tcp/ 2>/dev/null || true
	
	# Install HAL for POSIX systems (USB dongle)
	$(INSTALL_DIR) $(1)/usr/include/libtropic/hal/posix/usb_dongle
	$(CP) $(PKG_BUILD_DIR)/hal/posix/usb_dongle/*.h $(1)/usr/include/libtropic/hal/posix/usb_dongle/ 2>/dev/null || true
	$(CP) $(PKG_BUILD_DIR)/hal/posix/usb_dongle/*.c $(1)/usr/include/libtropic/hal/posix/usb_dongle/ 2>/dev/null || true
	$(CP) $(PKG_BUILD_DIR)/hal/posix/usb_dongle/CMakeLists.txt $(1)/usr/include/libtropic/hal/posix/usb_dongle/ 2>/dev/null || true
endef

define Package/libtropic/install
	# This is a development library, no runtime files to install
	# Applications using libtropic will statically link against it
	$(INSTALL_DIR) $(1)/usr/lib
endef

$(eval $(call BuildPackage,libtropic))
