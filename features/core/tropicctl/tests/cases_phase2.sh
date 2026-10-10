# Test cases for phases 2 to 4: ECC, pairing writes, configuration rewrites, MAC-and-Destroy, counters, PIN-protected
# sessions and the rest of legacy mode. Sourced by run.sh, which provides T, rc, lrc, L, fresh, setkey, json_ok.

rawpub() { # rawpub <DER private key file>: the raw public key bytes (Ed25519 32, X25519 32)
	openssl pkey -inform DER -in "$1" -pubout -outform DER 2>/dev/null | tail -c 32
}
cfgword() { # cfgword <NAME> [file]: the word of a config object in a dump
	grep -E "^R TR01_CFG_$1 " "${2:-out.txt}" | awk '{print $3}'
}
printf 'message to sign' > msg.bin

# ------------------------------------------------------------------ ECC (signatures are verified by OpenSSL itself)
fresh
check "ecc list on a fresh chip" 0 "$(rc ecc list)"
check "32 slots, all empty" 32 "$(grep -c ': empty' out.txt)"
check "ecc generate p256" 0 "$(rc ecc generate 3 --curve p256)"
check_contains "reports the key" "generated a p256 key" "$(cat out.txt)"
check "ecc list shows it" "ecc slot 3: p256 generated" "$(T ecc list 2>/dev/null | grep 'slot 3:')"
check "ecc pub raw is 64 bytes" 64 "$(T ecc pub 3 --out p3.raw 2>/dev/null; wc -c < p3.raw | tr -d ' ')"
T ecc pub 3 --format pem --out p3.pem 2>/dev/null
check_contains "ecc pub --format pem" "BEGIN PUBLIC KEY" "$(cat p3.pem)"
check "ecc sign p256 (DER)" 0 "$(rc ecc sign 3 --in msg.bin --der --out s3.der)"
check "OpenSSL verifies the chip's ECDSA signature over SHA-256(message)" "Verified OK" "$(openssl dgst -sha256 -verify p3.pem -signature s3.der msg.bin 2>&1)"
check "the signature does not verify for another message" yes "$(printf 'other' > other.bin; openssl dgst -sha256 -verify p3.pem -signature s3.der other.bin >/dev/null 2>&1 && echo no || echo yes)"
check "ecc sign p256 raw is 64 bytes" 64 "$(T ecc sign 3 --in msg.bin --out s3.raw 2>/dev/null; wc -c < s3.raw | tr -d ' ')"
openssl dgst -sha256 -binary msg.bin > digest.bin
check "--digest signs a 32-byte hash as given" 0 "$(rc ecc sign 3 --in digest.bin --digest --der --out sd.der)"
check "OpenSSL verifies the digest signature" "Signature Verified Successfully" "$(openssl pkeyutl -verify -pubin -inkey p3.pem -in digest.bin -sigfile sd.der 2>&1)"
check "--digest with a message that is not 32 bytes is refused" 2 "$(rc ecc sign 3 --in msg.bin --digest)"
check "ecc sign prints hex without --out" 128 "$(T ecc sign 3 --in msg.bin 2>/dev/null | tr -d '\n' | wc -c | tr -d ' ')"
check "ecc generate ed25519" 0 "$(rc ecc generate 4 --curve ed25519)"
T ecc pub 4 --format pem --out p4.pem 2>/dev/null
check "ecc pub ed25519 raw is 32 bytes" 32 "$(T ecc pub 4 --out p4.raw 2>/dev/null; wc -c < p4.raw | tr -d ' ')"
check "ecc sign ed25519" 0 "$(rc ecc sign 4 --in msg.bin --out s4.bin)"
check "OpenSSL verifies the chip's EdDSA signature" "Signature Verified Successfully" "$(openssl pkeyutl -verify -pubin -inkey p4.pem -rawin -in msg.bin -sigfile s4.bin 2>&1)"
check "ECDSA on an Ed25519 slot is a usage error, not a wrong answer" 2 "$(rc ecc sign 4 --in msg.bin --alg ecdsa)"
check "EdDSA on a P-256 slot is a usage error" 2 "$(rc ecc sign 3 --in msg.bin --alg eddsa)"
check "--der with EdDSA is refused" 2 "$(rc ecc sign 4 --in msg.bin --der)"
head -c 4096 /dev/zero > big4096.bin
check "an EdDSA message over 4095 bytes is refused" 2 "$(rc ecc sign 4 --in big4096.bin)"
check "ecc pub of an empty slot: not found" 6 "$(rc ecc pub 9)"
check "ecc sign with an empty slot: not found" 6 "$(rc ecc sign 9 --in msg.bin)"
check "ecc slot 32 refused" 2 "$(rc ecc pub 32)"
check "ecc generate without --curve refused" 2 "$(rc ecc generate 9)"
check "ecc generate with a bad curve refused" 2 "$(rc ecc generate 9 --curve rsa)"
check "generating into an occupied slot: exit 7" 7 "$(rc ecc generate 3 --curve p256)"
check "--replace without the confirmation flag: exit 9" 9 "$(rc ecc generate 3 --curve p256 --replace)"
check "the key survived the refusal" "$(sha256sum < p3.raw)" "$(T ecc pub 3 --out p3b.raw 2>/dev/null; sha256sum < p3b.raw)"
check "ecc erase without its flag: exit 9" 9 "$(rc ecc erase 3)"
check "--dry-run generate writes nothing" 0 "$(rc --dry-run ecc generate 10 --curve p256)"
check "slot 10 is still empty after the dry run" 6 "$(rc ecc pub 10)"
check "--replace with the flag erases and regenerates" 0 "$(rc ecc generate 3 --curve p256 --replace --yes-erase-ecc-key)"
check "the new key is a different key" yes "$(T ecc pub 3 --out p3c.raw 2>/dev/null; cmp -s p3.raw p3c.raw && echo no || echo yes)"
check "ecc erase with its flag" 0 "$(rc ecc erase 3 --yes-erase-ecc-key)"
check "the slot reads empty after the erase" 6 "$(rc ecc pub 3)"
check "erasing an empty slot is not an error" 0 "$(rc ecc erase 3 --yes-erase-ecc-key)"
openssl genpkey -algorithm ed25519 -outform DER -out ed.der 2>/dev/null; tail -c 32 ed.der > ed.priv
check "ecc store ed25519 from a file" 0 "$(rc ecc store 5 --curve ed25519 --in ed.priv)"
check "the stored key's origin is 'stored'" "ecc slot 5: ed25519 stored" "$(T ecc list 2>/dev/null | grep 'slot 5:')"
check "the chip's public key is the one OpenSSL derives from that private key" yes "$(rawpub ed.der > ed.pub; T ecc pub 5 --out ed.chip 2>/dev/null; cmp -s ed.pub ed.chip && echo yes || echo no)"
head -c 31 /dev/urandom > short.priv
check "a private key file of the wrong size is refused" 2 "$(rc ecc store 6 --curve ed25519 --in short.priv)"
check "storing into an occupied slot: exit 7" 7 "$(rc ecc store 5 --curve ed25519 --in ed.priv)"
check "--dry-run store" 0 "$(rc --dry-run ecc store 7 --curve ed25519 --in ed.priv)"
check "slot 7 still empty" 6 "$(rc ecc pub 7)"
if [ "$HAVE_PY" = 1 ]; then rc --json ecc list >/dev/null; check "ecc list --json is valid" valid "$(json_ok out.txt)"; rc --json ecc sign 4 --in msg.bin >/dev/null; check "ecc sign --json is valid" valid "$(json_ok out.txt)"; fi
PRIV4="$(tail -c 32 chip/ecc/4.key | od -An -tx1 -v | tr -d ' \n')"
{ T ecc generate 11 --curve ed25519; T ecc sign 11 --in msg.bin; T ecc pub 11; } >eccout.txt 2>&1
check_absent "the fake's private key never appears in the tool's output" "$(od -An -tx1 -v chip/ecc/11.key | tr -d ' \n')" "$(od -An -tx1 -v eccout.txt | tr -d ' \n')"
# access rights per group of targets (the layout is libtropic's; the fake's use of it is [assumed] until a spare-chip run)
fresh; T ecc generate 3 --curve p256 >/dev/null 2>&1; T ecc generate 9 --curve p256 >/dev/null 2>&1
sed -i 's/^TR01_CFG_UAP_ECDSA_SIGN .*/TR01_CFG_UAP_ECDSA_SIGN 0xfffffffe/' chip/rconfig
check "[assumed layout] the shared key is denied signing with ECC slots 0-7" 5 "$(rc ecc sign 3 --in msg.bin)"
check "[assumed layout] but not with ECC slots 8-15" 0 "$(rc ecc sign 9 --in msg.bin)"

