#
# Copyright (C) 2026 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=coreMQTT
PKG_VERSION:=2.3.1
PKG_RELEASE:=1

# The previous "$(PKG_NAME)-v$(PKG_VERSION).tar.gz" filename does not exist
# on GitHub's archive service and 404s; the actual asset for tag v2.3.1 is
# named "v2.3.1.tar.gz". Verified byte-identical (same hash) to the tarball
# this package was originally built and tested against.
PKG_SOURCE:=v$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://github.com/FreeRTOS/coreMQTT/archive/refs/tags/
PKG_HASH:=b8e95044e6ef8381610949b7fe546c5ddf4e52989ce9938209d5dd5f3371b5d7
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

PKG_MAINTAINER:=OpenWrt Team
PKG_LICENSE:=MIT
PKG_LICENSE_FILES:=LICENSE

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/coreMQTT
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=FreeRTOS coreMQTT MQTT 3.1.1 client library
  URL:=https://github.com/FreeRTOS/coreMQTT
endef

define Package/coreMQTT/description
  coreMQTT is a client library for MQTT 3.1.1, maintained by AWS/FreeRTOS.
  It provides serialization/deserialization of MQTT packets and a state machine
  for QoS 0/1/2 publish flows. The library is transport-agnostic: callers
  supply a TransportInterface_t with send/recv callbacks (typically backed by
  OpenSSL TLS sockets).
  Installs: libcoreMQTT.a + headers under /usr/include/coreMQTT/
endef

# GitHub releases the tarball with a "v" prefix in the tag name but the
# extracted directory is named coreMQTT-2.3.1 (no v).
PKG_UNPACK:=$(HOST_TAR) -C $(BUILD_DIR) -xzf $(DL_DIR)/$(PKG_SOURCE)

define Build/Configure
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) \
		-DMQTT_DO_NOT_USE_CUSTOM_CONFIG \
		-I$(PKG_BUILD_DIR)/source/include \
		-I$(PKG_BUILD_DIR)/source/interface \
		-c $(PKG_BUILD_DIR)/source/core_mqtt.c \
		-o $(PKG_BUILD_DIR)/core_mqtt.o
	$(TARGET_CC) $(TARGET_CFLAGS) \
		-DMQTT_DO_NOT_USE_CUSTOM_CONFIG \
		-I$(PKG_BUILD_DIR)/source/include \
		-I$(PKG_BUILD_DIR)/source/interface \
		-c $(PKG_BUILD_DIR)/source/core_mqtt_serializer.c \
		-o $(PKG_BUILD_DIR)/core_mqtt_serializer.o
	$(TARGET_CC) $(TARGET_CFLAGS) \
		-DMQTT_DO_NOT_USE_CUSTOM_CONFIG \
		-I$(PKG_BUILD_DIR)/source/include \
		-I$(PKG_BUILD_DIR)/source/interface \
		-c $(PKG_BUILD_DIR)/source/core_mqtt_state.c \
		-o $(PKG_BUILD_DIR)/core_mqtt_state.o
	$(TARGET_AR) rcs $(PKG_BUILD_DIR)/libcoreMQTT.a \
		$(PKG_BUILD_DIR)/core_mqtt.o \
		$(PKG_BUILD_DIR)/core_mqtt_serializer.o \
		$(PKG_BUILD_DIR)/core_mqtt_state.o
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_BUILD_DIR)/libcoreMQTT.a $(1)/usr/lib/

	$(INSTALL_DIR) $(1)/usr/include/coreMQTT
	$(CP) $(PKG_BUILD_DIR)/source/include/*.h $(1)/usr/include/coreMQTT/
	$(CP) $(PKG_BUILD_DIR)/source/interface/transport_interface.h \
		$(1)/usr/include/coreMQTT/
endef

# coreMQTT is a static library — headers and .a are only needed at build time.
# The runtime package is empty; dependent packages link statically.
define Package/coreMQTT/install
	true
endef

$(eval $(call BuildPackage,coreMQTT))
