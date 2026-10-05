# OpenVPN with PKCS#11 and TROPIC01 on OpenWrt

This guide walks through building, provisioning, and running an OpenVPN client
that uses a **TROPIC01-ES** secure element (via Mikroe Click board over SPI) as a
hardware key store. The private key is generated inside the chip and **never
leaves it**. The PKCS#11 interface bridges TROPIC01 to OpenVPN.

---

## Architecture

```
OpenVPN (client)
    │  pkcs11-providers  /usr/lib/libtropic_pkcs11.so
    │  pkcs11-id         TropicSquare/TROPIC01-ES/<serial>/<label>/00
    ▼
libtropic_pkcs11.so  (PKCS#11 Cryptoki module)
    │  Linux SPI HAL
    ▼
/dev/spidev2.0  +  /dev/gpiochip0 pin 25  (CS)
    ▼
TROPIC01-ES  (Mikroe Click board)
    ECC slot 0  →  P-256 private key  (hardware-generated, never exported)
    R-MEM slot 1 →  X.509 client cert (DER, written by lt-util)
```

---

## Hardware Setup

### Mikroe Click wiring (MediaTek MT7981B / OpenWrt One)

| TROPIC01 Click pin | SoC signal         | Default in this build   |
|--------------------|--------------------|-------------------------|
| SCK                | SPI clock          | `spidev2.0`             |
| MOSI               | SPI data out       | `spidev2.0`             |
| MISO               | SPI data in        | `spidev2.0`             |
| CS                 | GPIO chip-select   | `/dev/gpiochip0` pin 25 |
| VCC                | 3.3 V              | —                       |
| GND                | GND                | —                       |

The SPI bus must be enabled in the device tree for your board. Verify the device
node exists on the target:

```bash
ls /dev/spidev2.0 /dev/gpiochip0
```

---

## 1. Build

### 1.1 Required packages

This tree ships the following custom packages that must be selected:

| Package | Path | Purpose |
|---|---|---|
| `libtropic` | `features/core/libtropic` | TROPIC01 SDK |
| `libtropic-pkcs11` | `features/core/libtropic-pkcs11` | PKCS#11 provider (SPI-patched) |
| `lt-util` | `features/core/lt-util` | Provisioning CLI |
| `pkcs11-helper` | `features/core/pkcs11-helper` | OpenVPN PKCS#11 dependency |
| `openvpn-openssl` | `features/cra-03-key-storage/openvpn` | OpenVPN with PKCS#11 support |

### 1.2 Selecting the packages

These packages are already selected by
`features/core/openwrt.config` and
`features/cra-03-key-storage/openwrt.config`, which `make` assembles into
`.config` automatically (see root README). To change the selection
interactively instead:

```bash
make menuconfig
# Libraries             → libtropic
# Libraries             → libtropic-pkcs11
# Libraries             → lt-util
# Libraries             → pkcs11-helper
# Network → VPN         → openvpn-openssl
```

### 1.3 SPI configuration

The SPI parameters are compiled into `libtropic_pkcs11.so` via CMake. Defaults
in `features/core/libtropic-pkcs11/Makefile`:

```makefile
CMAKE_OPTIONS += \
    -DTS_SPI_DEV="/dev/spidev2.0" \
    -DTS_GPIO_DEV="/dev/gpiochip0" \
    -DTS_GPIO_CS_PIN=25 \
    -DTS_SPI_SPEED=1000000 \
    -DLT_SH0_KEYS="eng_sample" \
    -DLT_PKCS11_LOG_EN=ON
```

Change `TS_SPI_DEV`, `TS_GPIO_DEV`, or `TS_GPIO_CS_PIN` here if your board uses
a different SPI bus or GPIO pin.

> **`LT_SH0_KEYS=eng_sample`** — required for **ENGINEERING_SAMPLES_01** silicon.
> Production chips need different pairing keys in the `libtropic` package.

### 1.4 Applied patches