# ------------------------------------------------------------------ pairing writes and invalidation
fresh
head -c 32 /dev/urandom > newpub.bin
check "pairing write without its flag: refused before the chip is touched" 9 "$(rc pairing write 1 --in newpub.bin)"
check "and nothing was written" 6 "$(rc pairing read 1)"
check "pairing write of slot 0 is refused" 9 "$(rc pairing write 0 --in newpub.bin --yes-write-pairing-slot)"
check_contains "naming the shared key" "shared engineering key" "$(cat err.txt)"
check "pairing write of slot 7" 2 "$(rc pairing write 7 --in newpub.bin --yes-write-pairing-slot)"
check "a public key of the wrong size" 2 "$(head -c 31 newpub.bin > np31.bin; rc pairing write 1 --in np31.bin --yes-write-pairing-slot)"
check "--dry-run runs the checks and writes nothing" 0 "$(rc --dry-run pairing write 1 --in newpub.bin)"
check_contains "and says it would write" "would write the public key into pairing slot 1" "$(cat err.txt)"
check "slot 1 still empty after the dry run" 6 "$(rc pairing read 1)"
check "pairing write" 0 "$(rc pairing write 1 --in newpub.bin --yes-write-pairing-slot)"
check_contains "read back" "written and read back correctly" "$(cat out.txt)"
check "the slot holds the key" yes "$(T pairing read 1 --out got.pub 2>/dev/null; cmp -s newpub.bin got.pub && echo yes || echo no)"
check "a second write to the same slot: exit 7" 7 "$(rc pairing write 1 --in newpub.bin --yes-write-pairing-slot)"
fresh
check "a write that stores the wrong key is caught: exit 8" 8 "$(TROPICCTL_FAKE_PAIRING_CORRUPT=1 rc pairing write 1 --in newpub.bin --yes-write-pairing-slot)"
check_contains "with the warning" "DO NOT CONTINUE" "$(cat err.txt)"
fresh
check "a silent write failure is caught: exit 8" 8 "$(TROPICCTL_FAKE_PAIRING_SILENT=1 rc pairing write 1 --in newpub.bin --yes-write-pairing-slot)"
fresh
check "a chip error while checking the slot is not taken for 'empty'" 10 "$(sed -i 's/^TR01_CFG_UAP_PAIRING_KEY_READ .*/TR01_CFG_UAP_PAIRING_KEY_READ 0xffffffff/' chip/rconfig; echo 10)"
# invalidation
fresh; setkey 1; T pairing write 2 --in newpub.bin --yes-write-pairing-slot >/dev/null 2>&1
check "invalidate without its flag: refused" 9 "$(rc pairing invalidate 2)"
check "invalidate slot 2 with the flag (shared session)" 0 "$(rc pairing invalidate 2 --yes-invalidate-pairing-slot)"
check "the slot reads invalidated" "pairing slot 2: invalidated" "$(T pairing list 2>/dev/null | grep 'slot 2')"
check "an invalidated slot cannot be written again: exit 7" 7 "$(rc pairing write 2 --in newpub.bin --yes-write-pairing-slot)"
check "invalidating an empty slot: not found" 6 "$(rc pairing invalidate 3 --yes-invalidate-pairing-slot)"
check "invalidating slot 0 needs its own extra flag" 9 "$(rc --key unit pairing invalidate 0 --yes-invalidate-pairing-slot)"
check "slot 0 from a session opened with the shared key itself: refused" 9 "$(rc --key shared pairing invalidate 0 --yes-invalidate-pairing-slot --yes-invalidate-shared-key)"
check_contains "because the key in use would be cut off" "cut off the key in use" "$(cat err.txt)"
check "a session may not invalidate its own key" 9 "$(rc --key unit pairing invalidate 1 --yes-invalidate-pairing-slot)"
check "--dry-run invalidate slot 0 from the unit key" 0 "$(rc --dry-run --key unit pairing invalidate 0 --yes-invalidate-pairing-slot --yes-invalidate-shared-key)"
check "slot 0 is still usable after the dry run" 0 "$(rc session test --key shared)"
check "slot 0 invalidated from the working unit key" 0 "$(rc --key unit pairing invalidate 0 --yes-invalidate-pairing-slot --yes-invalidate-shared-key)"
check "the shared key can no longer open a session" 4 "$(rc --key shared session test)"
check "the unit key still can" 0 "$(rc --key unit session test)"

