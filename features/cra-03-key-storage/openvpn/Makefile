#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=openvpn
PKG_VERSION:=2.6.12
PKG_RELEASE:=1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://swupdate.openvpn.net/community/releases/
PKG_HASH:=1c610fddeb686e34f1367c347e027e418e07523a10f4d8ce4a2c2af2f61a1929

PKG_CPE_ID:=cpe:/a:openvpn:openvpn
PKG_MAINTAINER:=OpenWrt Team
PKG_LICENSE:=GPL-2.0-only
PKG_LICENSE_FILES:=COPYING

PKG_FIXUP:=autoreconf
PKG_INSTALL:=1
PKG_BUILD_PARALLEL:=1
PKG_BUILD_FLAGS:=gc-sections

include $(INCLUDE_DIR)/package.mk

define Package/openvpn/default
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=VPN
  TITLE:=OpenVPN
  URL:=https://openvpn.net/
  DEPENDS:=+libpthread +kmod-tun
endef

define Package/openvpn_openssl
  $(Package/openvpn/default)
  TITLE += (openssl+pkcs11)
  DEPENDS += +libopenssl +pkcs11-helper +PACKAGE_libtropic-pkcs11:libtropic-pkcs11
  VARIANT:=openssl
  DEFAULT_VARIANT:=1
endef

define Package/openvpn_openssl/description
  OpenVPN built with OpenSSL and pkcs11-helper, enabling hardware-backed
  TLS client authentication through the TROPIC01 secure element.

  Private keys are generated inside TROPIC01 and never exposed. OpenVPN
  accesses them via /usr/lib/libtropic_pkcs11.so at runtime.

  See /etc/openvpn/tropic01-pkcs11-client.conf for a complete example.
endef

define Package/openvpn_openssl/conffiles
/etc/openvpn/
/etc/config/openvpn
endef

define Package/openvpn_wolfssl
  $(Package/openvpn/default)
  TITLE += (wolfssl)
  DEPENDS += +libwolfssl
  VARIANT:=wolfssl
endef

define Package/openvpn_wolfssl/description
  OpenVPN built with wolfSSL for a smaller footprint.
  PKCS11 hardware-key support is NOT available in this variant.
endef

define Package/openvpn_wolfssl/conffiles
/etc/openvpn/
/etc/config/openvpn
endef

# OpenVPN's configure.ac uses PKG_CHECK_MODULES for libcap-ng and treats a
# pkg-config failure as a hard error even when LIBCAPNG_CFLAGS/LIBS are
# overridden with empty strings.  We create stub files so pkg-config is
# satisfied and compilation succeeds; the stubs are no-ops at runtime since
# the router runs openvpn as root and musl handles privilege dropping itself.
define Build/Prepare
	$(call Build/Prepare/Default)
	$(INSTALL_DIR) $(STAGING_DIR)/usr/lib/pkgconfig
	if [ ! -f $(STAGING_DIR)/usr/lib/pkgconfig/libcap-ng.pc ]; then \
		printf 'Name: libcap-ng\nDescription: Capabilities library (stub)\nVersion: 0.8.0\nCflags:\nLibs:\n' \
			> $(STAGING_DIR)/usr/lib/pkgconfig/libcap-ng.pc; \
	fi
	$(INSTALL_DIR) $(STAGING_DIR)/usr/include
	if [ ! -f $(STAGING_DIR)/usr/include/cap-ng.h ]; then \
		printf '%s\n' \
			'/* Stub cap-ng.h for OpenWrt cross-compilation. */' \
			'/* OpenVPN uses libcap-ng for privilege dropping; on this target */' \
			'/* openvpn runs as root so all capng calls are safe no-ops.      */' \
			'#ifndef CAP_NG_H' '#define CAP_NG_H' \
			'#include <sys/types.h>' \
			'#define CAPNG_SELECT_BOTH   3' \
			'#define CAPNG_ADD           1' \
			'#define CAPNG_EFFECTIVE     1' \
			'#define CAPNG_PERMITTED     2' \
			'#define CAPNG_DROP_SUPP_GRP 4' \
			'#ifndef CAP_NET_ADMIN' '#define CAP_NET_ADMIN  12' '#endif' \
			'#ifndef CAP_SETPCAP'  '#define CAP_SETPCAP     8' '#endif' \
			'static inline void capng_clear(int s){(void)s;}' \
			'static inline int capng_update(int a,int t,unsigned c){(void)a;(void)t;(void)c;return 0;}' \
			'static inline int capng_change_id(uid_t u,gid_t g,unsigned f){(void)u;(void)g;(void)f;return 0;}' \
			'static inline int capng_have_capability(int w,unsigned c){(void)w;(void)c;return 1;}' \
			'#endif /* CAP_NG_H */' \
			> $(STAGING_DIR)/usr/include/cap-ng.h; \
	fi
endef

CONFIGURE_ARGS += \
	--disable-debug \
	--disable-lz4 \
	--disable-lzo \
	--disable-plugin-auth-pam \
	--disable-plugin-down-root \
	--disable-selinux \
	--disable-systemd \
	--enable-iproute2 \
	--enable-small \
	--with-iproute-path=/sbin/ip

ifeq ($(BUILD_VARIANT),openssl)
  CONFIGURE_ARGS += \
	--with-crypto-library=openssl \
	--enable-pkcs11 \
	--with-pkcs11-helper
  TARGET_CFLAGS += -I$(STAGING_DIR)/usr/include/pkcs11-helper-1.0
  TARGET_LDFLAGS += -lpkcs11-helper
  CONFIGURE_VARS += \
	LIBCAPNG_CFLAGS="" \
	LIBCAPNG_LIBS="" \
	PKCS11_HELPER_CFLAGS="-I$(STAGING_DIR)/usr/include/pkcs11-helper-1.0" \
	PKCS11_HELPER_LIBS="-lpkcs11-helper"
endif

ifeq ($(BUILD_VARIANT),wolfssl)
  CONFIGURE_ARGS += \
	--with-crypto-library=wolfssl \
	--disable-pkcs11
  CONFIGURE_VARS += \
	LIBCAPNG_CFLAGS="" \
	LIBCAPNG_LIBS=""
endif

define Package/openvpn_openssl/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/sbin/openvpn $(1)/usr/sbin/openvpn
	$(INSTALL_DIR) $(1)/etc/openvpn
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/openvpn.init $(1)/etc/init.d/openvpn
	$(INSTALL_DIR) $(1)/etc/hotplug.d/iface
	$(INSTALL_DATA) ./files/openvpn.hotplug $(1)/etc/hotplug.d/iface/20-openvpn
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/openvpn.config $(1)/etc/config/openvpn
	$(INSTALL_DATA) ./files/tropic01-pkcs11-client.conf \
		$(1)/etc/openvpn/tropic01-pkcs11-client.conf
endef

define Package/openvpn_wolfssl/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/sbin/openvpn $(1)/usr/sbin/openvpn
	$(INSTALL_DIR) $(1)/etc/openvpn
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/openvpn.init $(1)/etc/init.d/openvpn
	$(INSTALL_DIR) $(1)/etc/hotplug.d/iface
	$(INSTALL_DATA) ./files/openvpn.hotplug $(1)/etc/hotplug.d/iface/20-openvpn
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/openvpn.config $(1)/etc/config/openvpn
endef

$(eval $(call BuildPackage,openvpn_openssl))
$(eval $(call BuildPackage,openvpn_wolfssl))