Five patches are applied to the upstream `libtropic-pkcs11` source:

| Patch | What it does |
|---|---|
| `001-use-linux-spi-hal.patch` | Replaces USB dongle HAL with the Linux SPI HAL |
| `002-use-spi-device-structure.patch` | Passes SPI/GPIO device paths via CMake defines |
| `003-use-engineering-sample-keys.patch` | Selects ES01 pairing keys |
| `004-add-cko-certificate-from-r-mem.patch` | Reads client X.509 cert from R-MEM slot at PKCS#11 token init |
| `005-add-ecdsa-sign-prehash.patch` | Adds `lt_ecc_ecdsa_sign_prehash()` so OpenVPN's `CKM_ECDSA` gets a pre-hashed input (fixes double-hashing) |

### 1.5 Compile and build image

```bash
cd /path/to/openwrt

# Build individual packages
make package PKG=libtropic
make package PKG=libtropic-pkcs11
make package PKG=lt-util
make package PKG=pkcs11-helper
make package PKG=openvpn

# Or build the full firmware image
make -j$(nproc)
```

Flash the resulting `bin/targets/mediatek/filogic/*-sysupgrade.bin` to the
router.

---

## 2. Provision the TROPIC01 Chip

All provisioning steps run **on the router** over SSH.

### 2.1 Verify SPI connectivity

```bash
lt-util -i
```

`-i` prints the chip identification block (no separate "connectivity" flag
exists); a successful read confirms SPI/GPIO wiring and the pairing keys
both work. Expected output includes lines like:
```
P/N (long)             = 0x0B54524F50494330312D4553FFFFFFFF (TROPIC01-ES)
Batch ID               = 0x0000FFFFFF
```

| Error | Cause | Fix |
|---|---|---|
| `LT_L2_SPI_ERROR` | Bad wiring or wrong SPI device | Check `/dev/spidev2.0`, CS GPIO pin 25 |
| `LT_HOST_SECURE_CHANNEL_SETUP` | Wrong pairing keys | Rebuild with `LT_SH0_KEYS=eng_sample` |
| `GPIO_V2_GET_LINE_IOCTL error! ... Resource busy` | Another process (usually OpenVPN) already holds the SPI/GPIO session — TROPIC01 supports only one active session at a time | Stop the other consumer first (`/etc/init.d/openvpn stop`) |

### 2.2 Generate the hardware key

```bash
lt-util -e -g 0        # Generate P-256 key in ECC slot 0
```

Expected:
```
INFO    lt_ecc_key_generate() : LT_OK
```

The private key is generated inside TROPIC01 and **cannot be exported**.

### 2.3 Export the public key

```bash
lt-util -e -d 0 /tmp/tropic_pub.bin
```

Expected:
```
Curve: P256
Number of elements written: 64
```

This writes the 64-byte raw X‖Y coordinates (no `0x04` prefix). Copy to your
build host for certificate signing:

```bash
scp root@<router-ip>:/tmp/tropic_pub.bin /tmp/tropic_pub.bin
```

---

## 3. Issue the Client Certificate

### 3.1 Build a SubjectPublicKeyInfo PEM from the raw key

```bash
python3 - << 'EOF'
import base64

SPKI_PREFIX = bytes.fromhex(
    "3059301306072a8648ce3d020106082a8648ce3d030107034200"
)
with open("/tmp/tropic_pub.bin", "rb") as f:
    raw = f.read()
assert len(raw) == 64, f"Expected 64 bytes, got {len(raw)}"

der = SPKI_PREFIX + b"\x04" + raw
b64 = base64.b64encode(der).decode()
pem = "-----BEGIN PUBLIC KEY-----\n"
pem += "\n".join(b64[i:i+64] for i in range(0, len(b64), 64))
pem += "\n-----END PUBLIC KEY-----\n"
with open("/tmp/tropic_pub.pem", "w") as f:
    f.write(pem)
print("Written", len(der), "bytes")
EOF
```