# ------------------------------------------------------------------ configuration
fresh
check "config dump --decode" 0 "$(rc config dump --decode)"
check_contains "decodes a per-slot-group object" "ECC slots 8-15: pairing keys 0,1,2,3" "$(cat out.txt)"
check_contains "decodes a per-pairing-slot object" "on pairing slot 2: pairing keys 0,1,2,3" "$(cat out.txt)"
check "--decode is for the text output" 2 "$(rc --json config dump --decode)"
check "config get with the prefix left out" "R TR01_CFG_UAP_PING 0xffffffff
I TR01_CFG_UAP_PING 0xffffffff" "$(T config get UAP_PING 2>/dev/null)"
check "config get of an unknown object: not found" 6 "$(rc config get NOPE)"
check "restrict-shared before the unit key is in the chip: refused" 9 "$(rc config restrict-shared --unit-slot 1 --out r.cfg)"
setkey 1
check "restrict-shared builds a file" 0 "$(rc --key unit config restrict-shared --unit-slot 1 --out r.cfg)"
check "27 objects" 27 "$(grep -c '^R ' r.cfg)"
check "a per-group object loses key 0 in every group" "0xfefefefe" "$(cfgword UAP_ECDSA_SIGN r.cfg)"
check "a per-pairing-slot object loses key 0 in every group" "0xfefefefe" "$(cfgword UAP_PAIRING_KEY_READ r.cfg)"
check "a single-target object loses key 0 in byte 0 only" "0xfffffffe" "$(cfgword UAP_R_MEM_DATA_READ r.cfg)"
check "MAC-and-Destroy and counters too" "0xfefefefe 0xfefefefe" "$(cfgword UAP_MAC_AND_DESTROY r.cfg) $(cfgword UAP_MCOUNTER_UPDATE r.cfg)"
check "PING keeps key 0" "0xffffffff" "$(cfgword UAP_PING r.cfg)"
check "objects that are not access rights are untouched" "0xffffffef" "$(cfgword START_UP r.cfg)"
check "restrict-shared changes nothing on the chip" "0xffffffff" "$(T config dump 2>/dev/null > cur.txt; cfgword UAP_R_MEM_DATA_READ cur.txt)"
check "diff shows who loses access" 0 "$(rc --key unit config diff --in r.cfg)"
check_contains "naming pairing key 0" "pairing key 0 loses access" "$(cat out.txt)"
check "diff --check exits 8 when they differ" 8 "$(rc --key unit config diff --in r.cfg --check)"
check "rewrite without its flag: refused" 9 "$(rc --key unit config rewrite --in r.cfg)"
check "--dry-run rewrite changes nothing" 0 "$(rc --dry-run --key unit config rewrite --in r.cfg)"
check_contains "and says what it would do" "would save a backup, erase the R-config" "$(cat err.txt)"
check "the shared key still reads R-MEM after the dry run" 6 "$(rc --key shared mem read 1)"
check "rewrite" 0 "$(rc --key unit config rewrite --in r.cfg --yes-rewrite-r-config --backup bk.cfg)"
check_contains "read back" "rewritten and read back correctly" "$(cat out.txt)"
check "the backup was saved first" 27 "$(grep -c '^R ' bk.cfg)"
check "the shared key is now denied R-MEM reads" 5 "$(rc --key shared mem read 1)"
check "the shared key can still open a session (PING stays)" 0 "$(rc --key shared session test)"
check "the unit key still works" 6 "$(rc --key unit mem read 1)"
check "the config on the chip equals the file" 0 "$(rc --key unit config diff --in r.cfg --check)"
check "a second rewrite of the same file is a no-op" 0 "$(rc --key unit config rewrite --in r.cfg --yes-rewrite-r-config)"
check_contains "and says so" "nothing to do" "$(cat err.txt)"
check "restore with the unit key" 0 "$(rc --key unit config restore --in bk.cfg --yes-rewrite-r-config --backup bk2.cfg)"
check "the shared key has its rights again" 6 "$(rc --key shared mem read 1)"
# guards
fresh; setkey 1
T --key unit config restrict-shared --unit-slot 1 --out r.cfg >/dev/null 2>&1
sed 's/^R TR01_CFG_UAP_R_CONFIG_WRITE_ERASE .*/R TR01_CFG_UAP_R_CONFIG_WRITE_ERASE 0xfffffffd/' r.cfg > lock.cfg   # removes key 1 from rewriting the config
check "a file that would lock the session's own key out of the config: refused" 9 "$(rc --key unit config rewrite --in lock.cfg --yes-rewrite-r-config)"
check_contains "with the reason" "could never be undone" "$(cat err.txt)"
sed 's/^R TR01_CFG_START_UP .*/R TR01_CFG_START_UP 0xffffffff/' r.cfg > su.cfg
check "a change to a non-access object needs --include-non-access: refused" 9 "$(rc --key unit config rewrite --in su.cfg --yes-rewrite-r-config)"
check "and is allowed with it" 0 "$(rc --key unit config rewrite --in su.cfg --yes-rewrite-r-config --include-non-access --backup bk3.cfg)"
grep -v 'UAP_PING' r.cfg > miss.cfg
check "a file that does not list every object: usage error" 2 "$(rc --key unit config rewrite --in miss.cfg --yes-rewrite-r-config)"
{ cat r.cfg; echo "R TR01_CFG_UAP_PING 0xffffffff"; } > dup.cfg
check "a file with an object twice: usage error" 2 "$(rc --key unit config rewrite --in dup.cfg --yes-rewrite-r-config)"
sed 's/^R TR01_CFG_UAP_PING .*/R TR01_CFG_UAP_PINGX 0xffffffff/' r.cfg > unk.cfg
check "a file with an unknown object: usage error" 2 "$(rc --key unit config rewrite --in unk.cfg --yes-rewrite-r-config)"
fresh; setkey 1
T --key unit config restrict-shared --unit-slot 1 --out r.cfg >/dev/null 2>&1
check "a write that fails after the erase is reported: exit 10" 10 "$(TROPICCTL_FAKE_CONFIG_FAIL_AFTER_ERASE=1 rc --key unit config rewrite --in r.cfg --yes-rewrite-r-config --backup bkf.cfg)"
check_contains "saying the R-config was ERASED" "ERASED" "$(cat err.txt)"
check_contains "and how to restore" "config restore --in bkf.cfg" "$(cat err.txt)"
check "the backup exists" 27 "$(grep -c '^R ' bkf.cfg)"
fresh; setkey 1
T --key unit config restrict-shared --unit-slot 1 --out r.cfg >/dev/null 2>&1
check "a config that does not read back as written: exit 8" 8 "$(TROPICCTL_FAKE_CONFIG_PARTIAL=1 rc --key unit config rewrite --in r.cfg --yes-rewrite-r-config --backup bkp.cfg)"
check_contains "with a restore hint" "config restore --in bkp.cfg" "$(cat err.txt)"
fresh; setkey 1
check "without a writable backup location the config is not touched: exit 9" 9 "$(rc --key unit config rewrite --in r.cfg --yes-rewrite-r-config --backup /nonexistent-dir/bk.cfg)"
check "restrict-shared refuses a unit key that cannot rewrite the config" 9 "$(sed -i 's/^TR01_CFG_UAP_R_CONFIG_WRITE_ERASE .*/TR01_CFG_UAP_R_CONFIG_WRITE_ERASE 0xfffffffd/' chip/rconfig; rc --key unit config restrict-shared --unit-slot 1 --out r2.cfg)"
# I-config bits
fresh
check "burn without its flag: refused" 9 "$(rc config burn UAP_PING 3)"
check "--dry-run checks the firmware too: the bench chip's 0.1.2 is refused" 9 "$(rc --dry-run config burn UAP_PING 3)"
check "the bench chip's firmware (0.1.2) is older than 2.0.0: refused" 9 "$(rc config burn UAP_PING 3 --yes-burn-i-config-bit)"
check_contains "naming the version" "firmware is 0.1.2" "$(cat err.txt)"
check "nothing was burned" "I TR01_CFG_UAP_PING 0xffffffff" "$(T config get UAP_PING 2>/dev/null | grep '^I')"
echo "00000002 00000100" > chip/fw   # 2.0.0
check "--dry-run burn with firmware 2.0.0" 0 "$(rc --dry-run config burn UAP_PING 3)"
check "the dry run burned nothing" "I TR01_CFG_UAP_PING 0xffffffff" "$(T config get UAP_PING 2>/dev/null | grep '^I')"
check "burn a bit (firmware 2.0.0)" 0 "$(rc config burn UAP_PING 3 --yes-burn-i-config-bit)"
check "the bit is cleared (and only that bit)" "I TR01_CFG_UAP_PING 0xfffffff7" "$(T config get UAP_PING 2>/dev/null | grep '^I')"
check "burning a cleared bit is a no-op" 0 "$(rc config burn UAP_PING 3 --yes-burn-i-config-bit)"
check "bit 40" 2 "$(rc config burn UAP_PING 40 --yes-burn-i-config-bit)"
check "an unknown object: not found" 6 "$(rc config burn NOPE 3 --yes-burn-i-config-bit)"
check "a silent write failure is caught: exit 8" 8 "$(TROPICCTL_FAKE_ICONFIG_SILENT=1 rc config burn UAP_PING 5 --yes-burn-i-config-bit)"
echo "00000102 00000100" > chip/fw   # 2.1.0 is fine; 1.2.0 below is not
check "firmware 2.1.0 is accepted" 0 "$(rc config burn UAP_PING 6 --yes-burn-i-config-bit)"
echo "00000201 00000100" > chip/fw   # 1.2.0
check "firmware 1.2.0 is refused" 9 "$(rc config burn UAP_PING 7 --yes-burn-i-config-bit)"

