#!/bin/bash
# tropic01-vault: the state machine and its safety rules, with a fake chip (R-MEM slots are files) and a
# fake cryptsetup. It checks the logic, not the encryption: see tests/hardware for the real stack.
set -u
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"
V="$REPO/features/cra-05-secure-erase/tropic01-vault/files/usr/sbin/tropic01-vault"
cd "$WORK" && mkdir -p bin chip tmp map mnt img && : > mounts

cat > bin/lt-util <<'EOF2'
#!/bin/sh
[ -n "${FAIL_CHIP:-}" ] && exit 1
case "$1 $2" in
"-i ") exit 0 ;;
"-m -s") [ -e "$CHIP/slot$3" ] && exit 1; cp "$4" "$CHIP/slot$3"; echo "$3" >> "$CHIP/writes" ;;
"-m -r") : > "$4"; [ -e "$CHIP/slot$3" ] || exit 1; cp "$CHIP/slot$3" "$4" ;;
"-m -e") [ -n "${FAIL_ERASE:-}" ] && exit 1; rm -f "$CHIP/slot$3"; echo "$3" >> "$CHIP/erases" ;;
*) case "$1" in -r) head -c "$2" /dev/urandom > "$3" ;; *) exit 1 ;; esac ;;
esac
EOF2
cat > bin/cryptsetup <<'EOF2'
#!/bin/sh
echo "$*" >> "$VT/cs.log"
case "$1" in
open) touch "$TROPIC01_VAULT_DEVMAP/tropic01-vault" ;;
close) rm -f "$TROPIC01_VAULT_DEVMAP/tropic01-vault" ;;
esac
EOF2
cat > bin/mount <<'EOF2'
#!/bin/sh
echo "$1 $2 ext4 rw 0 0" >> "$TROPIC01_VAULT_MOUNTS"
EOF2
cat > bin/umount <<'EOF2'
#!/bin/sh
grep -v " $1 " "$TROPIC01_VAULT_MOUNTS" > "$TROPIC01_VAULT_MOUNTS.n"; mv "$TROPIC01_VAULT_MOUNTS.n" "$TROPIC01_VAULT_MOUNTS"
EOF2
printf '#!/bin/sh\nexit 0\n' > bin/mke2fs
cat > bin/uci <<'EOF2'
#!/bin/sh
[ "$1" = "-q" ] && shift
case "$2" in
 tropic01-vault.main.mount) echo "$VT/mnt/vault" ;;
 tropic01-vault.main.image) echo "$VT/img/vault.img" ;;
 tropic01-vault.main.size_mb) echo 1 ;;
 tropic01-vault.main.slot) echo "${SLOT:-100}" ;;
 *) exit 1 ;;
esac
EOF2
# shellcheck disable=SC2016  # the fake logger's body must stay unexpanded
printf '#!/bin/sh\nshift 2; echo "$*" >> "$VT/log"\n' > bin/logger
chmod +x bin/*
export VT="$WORK" CHIP="$WORK/chip" PATH="$WORK/bin:$PATH" TROPIC01_VAULT_LTUTIL="$WORK/bin/lt-util" TROPIC01_VAULT_CRYPTSETUP="$WORK/bin/cryptsetup" \
  TROPIC01_VAULT_TMP="$WORK/tmp" TROPIC01_VAULT_MOUNTS="$WORK/mounts" TROPIC01_VAULT_DEVMAP="$WORK/map" TROPIC01_VAULT_RETRY_SLEEP=0
run() { sh "$V" "$@" >out.txt 2>&1; echo $?; }
keyfiles() { find tmp -maxdepth 1 -name 'tropic01-vault*' | wc -l | tr -d ' '; }
slot() { [ -e chip/slot100 ] && echo populated || echo empty; }
open_() { [ -e map/tropic01-vault ] && echo yes || echo no; }

check "status before init: no image" "image: none (run: tropic01-vault init)" "$(sh "$V" status 2>&1 | sed -n 1p)"

check "init succeeds" "0" "$(run init)"
check "init puts a 32-byte key in the chip (slot 100)" "populated 32" "$(slot) $(wc -c < chip/slot100 | tr -d ' ')"
check "init creates the image and mounts it" "yes 1" "$([ -f img/vault.img ] && echo yes) $(grep -c vault mounts)"
check "init leaves no key file in the RAM dir" "0" "$(keyfiles)"
check_contains "init uses the small LUKS2 header" "--offset 2048" "$(cat cs.log)"
check "a second init is refused" "1" "$(run init)"
check_contains "  and says the vault already exists" "already exists" "$(cat out.txt)"

mv img/vault.img img/keep.img; before=$(cksum < chip/slot100)
check "init into a non-empty slot is refused" "1" "$(run init)"
check_contains "  and says the slot is not empty" "not empty" "$(cat out.txt)"
check "  and does not overwrite the key" "$before" "$(cksum < chip/slot100)"
check "  and writes the chip only once so far" "1" "$(wc -l < chip/writes | tr -d ' ')"
mv img/keep.img img/vault.img

check "close succeeds" "0" "$(run close)"
check "close unmounts and locks" "0 no" "$(grep -c vault mounts) $(open_)"
check "open succeeds (key read back from the chip)" "0" "$(run open)"
check "open mounts and unlocks; no key file left" "1 yes 0" "$(grep -c vault mounts) $(open_) $(keyfiles)"

run close >/dev/null
check "open fails when the chip is unavailable" "1" "$(FAIL_CHIP=1 run open)"
check "  and the vault stays locked with no key file left" "no 0" "$(open_) $(keyfiles)"

check "destroy needs its explicit flag" "1" "$(run destroy)"
check "  and then changes nothing" "populated kept" "$(slot) $([ -f img/vault.img ] && echo kept || echo gone)"
check "destroy is refused when the erase fails" "1" "$(FAIL_ERASE=1 run destroy --yes-destroy-vault)"
check_contains "  and says the vault was NOT destroyed" "NOT destroyed" "$(cat out.txt)"
check "  and keeps the image" "kept" "$([ -f img/vault.img ] && echo kept || echo gone)"
check "destroy with the flag succeeds" "0" "$(run destroy --yes-destroy-vault)"
check "  and the key is erased before the image is deleted" "empty gone" "$(slot) $([ -f img/vault.img ] && echo kept || echo gone)"
check "open after destroy fails" "1" "$(run open)"

check "a slot below 32 is refused (factory certificates)" "1" "$(SLOT=1 run status)"
check_contains "  with an explanation" "outside the general-purpose" "$(cat out.txt)"
check "a slot above 511 is refused" "1" "$(SLOT=600 run status)"
check "a non-numeric slot is refused" "1" "$(SLOT=abc run status)"
check "only slot 100 was ever written or erased" "100 100" "$(sort -u chip/writes | tr '\n' ' ' | sed 's/ $//') $(sort -u chip/erases | tr '\n' ' ' | sed 's/ $//')"
finish
