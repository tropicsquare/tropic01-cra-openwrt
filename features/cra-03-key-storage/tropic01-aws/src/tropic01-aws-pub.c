/*
 * tropic01-aws-pub — AWS IoT Core MQTT publisher over mTLS using coreMQTT
 *
 * Uses OpenSSL 3.x OSSL_STORE to load the private key from TROPIC01 ECC slot 1
 * via the pkcs11-provider (activated through OPENSSL_CONF pointing at
 * openssl-aws.cnf).  The device certificate is loaded from a PEM file that was
 * written to /etc/ssl/tropic01-aws/device.pem at provisioning time.
 *
 * The MQTT layer is implemented using FreeRTOS coreMQTT v2.3.1.  The TLS
 * socket (OpenSSL SSL*) is wrapped in a TransportInterface_t so coreMQTT
 * drives all packet serialisation and the CONNACK/PUBACK state machine.
 *
 * Usage:
 *   tropic01-aws-pub <endpoint> <port> <ca_file> <cert_pem> <key_uri>
 *                    <client_id> <topic> <payload>
 *
 * Example:
 *   OPENSSL_CONF=/etc/ssl/tropic01-aws/openssl-aws.cnf \
 *   tropic01-aws-pub \
 *     abc123.iot.eu-west-1.amazonaws.com \
 *     8883 \
 *     /etc/ssl/tropic01-aws/AmazonRootCA1.pem \
 *     /etc/ssl/tropic01-aws/device.pem \
 *     "pkcs11:id=%01;type=private" \
 *     tropic01-router \
 *     "dt/tropic01-router/messages/events/" \
 *     '{"status":"ok"}'
 *
 * AWS IoT Core specifics:
 *  - Port 8883: standard MQTT over TLS 1.2+
 *  - SNI must match the endpoint hostname
 *  - ALPN not required on port 8883 (required on 443)
 *  - Clean session, QoS 0, no persistence needed for telemetry
 *
 * Build:
 *   $(TARGET_CC) -o tropic01-aws-pub tropic01-aws-pub.c \
 *     -I$(STAGING_DIR)/usr/include/coreMQTT \
 *     -lcoreMQTT -lssl -lcrypto
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/store.h>
#include <openssl/x509.h>

/* coreMQTT — transport-agnostic MQTT 3.1.1 library */
#include "core_mqtt.h"

/* --------------------------------------------------------------------------
 * coreMQTT transport interface: wrap OpenSSL SSL* send/recv
 * ------------------------------------------------------------------------*/

/*
 * NetworkContext holds the live SSL connection.  This is the opaque
 * pointer that coreMQTT passes back into our send/recv callbacks.
 */
struct NetworkContext {
    SSL *ssl;
};

static int32_t tls_send(NetworkContext_t *pCtx,
                        const void *pBuf, size_t bytes)
{
    int n = SSL_write(pCtx->ssl, pBuf, (int)bytes);
    if (n <= 0) {
        int err = SSL_get_error(pCtx->ssl, n);
        if (err == SSL_ERROR_WANT_WRITE)
            return 0;   /* retry */
        fprintf(stderr, "tropic01-aws-pub: SSL_write error %d\n", err);
        return -1;
    }
    return (int32_t)n;
}

static int32_t tls_recv(NetworkContext_t *pCtx,
                        void *pBuf, size_t bytes)
{
    int n = SSL_read(pCtx->ssl, pBuf, (int)bytes);
    if (n <= 0) {
        int err = SSL_get_error(pCtx->ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_ZERO_RETURN)
            return 0;   /* no data yet / clean shutdown */
        fprintf(stderr, "tropic01-aws-pub: SSL_read error %d\n", err);
        return -1;
    }
    return (int32_t)n;
}

/* --------------------------------------------------------------------------
 * coreMQTT event callback — we only publish QoS 0, so no inbound packets
 * are expected.  Log unexpected ones and carry on.
 * ------------------------------------------------------------------------*/

