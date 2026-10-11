# Test cases: default slot-0 keys, chip restart, firmware status and update. Sourced by run.sh.
# Rules in the fake that these depend on are labelled [assumed] in src/chip_fake.c: a firmware update needs Maintenance Mode
# in the I-config and in the R-config in force, and a chip never accepts an older version.

echo "# default slot-0 keys"
rm -rf chip keys && mkdir keys; TROPICCTL_FAKE_SH0=prod0 T info fw >/dev/null 2>&1
check "a production-provisioned chip refuses the shared engineering key: exit 4" 4 "$(rc --key shared session test)"
check_contains "and the message points at prod0" "try --key prod0" "$(cat err.txt)"
check "--key prod0 opens a session" 0 "$(rc --key prod0 session test)"
check_contains "and says which key it used" "production (prod0)" "$(cat err.txt)"
check "--key es01 on that chip: exit 4" 4 "$(rc --key es01 session test)"
fresh
check "--key es01 opens a session on an engineering chip" 0 "$(rc --key es01 session test)"
check "--key prod0 is refused by an engineering chip: exit 4" 4 "$(rc --key prod0 session test)"
check "an unknown key name is a usage error" 2 "$(rc --key prod1 session test)"

echo "# the default slot-0 key setting (TROPICCTL_SH0 or <key dir>/slot0-key)"
rm -rf chip keys && mkdir keys; TROPICCTL_FAKE_SH0=prod0 T info fw >/dev/null 2>&1
check "a production chip refuses the default shared key: exit 4" 4 "$(rc session test)"
check_contains "and the hint says how to set it once" "echo prod0 >" "$(cat err.txt)"
check "TROPICCTL_SH0=prod0 makes the default shared key prod0" 0 "$(TROPICCTL_SH0=prod0 rc session test)"
check_contains "and the warning names it" "shared production (prod0)" "$(cat err.txt)"
echo prod0 > keys/slot0-key
check "<key dir>/slot0-key does the same without the environment" 0 "$(rc session test)"
check "--key shared follows the setting" 0 "$(rc --key shared session test)"
check "the environment overrides the file" 4 "$(TROPICCTL_SH0=es01 rc session test)"
echo bogus > keys/slot0-key
check "an unknown name is refused, not ignored: exit 4" 4 "$(rc session test)"
check_contains "naming the setting" "is not es01 or prod0" "$(cat err.txt)"
fresh
check "an engineering chip still works with no setting at all" 0 "$(rc session test)"
check_contains "and the warning keeps its old wording" "shared engineering-sample key" "$(cat err.txt)"

echo "# chip reboot"
fresh
check "chip reboot" 0 "$(rc chip reboot)"
check "--dry-run does not touch the chip" 0 "$(rc --dry-run chip reboot)"
check "an argument is a usage error" 2 "$(rc chip reboot now)"
check "--json" valid "$(T --json chip reboot > j.txt 2>/dev/null; json_ok j.txt)"

echo "# fw status"
fresh
check "fw status" 0 "$(rc fw status)"
check_contains "the versions" "RISC-V firmware 0.1.2" "$(cat out.txt)"
check_contains "SPECT" "SPECT firmware 0.3.1" "$(cat out.txt)"
check_contains "a chip older than 2.0.0 is noted" "older than 2.0.0" "$(cat out.txt)"
check "--json" valid "$(T --json fw status > j.txt 2>/dev/null; json_ok j.txt)"
check_contains "JSON names the version" '"riscv_version":"0.1.2"' "$(cat j.txt)"