### 3.2 Alternative: use the CSR from the device

If the firmware version of `lt-util` supports CSR generation, you can use the
resulting CSR directly:

```bash
# On build host — sign the CSR from the device directly
openssl x509 -req \
    -in /tmp/device.csr \
    -CA ca.crt -CAkey ca.key \
    -CAcreateserial \
    -days 3650 -sha256 \
    -out /tmp/client.crt
```

Or, to override the CN while keeping the hardware public key:

```bash
# Create a dummy CSR with the desired CN
openssl req -new -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes \
    -keyout /tmp/dummy.key \
    -subj "/CN=my-router" \
    -out /tmp/dummy.csr

# Sign with the hardware public key injected via --force_pubkey
openssl x509 -req \
    -in /tmp/dummy.csr \
    -CA ca.crt -CAkey ca.key \
    -CAcreateserial \
    -force_pubkey /tmp/tropic_pub.pem \
    -days 3650 -sha256 \
    -out /tmp/client.crt
```

> Each router must have a **unique CN** — the OpenVPN server assigns VPN IPs
> based on the CN via `ifconfig-pool-persist`. Two clients with the same CN get
> the same IP and will collide.

### 3.3 Verify the certificate

```bash
openssl x509 -in /tmp/client.crt -noout -subject -text \
    | grep -E "Subject:|Public-Key:|NIST"
# Subject: CN = my-router
# Public-Key: (256 bit)
# NIST CURVE: P-256
```

---

## 4. Store the Certificate in TROPIC01

The PKCS#11 module reads the client certificate from **R-MEM slot 1** at
runtime (patch `004`). Write it as DER:

```bash
# Build host: convert PEM → DER
openssl x509 -in /tmp/client.crt -outform DER -out /tmp/client.der
wc -c /tmp/client.der   # typically 290–310 bytes (max 444 bytes for R-MEM)

# Copy DER to router
scp /tmp/client.der root@<router-ip>:/tmp/client.der

# Router: write to R-MEM slot 1
ssh root@<router-ip> 'lt-util -m -s 1 /tmp/client.der'
```

Expected:
```
INFO    lt_r_mem_data_write(): LT_OK
```

> **R-MEM slot 0 is read-protected** on ENGINEERING_SAMPLES_01.
> The convention used here: cert for ECC slot N → R-MEM slot N+1.

---

## 5. Deploy VPN Credentials

```bash
# On the build host — fetch from your VPN CA/server
scp root@<vpn-server>:/etc/openvpn/ca.crt  /tmp/ca.crt
scp root@<vpn-server>:/etc/openvpn/ta.key  /tmp/ta.key

# Push to router
scp /tmp/ca.crt     root@<router-ip>:/etc/openvpn/ca.crt
scp /tmp/ta.key     root@<router-ip>:/etc/openvpn/ta.key
scp /tmp/client.crt root@<router-ip>:/etc/openvpn/client.crt
```

---

## 6. Discover the PKCS#11 Object ID

Each TROPIC01 chip has a unique serial number embedded in the PKCS#11 token
label. Run on the router:

```bash
openvpn --show-pkcs11-ids /usr/lib/libtropic_pkcs11.so 2>&1
```

Example output:
```
Certificate
       DN:             CN=my-router
       Serial:         31973147690D623B4CA1C043DBDA20447622C493
       Serialized id:  TropicSquare/TROPIC01\x2DES/0000FFFFFF5453/TROPIC01\x2DES\x2D5354/00
```

Copy the `Serialized id:` value — you need it in the next step.

---

## 7. OpenVPN Client Configuration

Create `/etc/openvpn/tropic01-client.conf` on the router:

