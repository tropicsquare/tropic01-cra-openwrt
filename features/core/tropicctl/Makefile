#
# OpenWrt package definition for tropicctl. This is the only file in this directory that knows about the OpenWrt
# feed: moving tropicctl to its own repository means copying the directory and replacing Build/Prepare below with a
# pinned download (PKG_SOURCE_URL, PKG_SOURCE_VERSION, PKG_MIRROR_HASH).
#

include $(TOPDIR)/rules.mk

PKG_NAME:=tropicctl
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_MAINTAINER:=Tropic Square
PKG_LICENSE:=BSD-3-Clause
PKG_LICENSE_FILES:=LICENSE

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/tropicctl
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Command-line tool for the TROPIC01 secure element
  URL:=https://github.com/tropicsquare/tropic01-cra-openwrt
  DEPENDS:=+libtropic +libopenssl
  PROVIDES:=lt-util
endef

define Package/tropicctl/description
  tropicctl talks to a TROPIC01 secure element over Linux SPI and GPIO: chip and
  firmware information, random numbers, R-MEM data, pairing keys and the chip
  configuration, with stable exit codes, optional JSON output and a safety
  class on every command. Started under the name lt-util it accepts the old
  lt-util command line.

  Replaces the patched lt-util: the package installs an lt-util symlink for
  the compatibility mode.
endef

# The sources are in this directory (nothing is downloaded).
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src ./CMakeLists.txt ./LICENSE $(PKG_BUILD_DIR)/
endef

CMAKE_OPTIONS += \
	-DTROPICCTL_CHIP=libtropic \
	-DTROPICCTL_DEFAULT_SH0=$(or $(TROPICCTL_DEFAULT_SH0),es01) \
	-DLIBTROPIC_SRC=$(STAGING_DIR)/usr/src/libtropic

define Package/tropicctl/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/tropicctl $(1)/usr/bin/tropicctl
	# started under this name the program accepts the old lt-util command line
	$(LN) tropicctl $(1)/usr/bin/lt-util
	# a sysupgrade that keeps settings must keep the key directory too (per-unit pairing key, slot-0 key setting, vault
	# image); without this entry an upgrade deletes them and a unit with a per-unit key is locked out of its own chip
	$(INSTALL_DIR) $(1)/lib/upgrade/keep.d
	$(INSTALL_DATA) ./files/keep.d/tropicctl $(1)/lib/upgrade/keep.d/tropicctl
endef

$(eval $(call BuildPackage,tropicctl))
