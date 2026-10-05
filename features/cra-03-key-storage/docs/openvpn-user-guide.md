# OpenVPN with the key in TROPIC01: user guide

How to give an OpenWrt One a VPN identity whose private key is generated inside the TROPIC01
chip and never leaves it, get a certificate for it, and install an OpenVPN profile that uses it.

You need three things, and they may be three different people:

| Role | Does |
|---|---|
| **Owner** | has the router; generates the key, makes the certificate request, installs the profile |
| **VPN operator** | runs the VPN server and its CA; signs the request and hands back the certificate, the CA and the TLS key |
| **PC** | any computer with `ssh` and `openssl` that can reach the router |

Each step below says whether it was **verified on hardware** (an OpenWrt One with a TROPIC01
engineering sample, 2026-10-05). The table at the end summarises it, including a full run on a spare slot.

## 0. What you get, and what you do not

- The client's **private key is generated in the chip and cannot be exported**. A copy of the
  flash, or of the `.ovpn` profile, does not contain it, and cannot be used to impersonate the router.
- The chip signs for **whoever can open a session to it**. Today every unit uses the shared
  engineering-sample pairing key (cra-04: nothing is provisioned), so someone with physical access to
  the board can make the chip sign. Keep the router physically safe until cra-04 provisioning is done.
- The profile's **TLS key** (`tls-crypt`) and CA are not protected by the chip. They are stored on flash,
  mode `0600`, unencrypted.

## 1. Before you start

On the router (the cra-03 image has all of this): `openvpn` with the PKCS#11 module
`/usr/lib/libtropic_pkcs11.so`, `lt-util`, the `tun` module, and the `openssl` command line
(`openssl-util`, with the provider package `pkcs11-provider`). On an older image: `apk add openssl-util`.

**TROPIC01 allows one SPI session at a time.** A running OpenVPN holds it. Before any `lt-util` or
`openssl` step below, stop it: `/etc/init.d/openvpn stop`. The installer script in step 7 does this for you.

**How the slots line up.** A key lives in an **ECC slot N**; its certificate lives in **R-MEM slot N+1**;
OpenSSL and OpenVPN address the pair as **PKCS#11 id `%0N`** (the serialized id ends in `/0N`).

| ECC slot (key) | R-MEM slot (certificate) | PKCS#11 id |
|---|---|---|
| 0 | 1 | `%00` |
| 1 | 2 | `%01` |
| N | N+1 | `%0N` |

R-MEM slot 0 is read-protected on these samples: never use it. Slots hold up to **444 bytes**.

**Pick a free slot.** See what is already in use before you generate anything:

```sh
openvpn --show-pkcs11-ids /usr/lib/libtropic_pkcs11.so 2>&1 | grep -E 'DN:|Serialized id'
```

A key that already exists in a slot cannot be generated again; clearing it destroys that identity for good.
On the development unit ECC slots 0-3 already hold keys. *(Verified.)*

## 2. Check the chip  *(verified)*

```sh
lt-util -i        # prints the chip identification; a successful read is the connectivity check
```

If it fails with a GPIO "resource busy" error, something else holds the SPI session: stop OpenVPN.

## 3. Generate the key in the chip  *(verified, ECC slot 4)*

> **Irreversible for that slot.** The private key is created inside the chip; there is no backup and no
> way to read it. Use a free ECC slot (here `N`).

```sh
lt-util -e -g N                   # generate a P-256 key in ECC slot N
lt-util -e -d N /tmp/pub.bin      # download its PUBLIC key: 64 bytes, X||Y
wc -c < /tmp/pub.bin              # must print 64
```

On hardware the key came out as P-256 (`prime256v1`), and the downloaded public key was the one the later CSR carried.

## 4. Make a certificate request (CSR)  *(verified)*