# ------------------------------------------------------------------ MAC-and-Destroy (PIN scheme of the application note)
fresh
echo 1234 > pin.txt
check "mnd setup without its flag: refused" 9 "$(rc mnd setup --state st.bin --out k.bin --pin-file pin.txt)"
check "--dry-run setup" 0 "$(rc --dry-run mnd setup --state st.bin --out k.bin --pin-file pin.txt)"
check "the dry run created no state" no "$([ -e st.bin ] && echo yes || echo no)"
echo 12 > pin2.txt
check "a PIN of 2 characters: usage error" 2 "$(rc mnd setup --state st.bin --out k.bin --pin-file pin2.txt --yes-setup-mnd)"
echo 123456789 > pin9.txt
check "a PIN of 9 characters: usage error" 2 "$(rc mnd setup --state st.bin --out k.bin --pin-file pin9.txt --yes-setup-mnd)"
check "mnd setup" 0 "$(rc mnd setup --state st.bin --out k.bin --pin-file pin.txt --yes-setup-mnd --slots 5)"
check "the key file holds 32 bytes" 32 "$(wc -c < k.bin | tr -d ' ')"
check "five chip slots were initialised" 5 "$(ls chip/mnd | wc -l | tr -d ' ')"
check "mnd status" "PIN state st.bin: 5 of 5 attempt(s) left (MAC-and-Destroy slots 0-4; the chip's own count is authoritative)" "$(T mnd status --state st.bin 2>/dev/null)"
check "setup refuses to overwrite a PIN state" 7 "$(rc mnd setup --state st.bin --out k2.bin --pin-file pin.txt --yes-setup-mnd)"
check "the right PIN releases the same key" 0 "$(rc mnd check --state st.bin --out k_ok.bin --pin-file pin.txt)"
check "the released key equals the setup key" yes "$(cmp -s k.bin k_ok.bin && echo yes || echo no)"
echo 4321 > wrong.txt
check "a wrong PIN: exit 8" 8 "$(rc mnd check --state st.bin --out k_bad.bin --pin-file wrong.txt)"
check_contains "naming the attempts left" "4 attempt(s) left" "$(cat err.txt)"
check "no key is written for a wrong PIN" no "$([ -e k_bad.bin ] && echo yes || echo no)"
check "the attempt count was used up" "PIN state st.bin: 4 of 5" "$(T mnd status --state st.bin 2>/dev/null | cut -c1-24)"
check "the right PIN still works and gives the same key" yes "$(rc mnd check --state st.bin --out k_ok2.bin --pin-file pin.txt >/dev/null; cmp -s k.bin k_ok2.bin && echo yes || echo no)"
check "a correct PIN restores the attempts" "PIN state st.bin: 5 of 5" "$(T mnd status --state st.bin 2>/dev/null | cut -c1-24)"
check "the PIN can come from standard input" 0 "$(echo 1234 | T --pin-stdin mnd check --state st.bin --out k_in.bin >out.txt 2>err.txt; echo $?)"
echo "extra" > add.bin
check "additional data changes the key: the wrong data counts as a wrong PIN" 8 "$(rc mnd check --state st.bin --out k_add.bin --pin-file pin.txt --add-file add.bin)"
fresh
check "setup with 3 attempts" 0 "$(rc mnd setup --state st3.bin --out k3.bin --pin-file pin.txt --yes-setup-mnd --slots 3 --base-slot 10)"
check "it used chip slots 10-12" "10 11 12" "$(ls chip/mnd | sort -n | tr '\n' ' ' | sed 's/ $//')"
for i in 1 2 3; do rc mnd check --state st3.bin --out kx.bin --pin-file wrong.txt >/dev/null; done
check "after three wrong PINs no attempts remain" "PIN state st3.bin: 0 of 3" "$(T mnd status --state st3.bin 2>/dev/null | cut -c1-25)"
check "then even the right PIN is refused: exit 9" 9 "$(rc mnd check --state st3.bin --out kx.bin --pin-file pin.txt)"
check "and no key is released" no "$([ -e kx.bin ] && echo yes || echo no)"
fresh; rm -f st.bin; rc mnd setup --state st.bin --out k.bin --pin-file pin.txt --yes-setup-mnd --slots 4 >/dev/null
check "a chip error during the check is reported" 10 "$(TROPICCTL_FAKE_MND_FAIL=1 rc mnd check --state st.bin --out kk.bin --pin-file pin.txt)"
check "the attempt was recorded BEFORE the chip was touched" "PIN state st.bin: 3 of 4" "$(T mnd status --state st.bin 2>/dev/null | cut -c1-24)"
printf 'secret key to protect, 32 bytes!' > protkey.bin
check "mnd seal" 0 "$(rc mnd seal --key-file k.bin --in protkey.bin --out sealed.bin)"
check "sealed data is 28 bytes longer" 60 "$(wc -c < sealed.bin | tr -d ' ')"
check "mnd unseal" 0 "$(rc mnd unseal --key-file k.bin --in sealed.bin --out unsealed.bin)"
check "round trip" yes "$(cmp -s protkey.bin unsealed.bin && echo yes || echo no)"
check "a wrong key does not unseal: exit 8" 8 "$(head -c 32 /dev/urandom > otherk.bin; rc mnd unseal --key-file otherk.bin --in sealed.bin --out x.bin)"
check "a damaged file does not unseal: exit 8" 8 "$(cp sealed.bin dam.bin; printf '\377' | dd of=dam.bin bs=1 seek=20 conv=notrunc 2>/dev/null; rc mnd unseal --key-file k.bin --in dam.bin --out x.bin)"
check "the PIN is never in the output" yes "$(grep -c 1234 out.txt err.txt | grep -v ':0' | wc -l | awk '{print ($1==0)?"yes":"no"}')"