```
client
dev tun
proto udp
remote <vpn-server-ip> 1194
resolv-retry infinite
nobind
persist-key
persist-tun

ca /etc/openvpn/ca.crt

# TROPIC01 PKCS#11 — private key stays in hardware; cert read from R-MEM slot 1
pkcs11-providers /usr/lib/libtropic_pkcs11.so
pkcs11-id 'TropicSquare/TROPIC01\x2DES/<CHIP-SERIAL>/<TOKEN-LABEL>/00'

tls-crypt /etc/openvpn/ta.key 1

cipher AES-256-GCM
data-ciphers AES-256-GCM:AES-128-GCM
auth SHA256
iproute /sbin/ip

verb 3
```

Replace `<CHIP-SERIAL>` and `<TOKEN-LABEL>` with the values from
`--show-pkcs11-ids` above.

---

## 8. Start OpenVPN

### Load the tun module and enable at boot

```bash
modprobe tun
echo "tun" > /etc/modules.d/tun
```

### Configure via UCI (procd-managed, auto-restarts on failure)

```bash
cat > /etc/config/openvpn << 'EOF'
config openvpn "tropic01_client"
    option enabled  1
    option config   "/etc/openvpn/tropic01-client.conf"
EOF

/etc/init.d/openvpn enable
/etc/init.d/openvpn start
```

### Verify the tunnel

```bash
sleep 15
ip addr show tun0
ping -c 3 <vpn-gateway-ip>
```

Expected:
```
6: tun0: <POINTOPOINT,...> mtu 1500
    inet 10.8.0.x/24 scope global tun0
64 bytes from 10.8.0.1: seq=0 ttl=64 time=7 ms
```

---

## 9. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `LT_L2_SPI_ERROR` | Bad SPI wiring or wrong device | Check `/dev/spidev2.0`, CS GPIO=25 |
| `LT_HOST_SECURE_CHANNEL_SETUP` | Wrong pairing keys | Rebuild with `LT_SH0_KEYS=eng_sample` |
| `LT_L3_R_MEM_DATA_WRITE_WRITE_FAIL` on slot 0 | Slot 0 is read-protected on ES01 | Use slot 1+ |
| `OpenSSL: error:bad signature` on server | Double-hashing in ECDSA path | Ensure patch `005-add-ecdsa-sign-prehash.patch` is applied |
| `Cannot open TUN/TAP dev /dev/net/tun` | `tun` module not loaded | `modprobe tun && echo tun > /etc/modules.d/tun` |
| `ip link set failed: could not execute external program` | OpenVPN looking for `/usr/sbin/ip` | Add `iproute /sbin/ip` to client config |
| No cert found in `--show-pkcs11-ids` | Cert not stored or wrong slot | Re-run step 4; verify `lt-util -m -s 1` returned `LT_OK` |
| TLS handshake timeout | Server log shows `bad signature` | Prehash patch missing (patch 005) |
| Two clients get the same VPN IP | Same CN on both certs | Use unique CN per device when signing |

---

## 10. R-MEM / ECC Slot Layout Reference

| Area | Index | Content |
|---|---|---|
| ECC key slots | 0 | P-256 private key (hardware-generated, never exported) |
| ECC key slots | 1–31 | Reserved for additional keys |
| R-MEM | 0 | **READ-PROTECTED** on ENGINEERING_SAMPLES_01 — do not use |
| R-MEM | 1 | DER X.509 client cert for ECC slot 0 (≤ 444 bytes) |
| R-MEM | 2–31 | DER certs for ECC slots 1–30 (if used) |
| R-MEM | 32–511 | General-purpose user data |

---

## 11. File Locations on the Router

| File | Purpose |
|---|---|
| `/usr/bin/lt-util` | TROPIC01 provisioning tool |
| `/usr/lib/libtropic_pkcs11.so` | PKCS#11 provider |
| `/etc/openvpn/ca.crt` | VPN CA certificate |
| `/etc/openvpn/ta.key` | TLS-crypt pre-shared key |
| `/etc/openvpn/client.crt` | Client X.509 certificate (public, for reference) |
| `/etc/openvpn/tropic01-client.conf` | OpenVPN client config |
| `/etc/config/openvpn` | UCI config — enables procd autostart at boot |
