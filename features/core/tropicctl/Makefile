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
endef

define Package/tropicctl/description
  tropicctl talks to a TROPIC01 secure element over Linux SPI and GPIO: chip and
  firmware information, random numbers, R-MEM data, pairing keys and the chip
  configuration, with stable exit codes, optional JSON output and a safety
  class on every command. Started under the name lt-util it accepts the old
  lt-util command line.

  Draft (phase 1 of SPEC.md): not selected in any image configuration yet.
endef

# The sources are in this directory (nothing is downloaded).
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src ./CMakeLists.txt ./LICENSE $(PKG_BUILD_DIR)/
endef

CMAKE_OPTIONS += \
	-DTROPICCTL_CHIP=libtropic \
	-DLIBTROPIC_SRC=$(STAGING_DIR)/usr/src/libtropic

define Package/tropicctl/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/tropicctl $(1)/usr/bin/tropicctl
endef

$(eval $(call BuildPackage,tropicctl))
