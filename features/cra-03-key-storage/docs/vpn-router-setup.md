# TROPIC01 Hardware-Backed VPN Router Setup Guide

> **Superseded for owners by [openvpn-user-guide.md](openvpn-user-guide.md)**, which covers key generation, a real
> certificate request signed by the chip, the certificate, and installing a `.ovpn` with a tested helper script.
> This older guide is kept as background. Its steps 5-6 sign the TROPIC01 public key with a **dummy request and
> `-force_pubkey`**, so the CA never sees proof that the key is held in the chip; prefer the CSR flow in the user guide.


This guide describes how to provision a new OpenWrt router with a **TROPIC01-ES** secure
element and connect it to the OpenVPN server over a hardware-protected P256 private key.
The private key is generated inside TROPIC01 and **never leaves the chip**.

---

## Architecture Summary

```
Router (TROPIC01-ES)                     VPN Server (<VPN_SERVER_IP>)
┌──────────────────────────────────┐     ┌────────────────────────────┐
│ OpenVPN (client)                 │     │ OpenVPN (server :1194 UDP) │
│   ↓ PKCS#11                      │────►│ CA: CN=ChangeMe (EC P256)  │
│ libtropic_pkcs11.so              │     └────────────────────────────┘
│   ↓ SPI                          │
│ TROPIC01-ES (Mikroe Click board) │
│   ECC slot 0  → P256 private key │
│   R-MEM slot 1→ X.509 cert (DER) │
└──────────────────────────────────┘
```

Each router gets:
- A unique P256 key pair generated in hardware (ECC slot 0)
- A unique X.509 client certificate signed by the VPN CA (stored in R-MEM slot 1)

**Convention:** cert for ECC slot N is stored in R-MEM slot N+1. R-MEM slot 0 is not used:
its behaviour is unclear (backlog 3.14).

---

## Requirements

### Hardware per router
- OpenWrt-compatible board (aarch64 cortex-a53 tested, e.g. MediaTek MT7981B)
- **TROPIC01-ES Mikroe Click** board wired to:
  - SPI bus: `/dev/spidev2.0` at 1 MHz
  - Chip-select GPIO: `/dev/gpiochip0` pin **25**

### Build environment
- This OpenWrt tree with the following custom packages already present:
  - `features/core/libtropic` — libtropic base library
  - `features/core/libtropic-pkcs11` — PKCS#11 provider (with this feed's patches, see `features/core/libtropic-pkcs11/patches/`)
  - `features/core/tropicctl` — command-line tool (also installed as `lt-util`)
  - `features/cra-03-key-storage/openvpn` — OpenVPN with PKCS#11 support

### VPS access
- SSH access to `root@<VPN_SERVER_IP>` (password: `<VPN_SERVER_PASSWORD>`) for signing certs
- Easy-RSA PKI at `/root/easy-rsa/` with a valid EC P256 CA

---

## Step 1 — Build and Flash Firmware

If a firmware image is already built and deployed, skip this step.

```bash
# From the repository root

# Build the relevant packages
make package PKG=libtropic
make package PKG=libtropic-pkcs11
make package PKG=tropicctl
make package PKG=openvpn

# Build a full firmware image for your target
make build
```

Flash the resulting image (`bin/targets/.../<target>-sysupgrade.bin`) to the router.

---

## Step 2 — Verify TROPIC01 Connectivity

After flashing, SSH into the router and confirm SPI communication works:

```bash
ssh root@<router-ip>

# Prints the chip identification block — there is no separate "connectivity"
# flag; a successful read is itself the connectivity check.
tropicctl info chip
```

Expected output includes lines like:
```
P/N (long)             = 0x0B54524F50494330312D4553FFFFFFFF (TROPIC01-ES)
Batch ID               = 0x0000FFFFFF
```

If you see `LT_L2_SPI_ERROR` check wiring. If you see `LT_HOST_SECURE_CHANNEL_SETUP` the
pairing key may be wrong — set `es01` or `prod0` in `/etc/tropic01/slot0-key`; a provisioned
unit needs `/etc/tropic01/pairing.*`.
If you see `GPIO_V2_GET_LINE_IOCTL error! ... Resource busy`, another process
(usually OpenVPN) already holds the SPI/GPIO session — TROPIC01 supports only
one at a time. Stop it first (`/etc/init.d/openvpn stop`).

---

## Step 3 — Generate Key in TROPIC01

Generate a P256 key pair in ECC slot 0. The private key stays inside the chip.

```bash
# On the router:
tropicctl ecc generate 0 --curve p256        # Generate P256 key in ECC slot 0
```

Expected output:
```
ecc slot 0: generated a p256 key; public key <hex>
```

---

## Step 4 — Export the Public Key

Export the 64-byte raw public key (X‖Y coordinates) to a file:

```bash
# On the router:
tropicctl ecc pub 0 --out /tmp/tropic_pub.bin
wc -c /tmp/tropic_pub.bin   # 64
```

`ecc pub` with `--out` prints nothing; check that the file is 64 bytes.

Copy the raw key to the build host for certificate creation:

```bash
# On the build host:
scp root@<router-ip>:/tmp/tropic_pub.bin /tmp/tropic_pub_<router-name>.bin
```

---

## Step 5 — Build an SPKI PEM from the Raw Public Key

The 64-byte file is the raw P256 uncompressed point (no 0x04 prefix). Wrap it in a
standard SubjectPublicKeyInfo DER structure, then convert to PEM:

```bash
# On the build host:
ROUTER="router01"     # change per router
PUBKEY_BIN="/tmp/tropic_pub_${ROUTER}.bin"
SPKI_PEM="/tmp/tropic_pub_${ROUTER}.pem"

python3 - << 'EOF'
import sys, base64, struct

# P256 SPKI DER prefix (AlgorithmIdentifier for id-ecPublicKey + P-256 OID)
SPKI_PREFIX = bytes.fromhex(
    "3059301306072a8648ce3d020106082a8648ce3d030107034200"
)
# 0x04 = uncompressed point prefix
with open("/tmp/tropic_pub_router01.bin", "rb") as f:
    raw = f.read()
assert len(raw) == 64, f"Expected 64 bytes, got {len(raw)}"

der = SPKI_PREFIX + b"\x04" + raw
pem = "-----BEGIN PUBLIC KEY-----\n"
b64 = base64.b64encode(der).decode()
pem += "\n".join(b64[i:i+64] for i in range(0, len(b64), 64))
pem += "\n-----END PUBLIC KEY-----\n"
with open("/tmp/tropic_pub_router01.pem", "w") as f:
    f.write(pem)
print("Written", len(der), "byte SPKI DER as PEM")
EOF

# Verify
openssl pkey -pubin -in "$SPKI_PEM" -noout -text | grep -E "Curve|Public-Key"
```

---

## Step 6 — Issue a Client Certificate on the VPS

Copy the public key PEM to the VPS and sign it with the Easy-RSA CA:

```bash
# Copy public key to VPS
scp "$SPKI_PEM" root@<VPN_SERVER_IP>:/tmp/tropic_pub_${ROUTER}.pem

# SSH to VPS and issue the certificate
ssh root@<VPN_SERVER_IP> << ENDSSH
cd /root/easy-rsa

# Create a dummy CSR (the key in it will be replaced by -force_pubkey)
openssl req -new -newkey ec -pkeyopt ec_paramgen_curve:P-256 \
    -nodes -keyout /tmp/dummy_${ROUTER}.key \
    -subj "/CN=${ROUTER}" -out /tmp/${ROUTER}.csr

# Sign a cert with the TROPIC01 public key and the CSR subject
openssl x509 -req \
    -in /tmp/${ROUTER}.csr \
    -CA pki/ca.crt \
    -CAkey pki/private/ca.key \
    -CAcreateserial \
    -force_pubkey /tmp/tropic_pub_${ROUTER}.pem \
    -days 3650 \
    -sha256 \
    -out /tmp/${ROUTER}.crt

# Verify
openssl x509 -in /tmp/${ROUTER}.crt -noout -text | \
    grep -E "Subject:|Public-Key:|Signature Alg"

# Convert to DER for TROPIC01 storage
openssl x509 -in /tmp/${ROUTER}.crt -outform DER -out /tmp/${ROUTER}.der
wc -c /tmp/${ROUTER}.der   # typically 290-310 bytes
ENDSSH
```

---

## Step 7 — Store the Certificate in TROPIC01 R-MEM

Download the DER certificate and write it to R-MEM slot 1 on the router:

```bash
# Build host: download DER from VPS
scp root@<VPN_SERVER_IP>:/tmp/${ROUTER}.der /tmp/${ROUTER}.der

# Deploy to router
scp /tmp/${ROUTER}.der root@<router-ip>:/tmp/client.der

# Router: write DER to R-MEM slot 1 (ECC slot 0 cert → R-MEM slot 0+1 = 1)
ssh root@<router-ip> 'tropicctl mem write 1 --in /tmp/client.der'
```

Expected output:
```
slot 1: wrote N bytes and read them back
```

> **Note:** R-MEM slot 0 is not used (backlog 3.14).
> Always use slot N+1 for the cert of ECC key slot N.

---

## Step 8 — Deploy VPN Credential Files

Copy the required VPN files to the router:

```bash
# Fetch from VPS
scp root@<VPN_SERVER_IP>:/etc/openvpn/ca.crt  /tmp/ca.crt
scp root@<VPN_SERVER_IP>:/etc/openvpn/ta.key  /tmp/ta.key
scp root@<VPN_SERVER_IP>:/tmp/${ROUTER}.crt    /tmp/client.crt

# Push to router (the cert is only used so OpenVPN knows the DN it offers;
# the hardware key is looked up via pkcs11-id)
scp /tmp/ca.crt   root@<router-ip>:/etc/openvpn/ca.crt
scp /tmp/ta.key   root@<router-ip>:/etc/openvpn/ta.key
scp /tmp/client.crt root@<router-ip>:/etc/openvpn/client.crt
```

---

## Step 9 — Discover the PKCS#11 Object ID

Each TROPIC01 chip has a unique serial number embedded in its token label. Run the
following on the router to discover the exact pkcs11-id string:

```bash
ssh root@<router-ip> \
  'openvpn --show-pkcs11-ids /usr/lib/libtropic_pkcs11.so 2>&1' \
  | grep -A3 'Certificate'
```

Example output:
```
Certificate
       DN:             CN=router01
       Serial:         31973147690D623B4CA1C043DBDA20447622C493
       Serialized id:  TropicSquare/TROPIC01\x2DES/0000FFFFFF5453/TROPIC01\x2DES\x2D5354/00
```

Copy the `Serialized id:` value — you will need it in the next step.

---

## Step 10 — Create the OpenVPN Client Configuration

Create `/etc/openvpn/tropic01-client.conf` on the router, substituting the pkcs11-id
discovered in step 9:

```bash
ssh root@<router-ip> 'cat > /etc/openvpn/tropic01-client.conf << "EOF"
client
dev tun
proto udp
remote <VPN_SERVER_IP> 1194
resolv-retry infinite
nobind
persist-key
persist-tun

ca /etc/openvpn/ca.crt

# TROPIC01 PKCS#11 — private key stays in hardware; cert read from R-MEM slot 1
pkcs11-providers /usr/lib/libtropic_pkcs11.so
pkcs11-id '\''TropicSquare/TROPIC01\x2DES/<CHIP-SERIAL>/<TOKEN-LABEL>/00'\''

tls-crypt /etc/openvpn/ta.key 1

cipher AES-256-GCM
data-ciphers AES-256-GCM:AES-128-GCM
auth SHA256
iproute /sbin/ip

verb 3
EOF'
```

Replace `<CHIP-SERIAL>` and `<TOKEN-LABEL>` with the values from `openvpn --show-pkcs11-ids`.

---

## Step 11 — Start OpenVPN and Enable at Boot

Ensure the `tun` kernel module is loaded now and persisted across reboots:

```bash
ssh root@<router-ip> 'modprobe tun && echo "tun" > /etc/modules.d/tun'
```

Create the UCI configuration that tells OpenWrt's init system to manage OpenVPN:

```bash
ssh root@<router-ip> 'cat > /etc/config/openvpn << "EOF"
config openvpn "tropic01_client"
	option enabled  1
	option config   "/etc/openvpn/tropic01-client.conf"
EOF'
```

Enable the service at boot and start it now (managed by procd — auto-restarts on failure):

```bash
ssh root@<router-ip> '/etc/init.d/openvpn enable && /etc/init.d/openvpn start'
```

`/etc/init.d/openvpn enable` creates two symlinks in `/etc/rc.d/`:
- `S90openvpn` — starts the daemon on boot (priority 90)
- `K10openvpn` — stops it cleanly on shutdown

Verify the tunnel is up:

```bash
ssh root@<router-ip> 'sleep 15 && ip addr show tun0 && ping -c 3 10.8.0.1'
```

Success looks like:
```
6: tun0: <POINTOPOINT,...> mtu 1500
    inet 10.8.0.x/24 scope global tun0
64 bytes from 10.8.0.1: seq=0 ttl=64 time=7 ms
```

To stop or restart the service manually:
```bash
ssh root@<router-ip> '/etc/init.d/openvpn stop'
ssh root@<router-ip> '/etc/init.d/openvpn restart'
```

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `LT_L2_SPI_ERROR` | Bad SPI wiring or wrong device | Check `/dev/spidev2.0`, GPIO CS=25 |
| `LT_HOST_SECURE_CHANNEL_SETUP` | Wrong pairing key | Set `es01` or `prod0` in `/etc/tropic01/slot0-key`; a provisioned unit needs `/etc/tropic01/pairing.*` |
| Anything on R-MEM slot 0 | Slot 0 is not used (backlog 3.14) | Use slot 1+ |
| `OpenSSL: error:bad signature` on server | Double-hashing | Ensure patch `008-port-to-libtropic-4.2.patch` is applied |
| `Cannot open TUN/TAP dev /dev/net/tun: No such file` | `tun` module not loaded | `modprobe tun` and `echo tun > /etc/modules.d/tun` |
| `ip link set failed: could not execute external program` | OpenVPN looking for `/usr/sbin/ip` | Add `iproute /sbin/ip` to client config |
| No cert found in `--show-pkcs11-ids` | Cert not stored / wrong slot | Re-run step 7; `tropicctl mem write 1 --in client.der` reads the slot back and says so |
| TLS handshake timeout (60 s) | Server log shows `bad signature` | Patch 008 missing |