echo "# fw update"
mkdir -p img220 img101
head -c 3000 /dev/urandom > img220/fw_v2.2.0.hex32_signed_chunks.bin
head -c 2000 /dev/urandom > img220/spect_app-v1.3.0_signed_chunks.bin
head -c 3000 /dev/urandom > img101/fw_v1.0.1.hex32_signed_chunks.bin
head -c 2000 /dev/urandom > img101/spect_app-v1.0.0_signed_chunks.bin
acab() { printf ACAB > chip/silicon; }
fresh; acab
check "without its flag: refused" 9 "$(rc fw update --dir img220)"
check "--dry-run checks and changes nothing" 0 "$(rc --dry-run fw update --dir img220)"
check_contains "and says what it would do" "would update the firmware" "$(cat err.txt)"
check_contains "the chip still runs the old firmware" "RISC-V firmware 0.1.2" "$(T fw status 2>/dev/null)"
check "no images given: usage error" 2 "$(rc fw update --yes-update-firmware)"
check "a directory without the images: usage error" 2 "$(mkdir -p empty; rc fw update --dir empty --yes-update-firmware)"
check "--dir with --cpu: usage error" 2 "$(rc fw update --dir img220 --cpu x --yes-update-firmware)"
cp img220/fw_v2.2.0.hex32_signed_chunks.bin noversion.bin
check "an image whose name carries no version: usage error" 2 "$(rc fw update --cpu noversion.bin --spect img220/spect_app-v1.3.0_signed_chunks.bin --yes-update-firmware)"
rm -f chip/silicon
check "a chip without a silicon field is ABAB; this build updates ACAB: refused" 9 "$(rc fw update --dir img220 --yes-update-firmware)"
check_contains "and says why" "silicon revision ABAB" "$(cat err.txt)"
acab
T fw update --dir img220 --yes-update-firmware >/dev/null 2>&1
check "a downgrade is refused" 9 "$(rc fw update --dir img101 --yes-update-firmware)"
check_contains "and says so" "does not accept a downgrade" "$(cat err.txt)"
fresh; acab
check "the update" 0 "$(rc fw update --dir img220 --yes-update-firmware)"
check_contains "is read back" "firmware updated and read back: RISC-V 2.2.0, SPECT 1.3.0" "$(cat out.txt)"
check_contains "status shows the new versions" "RISC-V firmware 2.2.0" "$(T fw status 2>/dev/null)"
check "the same versions again: nothing to do" 0 "$(rc fw update --dir img220 --yes-update-firmware)"
check_contains "and says so" "nothing to do" "$(cat err.txt)"
check "the tool's own I-config burn gate now passes (CPU firmware >= 2.0.0)" 0 "$(rc config burn UAP_PING 3 --dry-run)"
check "--cpu and --spect work like --dir" 0 "$(fresh; acab; rc fw update --cpu img220/fw_v2.2.0.hex32_signed_chunks.bin --spect img220/spect_app-v1.3.0_signed_chunks.bin --yes-update-firmware)"

