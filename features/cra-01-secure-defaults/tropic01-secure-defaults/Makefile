#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=tropic01-secure-defaults
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_MAINTAINER:=Tropic Square
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/tropic01-secure-defaults
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=CRA Annex I(b) secure-by-default baseline
  URL:=https://github.com/tropicsquare/tropic01-cra-openwrt
  DEPENDS:=+firewall4 +dropbear +rpcd +jsonfilter
endef

define Package/tropic01-secure-defaults/description
  Ships the CRA Annex I(b) "secure-by-default configuration" baseline for
  this repo's TROPIC01 CRA reference build.

  This package intentionally does NOT claim to fix "secure by default" on
  its own -- most of that requirement is attack-surface and firewall
  policy, which no package here can compensate for if it is misconfigured
  elsewhere. See this feature's README.md for the full scope split (what
  is policy, what is hardware-adjacent, and what is not implemented yet).

  What this package actually installs:
    95-tropic01-secure-defaults - uci-defaults script (runs once, at
      first boot): re-asserts a closed-WAN firewall baseline explicitly,
      binds dropbear (SSH) to the lan interface only, switches SSH to
      key-only once a key is provisioned, and warns about WAN-facing
      firewall rules outside the stock allowlist -- so the baseline is
      auditable in one file instead of implied by whatever the image's
      stock defaults happen to be. If root still has no password (the
      shipped default -- see README.md), it also points dropbear's
      pre-auth banner and the console MOTD at banner.tropic01-insecure-
      default, so a blank password is loud instead of silent.
    profile.d/95-tropic01-force-password.sh - sourced by every interactive
      login shell: while root has no password it forces `passwd` before a
      shell is usable, then sets system.ttylogin=1 so the serial console
      asks for it afterwards. Skipped on builds carrying
      /etc/tropic01-dev-build (bench-testing opt-out, never a default).
    tropic01-console-guard (+ init.d, S99) - the shared check. "No password"
      includes "" stored as a real hash, which rpcd is asked about; such an
      account is blanked so the forced flow applies. Also enforces
      ttylogin=1 at every boot and removes the first-boot blank warnings.

  The per-unit default-credential generator
  (host-tools/tropic01-gen-credential.sh) is a build-host script, not
  part of this package -- see README.md. It must run before an image is
  flashed, not on the router itself.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

# Nothing to compile -- this package only installs a uci-defaults script.
# Without this override, package.mk's default Build/Compile runs `make`
# against the (empty) PKG_BUILD_DIR and fails with "No targets specified
# and no makefile found."
define Build/Compile
endef

define Package/tropic01-secure-defaults/install
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/95-tropic01-secure-defaults $(1)/etc/uci-defaults/95-tropic01-secure-defaults
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_DATA) ./files/etc/banner.tropic01-insecure-default $(1)/etc/banner.tropic01-insecure-default
	$(INSTALL_DIR) $(1)/etc/profile.d
	$(INSTALL_DATA) ./files/etc/profile.d/95-tropic01-force-password.sh $(1)/etc/profile.d/95-tropic01-force-password.sh
	$(INSTALL_DIR) $(1)/usr/libexec
	$(INSTALL_BIN) ./files/usr/libexec/tropic01-console-guard $(1)/usr/libexec/tropic01-console-guard
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/tropic01-console-guard $(1)/etc/init.d/tropic01-console-guard
	# enabled by default: S99 symlink, same as `/etc/init.d/... enable` would make
	$(INSTALL_DIR) $(1)/etc/rc.d
	$(LN) ../init.d/tropic01-console-guard $(1)/etc/rc.d/S99tropic01-console-guard
endef

$(eval $(call BuildPackage,tropic01-secure-defaults))
