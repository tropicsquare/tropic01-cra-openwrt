include $(TOPDIR)/rules.mk

PKG_NAME:=lt-util
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/tropicsquare/libtropic-util.git
# Pinned to the commit this package was actually built and tested
# against, not HEAD (which moves). Verified byte-identical (except
# .gitmodules) to dl/lt-util-0.1.0.tar.zst from the original fork --
# including its libtropic submodule at 3093cf1.
PKG_SOURCE_VERSION:=cbc30f5ac37e7d0874df6b989d4b4be7d01d93e8
PKG_SOURCE_DATE:=2025-09-12
PKG_SOURCE:=$(PKG_NAME)-$(PKG_SOURCE_DATE)-$(PKG_SOURCE_VERSION).tar.zst
PKG_MIRROR_HASH:=de29f4380f9fe0b17dd1cc17bd17d787b08ce30f93e11a6a3244156f2148cd7b
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
  DEPENDS:=+libtropic
endef

define Package/lt-util/description
  Command line utility for interfacing with TROPIC01 chip - a secure element
  by Tropic Square. Provides commands for random number generation, ECC key
  management, memory operations, and cryptographic operations through TROPIC01.
  
  Supports both Linux SPI and USB dongle interfaces.
endef

CMAKE_OPTIONS += \
	-DLINUX_SPI=ON \
	-DCMAKE_BUILD_TYPE=Debug \
	-DLT_PRINT_SPI_DATA=1 \
	-DLT_BUILD_EXAMPLES=OFF \
	-DLT_BUILD_TESTS=OFF \
	-DLT_HELPERS=ON \
	-DLT_LOG_LVL=None \
	-DLT_USE_TREZOR_CRYPTO=ON

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