echo "# fw update and Maintenance Mode"
fresh; acab
sed -i 's/^TR01_CFG_START_UP .*/TR01_CFG_START_UP 0xfffffff7/' chip/rconfig
check "Maintenance Mode off in the R-config: refused, with the way out" 9 "$(rc fw update --dir img220 --yes-update-firmware)"
check_contains "naming the option" "--enable-maintenance" "$(cat err.txt)"
check "--dry-run says it would enable it" 0 "$(rc --dry-run fw update --dir img220 --yes-update-firmware --enable-maintenance)"
check_contains "in words" "would enable Maintenance Mode" "$(cat err.txt)"
check "with --enable-maintenance the update runs" 0 "$(rc fw update --dir img220 --yes-update-firmware --enable-maintenance --backup bkm.cfg)"
check "the R-config was backed up first" 27 "$(grep -c '^R ' bkm.cfg)"
check "the backup holds the old START_UP" "0xfffffff7" "$(grep '^R TR01_CFG_START_UP' bkm.cfg | awk '{print $3}')"
check_contains "Maintenance Mode was left enabled and the message says how to turn it off" "fw maintenance off" "$(cat out.txt)"
check "fw maintenance off needs its flag" 9 "$(rc fw maintenance off)"
check "fw maintenance off" 0 "$(rc fw maintenance off --yes-rewrite-r-config --backup bkm2.cfg)"
check "START_UP is back to the old value in the R-config" "0xfffffff7" "$(grep '^TR01_CFG_START_UP' chip/rconfig | awk '{print $2}')"
check "fw maintenance on" 0 "$(rc fw maintenance on --yes-rewrite-r-config --backup bkm3.cfg)"
check "bit 3 is set again" "0xffffffff" "$(grep '^TR01_CFG_START_UP' chip/rconfig | awk '{print $2}')"
check "a bad argument is a usage error" 2 "$(rc fw maintenance maybe --yes-rewrite-r-config)"
fresh; acab
check "--disable-maintenance turns it off after a verified update" 0 "$(rc fw update --dir img220 --yes-update-firmware --disable-maintenance)"
check "the R-config no longer has Maintenance Mode (bit 3 of 0xffffffef cleared)" "0xffffffe7" "$(grep '^TR01_CFG_START_UP' chip/rconfig | awk '{print $2}')"
fresh; acab
check_contains "JSON says it is off" '"maintenance_mode_left_enabled":false' "$(T --json fw update --dir img220 --yes-update-firmware --disable-maintenance 2>/dev/null)"
fresh; acab
# the I-config bit cannot be set again: when it is clear the chip can never be updated
sed -i 's/^TR01_CFG_START_UP .*/TR01_CFG_START_UP 0xfffffff7/' chip/iconfig
check "Maintenance Mode disabled in the I-config: refused" 9 "$(rc fw update --dir img220 --yes-update-firmware)"
check_contains "and the message says it is irreversible" "irreversible" "$(cat err.txt)"

echo "# fw update failures"
fresh; acab
check "an update that fails: exit 10" 10 "$(TROPICCTL_FAKE_FWUPDATE_FAIL=1 rc fw update --dir img220 --yes-update-firmware)"
check_contains "tells the user to run it again" "run the update again" "$(cat err.txt)"
check_contains "and the chip still runs what it ran" "RISC-V firmware 0.1.2" "$(T fw status 2>/dev/null)"
check "an update that reports success but changes nothing: exit 8" 8 "$(TROPICCTL_FAKE_FWUPDATE_SILENT=1 rc fw update --dir img220 --yes-update-firmware)"
check_contains "and says what the chip runs" "the chip runs RISC-V 0.1.2" "$(cat err.txt)"

echo "# a key that may read and rewrite the R-config but not read the I-config"
# [measured 2026-10-11, spare chip] config dump reads both; a key without UAP_I_CONFIG_READ was refused. The tool now shows the
# R-config alone, so such a key can still back up and restore the configuration.
fresh; setkey 1
T --key unit config dump 2>/dev/null | grep '^R ' > full.cfg
sed -i 's/^TR01_CFG_UAP_I_CONFIG_READ .*/TR01_CFG_UAP_I_CONFIG_READ 0xfffffff4/' chip/rconfig
check "config dump works without the I-config right" 0 "$(rc --key unit config dump)"
check "it shows the R-config" 27 "$(grep -c '^R ' out.txt)"
check "and no I-config lines" 0 "$(grep -c '^I ' out.txt)"
check_contains "and says why" "UAP_I_CONFIG_READ" "$(cat err.txt)"
check "--json: i_config is null" valid "$(T --key unit --json config dump > j.txt 2>/dev/null; grep -q '"i_config":null' j.txt && json_ok j.txt)"
check "config get shows R only" 0 "$(rc --key unit config get START_UP)"
check "config restore works with such a key" 0 "$(rc --key unit config restore --in full.cfg --yes-rewrite-r-config --backup bki.cfg)"
echo "00000002 00000100" > chip/fw
sed -i 's/^TR01_CFG_UAP_I_CONFIG_READ .*/TR01_CFG_UAP_I_CONFIG_READ 0xfffffff4/' chip/rconfig
check "config burn without the right to read the I-config: exit 5" 5 "$(rc --key unit config burn UAP_PING 3 --yes-burn-i-config-bit)"