# ------------------------------------------------------------------ monotonic counters
fresh
check "reading a counter that was never initialised: not found" 6 "$(rc counter get 2)"
check "counter init without its flag: refused" 9 "$(rc counter init 2 5)"
check "--dry-run init" 0 "$(rc --dry-run counter init 2 5)"
check "the dry run set nothing" 6 "$(rc counter get 2)"
check "counter init" 0 "$(rc counter init 2 3 --yes-init-counter)"
check "counter get" "counter 2: 3" "$(T counter get 2 2>/dev/null)"
check "counter update without its flag: refused" 9 "$(rc counter update 2)"
check "counter update decrements by one" "counter 2: 3 -> 2" "$(T counter update 2 --yes-update-counter 2>/dev/null)"
check "--dry-run update" 0 "$(rc --dry-run counter update 2)"
check "the dry run changed nothing" "counter 2: 2" "$(T counter get 2 2>/dev/null)"
T counter update 2 --yes-update-counter >/dev/null 2>&1; T counter update 2 --yes-update-counter >/dev/null 2>&1
check "the counter reached zero" "counter 2: 0" "$(T counter get 2 2>/dev/null)"
check "an update at zero is refused before the chip is asked: exit 9" 9 "$(rc counter update 2 --yes-update-counter)"
check "init may be repeated (libtropic's functional test)" 0 "$(rc counter init 2 9 --yes-init-counter)"
check "a value above 0xFFFFFFFE" 2 "$(rc counter init 2 4294967295 --yes-init-counter)"
check "counter 16" 2 "$(rc counter get 16)"
check "the largest value" 0 "$(rc counter init 3 4294967294 --yes-init-counter)"
if [ "$HAVE_PY" = 1 ]; then rc --json counter get 3 >/dev/null; check "counter --json is valid" valid "$(json_ok out.txt)"; fi
sed -i 's/^TR01_CFG_UAP_MCOUNTER_UPDATE .*/TR01_CFG_UAP_MCOUNTER_UPDATE 0xfffffefe/' chip/rconfig   # clear key 0 for counters 4-7 only... and 0-3 below
sed -i 's/^TR01_CFG_UAP_MCOUNTER_UPDATE .*/TR01_CFG_UAP_MCOUNTER_UPDATE 0xfffffffe/' chip/rconfig   # key 0 denied for counters 0-3
check "[assumed layout] the shared key is denied updating counters 0-3" 5 "$(rc counter update 2 --yes-update-counter)"