The request is **signed by the chip**, which proves to the CA that you hold the private key.
(`lt-util -e -s` cannot do this: it signs with EdDSA and fails on a P-256 key with
`LT_L3_ECC_INVALID_KEY`. Use OpenSSL with the PKCS#11 provider, as below.)

Create an isolated OpenSSL config once:

```sh
mkdir -p /etc/ssl/tropic01
cat > /etc/ssl/tropic01/openssl-pkcs11.cnf <<'EOF'
openssl_conf = openssl_init
[openssl_init]
providers = provider_sect
[provider_sect]
default = default_sect
pkcs11 = pkcs11_sect
[default_sect]
activate = 1
[pkcs11_sect]
module = /usr/lib/ossl-modules/pkcs11.so
pkcs11-module-path = /usr/lib/libtropic_pkcs11.so
pkcs11-module-login-behavior = always
pkcs11-module-token-pin = 0000
pkcs11-module-cache-sessions = 1
pkcs11-module-block-operations = rand
activate = 1
EOF
```

Make the request. Use a **unique common name for every device** (the VPN server assigns addresses by CN):

```sh
/etc/init.d/openvpn stop
OPENSSL_CONF=/etc/ssl/tropic01/openssl-pkcs11.cnf \
  openssl req -new -provider default -provider pkcs11 \
  -key 'pkcs11:id=%0N;type=private' \
  -subj '/CN=router01' -out /tmp/router01.csr </dev/null
```

(`%0N` is the id from the table; for slot 0 it is `%00`.) The module is chatty in the system log
(`logread | grep tropic-pkcs11`): that is normal. **Do not run `openssl storeutl` to list objects**: it
stalls after the first object. Use `openvpn --show-pkcs11-ids` instead.

Copy it to your PC and check it:

```sh
ssh root@<router> 'cat /tmp/router01.csr' > router01.csr
openssl req -in router01.csr -verify -noout      # expect: Certificate request self-signature ok
openssl req -in router01.csr -noout -subject
```

On hardware, the signature verified, and the public key in the request equalled the one in the
certificate stored for that slot, for slots 0 and 1.

## 5. The VPN operator signs it  *(verified with `openssl`; untested with easy-rsa)*

The operator receives `router01.csr` and **checks the subject** (and, if they wish, the public key against
step 3's `pub.bin`). With plain OpenSSL and the VPN's CA:

```sh
cat > ext.cnf <<'EOF'
basicConstraints = CA:FALSE
keyUsage = digitalSignature
extendedKeyUsage = clientAuth
EOF
openssl x509 -req -in router01.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -days 3650 -sha256 -extfile ext.cnf -out router01.crt
openssl verify -CAfile ca.crt router01.crt
openssl x509 -in router01.crt -outform DER -out router01.der
wc -c < router01.der          # must be 444 or less: it has to fit one R-MEM slot
```

The test certificate was **413 bytes**. **Keep it small:** a long subject, subject alternative names or extra
extensions can push it past 444 bytes and it will not fit. easy-rsa 3 adds extensions of its own
(`./easyrsa import-req router01.csr router01 && ./easyrsa sign-req client router01`); if you use it, check the
DER size before going further. *(Not tested here.)*

The operator returns **`router01.der`** (or `.crt`), the **CA certificate** (`ca.crt`) and, if the server uses
one, the **TLS key** (`tls-crypt`). Never send the CA's private key anywhere.

## 6. Store the certificate in the chip  *(verified, R-MEM slot 5)*

> Writes R-MEM slot N+1. The slot must be empty; check first.

```sh
lt-util -m -r $((N+1)) /tmp/probe        # expect an error "SLOT_EMPTY" if it is free
ssh root@<router> 'cat > /tmp/router01.der' < router01.der
lt-util -m -s $((N+1)) /tmp/router01.der           # write
lt-util -m -r $((N+1)) /tmp/back.der && cmp /tmp/router01.der /tmp/back.der && echo STORED_OK
openvpn --show-pkcs11-ids /usr/lib/libtropic_pkcs11.so 2>&1 | grep -A2 -E 'DN:.*router01'
```

The last command must show your certificate with a serialized id ending in `/0N`.

## 7. Install the OpenVPN profile  *(verified)*

You get a normal `.ovpn` from the operator, with inline `<ca>` and `<tls-crypt>` blocks. It may also contain
`<cert>` and `<key>` blocks for a software identity: these are **dropped**, because your key is in the chip.

From the repository on your PC:

```sh
features/cra-03-key-storage/host-tools/tropic01-install-ovpn.sh [-i N] [-n NAME] [--no-start] \
    profile.ovpn root@<router>
```

It stops any running OpenVPN on the router (it needs the chip), reads the chip's certificates to find the
PKCS#11 id for key `N` (default 0), writes `/etc/openvpn/NAME.conf`, `NAME-ca.crt` and `NAME-ta.key` with mode
`0600`, creates an enabled OpenVPN instance in `/etc/config/openvpn` so it starts at boot, starts it, and waits
for the tunnel. It never prints anything from the profile. If anything fails, it restarts the OpenVPN that was
running before. Without a certificate for key `N` it stops and tells you to do step 6.

Set `SSH` for options or a wrapper, for example `SSH="ssh -i ~/.ssh/router_key"`.

**By hand**, the same result is: the config below as `/etc/openvpn/tropic01.conf`, the CA and TLS key as files,
and the UCI instance (`uci set openvpn.tropic01=openvpn; uci set openvpn.tropic01.enabled=1; uci set
openvpn.tropic01.config=/etc/openvpn/tropic01.conf; uci commit openvpn; /etc/init.d/openvpn enable`):

```
client
dev tun
proto udp
remote vpn.example.com 1194
nobind
persist-key
persist-tun
remote-cert-tls server
cipher AES-256-GCM
auth SHA256
ca /etc/openvpn/tropic01-ca.crt
tls-crypt /etc/openvpn/tropic01-ta.key
pkcs11-providers /usr/lib/libtropic_pkcs11.so
pkcs11-id 'TropicSquare/TROPIC01\x2DES/<chip-serial>/<token-label>/0N'
iproute /sbin/ip
keepalive 10 120
```

The `pkcs11-id` string is the "Serialized id" printed by `openvpn --show-pkcs11-ids`. Do not put a `cert` or
`key` line in this file.

## 8. Check it  *(verified)*

```sh
ip -4 addr show tun0                       # the tunnel address
ping -c3 <vpn-gateway-inside-the-tunnel>   # for example 10.8.0.1
logread | grep -E 'openvpn|tropic-pkcs11'
reboot                                     # then: the tunnel should return by itself within about a minute
```

On hardware: `tun0` came up in about 5 s, and after a reboot the tunnel came up by itself at 48 s of uptime.
The server may push a default route, so all of the router's traffic can start going through the VPN.

## 9. Remove it

```sh
/etc/init.d/openvpn stop; /etc/init.d/openvpn disable
uci -q delete openvpn.tropic01; uci commit openvpn
rm -f /etc/openvpn/tropic01.conf /etc/openvpn/tropic01-ca.crt /etc/openvpn/tropic01-ta.key
```

Ask the VPN operator to **revoke the certificate**. The key stays in the chip; clearing an ECC slot
(`lt-util -e -c N`) and erasing its certificate (`lt-util -m -e $((N+1))`) are irreversible. Both were run on a test
slot pair (4 and 5): afterwards the slot read as empty and every other slot was unchanged.

## 10. Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `GPIO_V2_GET_LINE_IOCTL ... Resource busy`, `C_Initialize` fails | another process holds the chip's single SPI session: `/etc/init.d/openvpn stop`, stop `tropic01-aws`, retry |
| the installer says "no certificate for key index N" | step 6 is missing: the certificate is not in R-MEM slot N+1 |
| `lt-util -e -s` fails with `LT_L3_ECC_INVALID_KEY` | it signs with EdDSA only; use `openssl req` (step 4) for P-256 keys |
| `openssl req` finds no key | wrong id, or the slot has no key; list with `openvpn --show-pkcs11-ids` |
| `openssl storeutl pkcs11:` hangs | known; do not use it |
| certificate does not fit R-MEM | over 444 bytes: shorter subject, fewer extensions (step 5) |
| TLS handshake fails, "certificate is not yet valid" | the router's clock is wrong until NTP succeeds; check `date` |
| tunnel never comes up at boot but starts by hand | the chip needs a few seconds after boot; the init script retries every 30 s |
| authentication errors | CA, certificate and TLS key must all come from the same VPN; ask the operator |

## 11. What was verified on hardware

| Step | Status |
|---|---|
| 2 chip check | verified |
| 3 generate a key in a free slot (ECC slot 4): P-256, 64-byte public key | verified |
| 4 CSR signed by the chip (slots 0, 1 and 4): self-signature valid, public key equals the chip's | verified (359-byte CSR) |
| 5 signing and the 444-byte size check with `openssl` | verified (413 and 419 bytes); easy-rsa not tested |
| 6 store the certificate in R-MEM (slot 5), read back identical, exposed by the PKCS#11 module with id `/04` | verified |
| a signature made by the chip's key verifies with the stored certificate's public key | verified |
| 7 installer, derived config, boot start, failure paths | verified |
| 8 tunnel up, ping through it, up again after a reboot | verified |
| 9 clearing the test slots (`-e -c 4`, `-m -e 5`) | verified; slots 0-3, R-MEM 1-2 and the vault slot untouched |

Steps 3-6 were run once on slots 4 and 5 with a throwaway CA (the resulting certificate was not used to connect to
a real VPN, because that VPN's CA did not sign it); the connection test in steps 7-8 used the existing identity in
slot 0 with the VPN operator's certificate.

Never commit a filled-in profile: `*.ovpn` is git-ignored, and the repository keeps only `.example` templates.