static void mqtt_event_cb(MQTTContext_t *pCtx,
                          MQTTPacketInfo_t *pPktInfo,
                          MQTTDeserializedInfo_t *pDeserInfo)
{
    (void)pCtx;
    (void)pDeserInfo;
    fprintf(stderr, "tropic01-aws-pub: unexpected MQTT packet type 0x%02x\n",
            pPktInfo->type);
}

/* --------------------------------------------------------------------------
 * coreMQTT time function — milliseconds since boot (monotonic)
 * ------------------------------------------------------------------------*/

static uint32_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

/* --------------------------------------------------------------------------
 * OpenSSL helpers
 * ------------------------------------------------------------------------*/

static void ssl_print_errors(const char *ctx)
{
    fprintf(stderr, "tropic01-aws-pub: %s\n", ctx);
    ERR_print_errors_fp(stderr);
}

/*
 * Load EVP_PKEY from a pkcs11: URI using OSSL_STORE.
 * The openssl-aws.cnf (pointed to by OPENSSL_CONF) must have activated the
 * pkcs11-provider before this function is called.
 */
static EVP_PKEY *load_key_from_uri(const char *uri)
{
    OSSL_STORE_CTX *store = OSSL_STORE_open(uri, NULL, NULL, NULL, NULL);
    if (!store) {
        ssl_print_errors("OSSL_STORE_open failed");
        return NULL;
    }

    EVP_PKEY *pkey = NULL;
    while (!OSSL_STORE_eof(store)) {
        OSSL_STORE_INFO *info = OSSL_STORE_load(store);
        if (!info)
            break;
        if (OSSL_STORE_INFO_get_type(info) == OSSL_STORE_INFO_PKEY) {
            pkey = OSSL_STORE_INFO_get1_PKEY(info);
            OSSL_STORE_INFO_free(info);
            break;
        }
        OSSL_STORE_INFO_free(info);
    }
    OSSL_STORE_close(store);

    if (!pkey)
        fprintf(stderr, "tropic01-aws-pub: no private key found at '%s'\n", uri);
    return pkey;
}

/* --------------------------------------------------------------------------
 * TCP connect helper
 * ------------------------------------------------------------------------*/

static int tcp_connect(const char *host, const char *port)
{
    struct addrinfo hints = {0}, *res, *rp;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc) {
        fprintf(stderr, "tropic01-aws-pub: getaddrinfo(%s:%s): %s\n",
                host, port, gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0)
        fprintf(stderr, "tropic01-aws-pub: cannot connect to %s:%s\n",
                host, port);
    return fd;
}

/* --------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------*/

