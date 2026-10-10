#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=libtropic
PKG_VERSION:=4.2.0
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/tropicsquare/libtropic.git
# Commit that tag v4.2.0 points to (git ls-remote ...refs/tags/v4.2.0^{}).
# Pinned to a commit, not the tag, because a tag can be moved or deleted.
PKG_SOURCE_VERSION:=9085ea5387a8a1d5a946c7e4f633c7134e4909a7
PKG_SOURCE_DATE:=2026-10-02
PKG_MIRROR_HASH:=7863c96ca8e5bc2a25d89f8c66e14c6a0a93e21c22f6b1c9026b9596458bad67

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
  DEPENDS:=+libopenssl
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
# are chosen by the consumer application, as libtropic intends. This package
# fetches the one pinned source, builds the core as a static library to check
# that it compiles, and installs the source tree under usr/src/libtropic so
# that lt-util and libtropic-pkcs11 build the same code instead of each
# bundling its own copy.

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
	
	# Install the sources that consumers build against. libtropic is configured by
	# compile definitions that must match between the library and its users (silicon
	# revision, retry counts, timeouts), so consumers add this tree with CMake's
	# add_subdirectory() and pick their HAL and CAL from it. One pinned source, no
	# bundled copies.
	$(INSTALL_DIR) $(1)/usr/src/libtropic
	$(CP) $(PKG_BUILD_DIR)/CMakeLists.txt $(PKG_BUILD_DIR)/LICENSE.md \
		$(PKG_BUILD_DIR)/cmake $(PKG_BUILD_DIR)/cal $(PKG_BUILD_DIR)/hal \
		$(PKG_BUILD_DIR)/include $(PKG_BUILD_DIR)/src \
		$(PKG_BUILD_DIR)/TROPIC01_fw_update_files $(1)/usr/src/libtropic/
endef

define Package/libtropic/install
	# This is a development library, no runtime files to install
	# Applications using libtropic will statically link against it
	$(INSTALL_DIR) $(1)/usr/lib
endef

$(eval $(call BuildPackage,libtropic))