# ------------------------------------------------------------------ info mode and certificates
fresh
check "info mode" "application" "$(T info mode 2>/dev/null)"
check "info cert on a chip with no certificates" 0 "$(rc info cert)"
check "four entries" 4 "$(grep -c 'bytes' out.txt)"
mkdir -p chip/certs; head -c 300 /dev/urandom > chip/certs/0.der
check "info cert --out-dir writes the certificate" 0 "$(rc info cert --out-dir certs-out)"
check "300 bytes" 300 "$(wc -c < certs-out/cert0.der | tr -d ' ')"

# ------------------------------------------------------------------ the PIN-protected session (--key pin): tier 1 of the cra-04 design
fresh; setkey 1
openssl genpkey -algorithm X25519 -outform DER -out prot.der 2>/dev/null; tail -c 32 prot.der > prot.priv; rawpub prot.der > prot.pub
T mem write 6 --in in.bin >/dev/null 2>&1
check "write the protected key's public half into pairing slot 2" 0 "$(rc pairing write 2 --in prot.pub --yes-write-pairing-slot)"
check "set the PIN, using the bootstrap (unit) key" 0 "$(rc --key unit mnd setup --state keys/pin.state --out kpin.bin --pin-file pin.txt --yes-setup-mnd --slots 3)"
check "seal the protected private key under the released key" 0 "$(rc mnd seal --key-file kpin.bin --in prot.priv --out keys/protected.enc)"
echo 2 > keys/protected.slot
# only the protected key (slot 2) may read R-MEM; the bootstrap key (slot 1) keeps MAC-and-Destroy
sed -i 's/^TR01_CFG_UAP_R_MEM_DATA_READ .*/TR01_CFG_UAP_R_MEM_DATA_READ 0xfffffff4/' chip/rconfig
check "the bootstrap key is now denied R-MEM reads" 5 "$(rc --key unit mem read 6)"
check "session test with the PIN" 0 "$(rc --key pin --pin-file pin.txt session test)"
check_contains "says which key opened it" "PIN-protected key, slot 2" "$(cat out.txt)"
check "reading R-MEM with the PIN-protected key" 0 "$(rc --key pin --pin-file pin.txt mem read 6 --out got6.bin)"
check "the data is right" yes "$(cmp -s in.bin got6.bin && echo yes || echo no)"
check "the PIN can come from standard input" 0 "$(echo 1234 | T --key pin --pin-stdin mem read 6 --out got6b.bin >out.txt 2>err.txt; echo $?)"
check "a wrong PIN: exit 8, no session" 8 "$(rc --key pin --pin-file wrong.txt mem read 6)"
check_contains "naming the attempts left" "attempt(s) left" "$(cat err.txt)"
check "the right PIN works again afterwards" 0 "$(rc --key pin --pin-file pin.txt mem read 6 --out got6c.bin)"
rm -f keys/protected.enc
check "without the sealed key file: refused" 4 "$(rc --key pin --pin-file pin.txt session test)"
T mnd seal --key-file kpin.bin --in prot.priv --out keys/protected.enc 2>/dev/null
rm -f keys/pin.state
check "without the PIN state: refused" 4 "$(rc --key pin --pin-file pin.txt session test)"
fresh; setkey 1; T pairing write 2 --in prot.pub --yes-write-pairing-slot >/dev/null 2>&1
T --key unit mnd setup --state keys/pin.state --out kpin.bin --pin-file pin.txt --yes-setup-mnd --slots 2 >/dev/null 2>&1
T mnd seal --key-file kpin.bin --in prot.priv --out keys/protected.enc 2>/dev/null; echo 2 > keys/protected.slot
rc --key pin --pin-file wrong.txt session test >/dev/null; rc --key pin --pin-file wrong.txt session test >/dev/null
check "after two wrong PINs no attempts remain: even the right PIN is refused (exit 9)" 9 "$(rc --key pin --pin-file pin.txt session test)"
check_contains "and the message says the key can no longer be opened" "can no longer be opened" "$(cat err.txt)"

