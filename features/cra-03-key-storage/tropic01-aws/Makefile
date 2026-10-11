#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=tropic01-aws
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_MAINTAINER:=Tropic Square
PKG_LICENSE:=GPL-2.0-only

# coreMQTT is a build-time-only static library (headers + .a staged via its
# own Build/InstallDev). Without this, a from-scratch or parallel build can
# compile this package before coreMQTT has staged, and fail with a missing
# coreMQTT/*.h or -lcoreMQTT.
PKG_BUILD_DEPENDS:=coreMQTT

include $(INCLUDE_DIR)/package.mk

define Package/tropic01-aws
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=TROPIC01 AWS IoT Core client
  URL:=https://github.com/tropicsquare/libtropic-pkcs11
  DEPENDS:=+tropicctl +libtropic-pkcs11 +pkcs11-provider +libopenssl
endef

define Package/tropic01-aws/description
  AWS IoT Core MQTT client that authenticates using a private key stored
  in the TROPIC01 secure element (ECC slot 1) and a device certificate
  stored in TROPIC01 R-MEM slot 2.

  TROPIC01 operates in monopoly SPI mode: only one process may hold the
  SPI bus at a time.  The tropic01-aws-connect wrapper automatically stops
  OpenVPN (which uses ECC slot 0) before publishing and restarts it after.

  TROPIC01 slot allocation:
    ECC slot 0  / R-MEM slot 1  —  OpenVPN (reserved, do not overwrite)
    ECC slot 1  / R-MEM slot 2  —  AWS IoT Core (this package)

  Includes:
    tropic01-aws-pub       - C binary: TLS + MQTT CONNECT/PUBLISH over PKCS#11
    tropic01-aws-connect   - shell wrapper (stop OpenVPN, publish, restart OpenVPN)
    tropic01-aws-status    - show provisioning and connection status
    tropic01-aws-provision - provision ECC slot 1 key and export device cert
    openssl-aws.cnf        - isolated OpenSSL provider config (does not affect
                             system openssl.cnf or OpenVPN)
    tropic01-aws.init      - procd init script with 'publish' and 'status' commands
endef

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) \
		-I$(STAGING_DIR)/usr/include \
		-I$(STAGING_DIR)/usr/include/coreMQTT \
		-o $(PKG_BUILD_DIR)/tropic01-aws-pub \
		$(PKG_BUILD_DIR)/tropic01-aws-pub.c \
		$(TARGET_LDFLAGS) \
		-L$(STAGING_DIR)/usr/lib \
		-lcoreMQTT -lssl -lcrypto
endef

define Package/tropic01-aws/install
	# Binary
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/tropic01-aws-pub $(1)/usr/bin/

	# Shell scripts
	$(INSTALL_BIN) ./files/tropic01-aws-connect   $(1)/usr/bin/
	$(INSTALL_BIN) ./files/tropic01-aws-telemetry $(1)/usr/bin/
	$(INSTALL_BIN) ./files/tropic01-aws-status    $(1)/usr/bin/
	$(INSTALL_BIN) ./files/tropic01-aws-provision $(1)/usr/bin/

	# init.d
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/tropic01-aws.init $(1)/etc/init.d/tropic01-aws

	# UCI config (installed only if not already present)
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/tropic01-aws.config $(1)/etc/config/tropic01-aws

	# OpenSSL provider config (isolated — used only by tropic01-aws-pub)
	$(INSTALL_DIR) $(1)/etc/ssl/tropic01-aws
	$(INSTALL_DATA) ./files/openssl-aws.cnf $(1)/etc/ssl/tropic01-aws/
endef

$(eval $(call BuildPackage,tropic01-aws))