int main(int argc, char *argv[])
{
    if (argc != 9) {
        fprintf(stderr,
            "Usage: %s <endpoint> <port> <ca_file> <cert_pem> <key_uri>"
            " <client_id> <topic> <payload>\n", argv[0]);
        return 1;
    }

    const char *endpoint  = argv[1];
    const char *port_str  = argv[2];
    const char *ca_file   = argv[3];
    const char *cert_pem  = argv[4];
    const char *key_uri   = argv[5];
    const char *client_id = argv[6];
    const char *topic     = argv[7];
    const char *payload   = argv[8];

    int ret = 1;

    /* --- OpenSSL TLS context -------------------------------------------- */
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        ssl_print_errors("SSL_CTX_new");
        return 1;
    }

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    if (!SSL_CTX_load_verify_locations(ctx, ca_file, NULL)) {
        ssl_print_errors("SSL_CTX_load_verify_locations (CA)");
        goto out_ctx;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    if (!SSL_CTX_use_certificate_chain_file(ctx, cert_pem)) {
        ssl_print_errors("SSL_CTX_use_certificate_chain_file");
        goto out_ctx;
    }

    /* Load private key from TROPIC01 ECC slot 1 via pkcs11-provider */
    EVP_PKEY *pkey = load_key_from_uri(key_uri);
    if (!pkey)
        goto out_ctx;
    if (!SSL_CTX_use_PrivateKey(ctx, pkey)) {
        ssl_print_errors("SSL_CTX_use_PrivateKey");
        EVP_PKEY_free(pkey);
        goto out_ctx;
    }
    EVP_PKEY_free(pkey);

    if (!SSL_CTX_check_private_key(ctx)) {
        ssl_print_errors("SSL_CTX_check_private_key");
        goto out_ctx;
    }

    /* --- TCP + TLS handshake -------------------------------------------- */
    int fd = tcp_connect(endpoint, port_str);
    if (fd < 0)
        goto out_ctx;

    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        ssl_print_errors("SSL_new");
        goto out_fd;
    }

    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, endpoint);   /* SNI — required by AWS IoT */

    if (SSL_connect(ssl) != 1) {
        ssl_print_errors("SSL_connect");
        goto out_ssl;
    }

    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        fprintf(stderr, "tropic01-aws-pub: server certificate verification failed\n");
        goto out_ssl;
    }

    /* --- coreMQTT initialisation ---------------------------------------- */
    struct NetworkContext netCtx = { .ssl = ssl };

    TransportInterface_t transport = {
        .pNetworkContext = &netCtx,
        .send            = tls_send,
        .recv            = tls_recv,
        .writev          = NULL,
    };

    /* Network buffer for coreMQTT to read incoming packets into */
    static uint8_t mqtt_buf[1024];
    MQTTFixedBuffer_t fixed_buf = {
        .pBuffer = mqtt_buf,
        .size    = sizeof(mqtt_buf),
    };

    MQTTContext_t mqtt;
    MQTTStatus_t  mrc;

    mrc = MQTT_Init(&mqtt, &transport, get_time_ms, mqtt_event_cb, &fixed_buf);
    if (mrc != MQTTSuccess) {
        fprintf(stderr, "tropic01-aws-pub: MQTT_Init failed (%d)\n", mrc);
        goto out_ssl;
    }

    /* --- MQTT CONNECT ---------------------------------------------------- */
    MQTTConnectInfo_t conn_info = {
        .cleanSession          = true,
        .keepAliveSeconds      = 60,
        .pClientIdentifier     = client_id,
        .clientIdentifierLength = (uint16_t)strlen(client_id),
        .pUserName             = NULL,
        .userNameLength        = 0,
        .pPassword             = NULL,
        .passwordLength        = 0,
    };

    bool session_present = false;
    mrc = MQTT_Connect(&mqtt, &conn_info, NULL, 5000U, &session_present);
    if (mrc != MQTTSuccess) {
        fprintf(stderr, "tropic01-aws-pub: MQTT_Connect failed: %s\n",
                MQTT_Status_strerror(mrc));
        goto out_ssl;
    }

    /* --- MQTT PUBLISH (QoS 0) ------------------------------------------- */
    MQTTPublishInfo_t pub_info = {
        .qos             = MQTTQoS0,
        .retain          = false,
        .dup             = false,
        .pTopicName      = topic,
        .topicNameLength = (uint16_t)strlen(topic),
        .pPayload        = payload,
        .payloadLength   = strlen(payload),
    };

    /* packetId is unused for QoS 0 */
    mrc = MQTT_Publish(&mqtt, &pub_info, 0U);
    if (mrc != MQTTSuccess) {
        fprintf(stderr, "tropic01-aws-pub: MQTT_Publish failed: %s\n",
                MQTT_Status_strerror(mrc));
        goto out_disconnect;
    }

    fprintf(stdout, "tropic01-aws-pub: published to %s\n", topic);
    ret = 0;

out_disconnect:
    MQTT_Disconnect(&mqtt);

out_ssl:
    SSL_shutdown(ssl);
    SSL_free(ssl);
out_fd:
    close(fd);
out_ctx:
    SSL_CTX_free(ctx);
    return ret;
}