# ------------------------------------------------------------------ the rest of legacy mode
fresh
check "lt-util -e -g" 0 "$(lrc -e -g 3)"
check "lt-util -e -g on an occupied slot fails with 1" 1 "$(lrc -e -g 3)"
check "lt-util -e -d writes the public key" 0 "$(lrc -e -d 3 lp.bin)"
check "64 bytes for P-256 (the old tool's full export)" 64 "$(wc -c < lp.bin | tr -d ' ')"
check "lt-util -e -s on a P-256 slot fails (EdDSA only, as before)" 1 "$(lrc -e -s 3 msg.bin ls.bin)"
check "lt-util -e -i installs an Ed25519 key" 0 "$(lrc -e -i 4 ed.priv)"
check "lt-util -e -s signs with it" 0 "$(lrc -e -s 4 msg.bin ls4.bin)"
check "the signature verifies" "Signature Verified Successfully" "$(T ecc pub 4 --format pem --out lp4.pem 2>/dev/null; openssl pkeyutl -verify -pubin -inkey lp4.pem -rawin -in msg.bin -sigfile ls4.bin 2>&1)"
check "lt-util -e -c clears the slot (no flag needed, as before)" 0 "$(lrc -e -c 3)"
check "the slot is empty" 1 "$(lrc -e -d 3 lp.bin)"
head -c 32 /dev/urandom > lnew.pub
check "lt-util -k -w without --irreversible fails with 1" 1 "$(lrc -k -w 1 lnew.pub x)"
check_contains "with the old message" "pass the literal argument --irreversible" "$(cat err.txt)"
check "lt-util -k -w slot 0 fails with 1" 1 "$(lrc -k -w 0 lnew.pub --irreversible)"
check "lt-util -k -w ... --irreversible" 0 "$(lrc -k -w 1 lnew.pub --irreversible)"
check "the slot holds the key" yes "$(lrc -k -r 1 lgot.pub >/dev/null; cmp -s lnew.pub lgot.pub && echo yes || echo no)"