---

## Per-Router Provisioning Summary (Quick Reference)

```bash
ROUTER="routerXX"
ROUTER_IP="<router-ip>"

# 1. Generate hardware key
ssh root@$ROUTER_IP 'tropicctl ecc generate 0 --curve p256'

# 2. Export public key
ssh root@$ROUTER_IP 'tropicctl ecc pub 0 --out /tmp/tropic_pub.bin'
scp root@$ROUTER_IP:/tmp/tropic_pub.bin /tmp/tropic_pub_${ROUTER}.bin

# 3. Build SPKI PEM (run python3 snippet from Step 5, adjust filename)

# 4. Issue cert on VPS
scp /tmp/tropic_pub_${ROUTER}.pem root@<VPN_SERVER_IP>:/tmp/
ssh root@<VPN_SERVER_IP> "cd /root/easy-rsa && \
  openssl req -new -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes \
    -keyout /tmp/dummy.key -subj '/CN=${ROUTER}' -out /tmp/${ROUTER}.csr && \
  openssl x509 -req -in /tmp/${ROUTER}.csr -CA pki/ca.crt \
    -CAkey pki/private/ca.key -CAcreateserial \
    -force_pubkey /tmp/tropic_pub_${ROUTER}.pem \
    -days 3650 -sha256 -out /tmp/${ROUTER}.crt && \
  openssl x509 -in /tmp/${ROUTER}.crt -outform DER -out /tmp/${ROUTER}.der"

# 5. Store cert in TROPIC01
scp root@<VPN_SERVER_IP>:/tmp/${ROUTER}.der /tmp/${ROUTER}.der
scp /tmp/${ROUTER}.der root@$ROUTER_IP:/tmp/client.der
ssh root@$ROUTER_IP 'tropicctl mem write 1 --in /tmp/client.der'

# 6. Deploy VPN files
scp root@<VPN_SERVER_IP>:/etc/openvpn/ca.crt root@$ROUTER_IP:/etc/openvpn/ca.crt
scp root@<VPN_SERVER_IP>:/etc/openvpn/ta.key root@$ROUTER_IP:/etc/openvpn/ta.key
scp root@<VPN_SERVER_IP>:/tmp/${ROUTER}.crt  root@$ROUTER_IP:/etc/openvpn/client.crt

# 7. Discover pkcs11-id and create config (Step 9-10 above)

# 8. Start VPN and enable at boot (procd manages it, auto-restarts on failure)
ssh root@$ROUTER_IP 'modprobe tun; echo tun > /etc/modules.d/tun'
ssh root@$ROUTER_IP 'cat > /etc/config/openvpn << "EOF"
config openvpn "tropic01_client"
	option enabled  1
	option config   "/etc/openvpn/tropic01-client.conf"
EOF'
ssh root@$ROUTER_IP '/etc/init.d/openvpn enable && /etc/init.d/openvpn start'
sleep 12
ssh root@$ROUTER_IP 'ping -c 3 10.8.0.1'
```

---

## Reference: Memory Layout on TROPIC01-ES

| Storage area | Index | Content |
|---|---|---|
| ECC key slots | 0 | P256 private key (hardware-generated, never exported) |
| ECC key slots | 1–31 | Reserved for future use |
| R-MEM (user data) | 0 | not used: behaviour unclear (backlog 3.14) |
| R-MEM (user data) | 1 | DER X.509 client certificate for ECC slot 0 (≤ 444 bytes) |
| R-MEM (user data) | 2–31 | DER certs for ECC slots 1–30 (if used) |
| R-MEM (user data) | 32–511 | General purpose user data |

## Reference: File Locations on the Router

| File | Purpose |
|---|---|
| `/usr/bin/tropicctl` (and the `lt-util` symlink) | TROPIC01 command-line tool |
| `/usr/lib/libtropic_pkcs11.so` | PKCS#11 provider (installed directly, no `.so.spi`) |
| `/etc/openvpn/ca.crt` | VPN CA certificate |
| `/etc/openvpn/ta.key` | TLS-crypt pre-shared key |
| `/etc/openvpn/client.crt` | Client X.509 certificate (public) |
| `/etc/openvpn/tropic01-client.conf` | OpenVPN client config |
| `/etc/config/openvpn` | UCI config — enables boot autostart via procd |
