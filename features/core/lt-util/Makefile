include $(TOPDIR)/rules.mk

PKG_NAME:=lt-util
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
# Only the project itself: it no longer uses its bundled libtropic submodule (the feed
# builds one libtropic), and a submodule-inclusive tarball is not reproducible.
PKG_SOURCE_SUBMODULES:=skip
PKG_SOURCE_URL:=https://github.com/tropicsquare/libtropic-util.git
# Pinned to the commit this package was actually built and tested
# against, not HEAD (which moves). Verified byte-identical (except
# .gitmodules) to dl/lt-util-0.1.0.tar.zst from the original fork --
# including its libtropic submodule at 3093cf1.
PKG_SOURCE_VERSION:=cbc30f5ac37e7d0874df6b989d4b4be7d01d93e8
PKG_SOURCE_DATE:=2025-09-12
PKG_SOURCE:=$(PKG_NAME)-$(PKG_SOURCE_DATE)-$(PKG_SOURCE_VERSION).tar.zst
PKG_MIRROR_HASH:=8cadc90bd9b77106cd8993296fae85f907f288e204b862383e8163e244c37128
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

PKG_LICENSE:=BSD-3-Clause
PKG_LICENSE_FILES:=LICENSE
PKG_MAINTAINER:=Tropic Square

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/lt-util
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Command line utility for TROPIC01 secure element
  URL:=https://github.com/tropicsquare/libtropic-util
  DEPENDS:=+libtropic +libopenssl
endef

define Package/lt-util/description
  Command line utility for interfacing with TROPIC01 chip - a secure element
  by Tropic Square. Provides commands for random number generation, ECC key
  management, memory operations, and cryptographic operations through TROPIC01.
  
  Supports both Linux SPI and USB dongle interfaces.
endef

CMAKE_OPTIONS += \
	-DLIBTROPIC_SRC=$(STAGING_DIR)/usr/src/libtropic \
	-DCMAKE_BUILD_TYPE=Release \
	-DLT_PRINT_SPI_DATA=OFF \
	-DLT_HELPERS=ON \
	-DLT_LOG_LVL=Error

# CMakeLists.txt doesn't have install target, binary is built in-tree
define Build/Install
	true
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/lt-util $(1)/usr/bin/
endef

define Package/lt-util/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/lt-util $(1)/usr/bin/
endef

$(eval $(call BuildPackage,lt-util))
