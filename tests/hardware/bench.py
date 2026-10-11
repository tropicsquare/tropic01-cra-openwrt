#!/usr/bin/env python3
"""Prompt-driven serial-console driver for the OpenWrt One test bench.

Host side only; needs `pexpect`. Opens the board's USB debug console once,
waits for a recognisable prompt before it sends anything (never types
blind: a keystroke sent to the wrong prompt becomes a password), and keeps a
transcript outside the repository.

Credentials: the bench root password is generated here, kept in a 0600 file
under ~/.cache/tropic01-bench/ (override with BENCH_PW_FILE), sent without
being echoed to the transcript, and never printed. It is a throwaway bench
credential, not a production one -- see features/cra-01-secure-defaults/CLAUDE.md.

    bench.py probe      classify the console state, send only a newline
    bench.py cra01      run the cra-01 console scenarios (see tests/hardware/README.md)
"""
import argparse
import fcntl
import os
import re
import secrets
import struct
import sys
import termios
import time

import pexpect
from pexpect import fdpexpect

PORT = os.environ.get("BENCH_PORT", "/dev/ttyACM0")
CACHE = os.path.expanduser("~/.cache/tropic01-bench")
# Public value: the sha256-crypt hash of the empty string (salt "tropic01x"). Planting it
# reproduces what LuCI's first-login screen used to store.
EMPTY_HASH = "$5$tropic01x$Ept3Ny/YSsFOft1S.Q9pP6pzX3IOOwrh0Ox5cUauXd2"
PW_FILE = os.path.expanduser(os.environ.get("BENCH_PW_FILE", os.path.join(CACHE, "root-password")))

# Console states, recognised from the end of what the board printed.
P_SHELL = r"root@OpenWrt:[^\r\n]*# "
P_LOGIN = r"login: "
P_LOGIN_PW = r"Password: "
P_NEW = r"New password: "
P_RETYPE = r"Retype password: "
STATES = [("shell", P_SHELL), ("login", P_LOGIN), ("login_pw", P_LOGIN_PW), ("new_pw", P_NEW), ("retype", P_RETYPE)]


def open_port():
    fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY)
    a = termios.tcgetattr(fd)
    a[0] = 0
    a[1] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[3] = 0
    a[4] = a[5] = termios.B115200
    a[6][termios.VMIN] = 1
    a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    # The board's USB bridge stays silent unless DTR/RTS are asserted.
    fcntl.ioctl(fd, termios.TIOCMSET, struct.pack("I", termios.TIOCM_DTR | termios.TIOCM_RTS))
    return fd


class Bench:
    def __init__(self):
        os.makedirs(CACHE, mode=0o700, exist_ok=True)
        self.log_path = os.path.join(CACHE, "transcript-%s.log" % time.strftime("%Y%m%d-%H%M%S"))
        self.log = open(self.log_path, "w", buffering=1)
        os.chmod(self.log_path, 0o600)
        self.fd = open_port()
        self.c = fdpexpect.fdspawn(self.fd, timeout=10, encoding="utf-8", codec_errors="replace")
        self.c.logfile_read = self.log
        self.results = []

    # -- credentials ------------------------------------------------------
    def password(self):
        if not os.path.exists(PW_FILE):
            os.makedirs(os.path.dirname(PW_FILE), mode=0o700, exist_ok=True)
            fd = os.open(PW_FILE, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, "w") as f:
                f.write(secrets.token_urlsafe(18) + "\n")
        with open(PW_FILE) as f:
            return f.read().strip()

    # -- low level ----------------------------------------------------------
    def drain(self, quiet=0.7, limit=20):
        for _ in range(limit):  # bounded: a console that never goes quiet must not hang the run
            try:
                self.c.expect(pexpect.TIMEOUT, timeout=quiet)
            except pexpect.EOF:
                return
            if not self.c.before:
                return
            self.c.buffer = ""

    def sync(self, timeout=8):
        """Send a newline and say which prompt answers. Sends nothing else."""
        self.drain()
        self.c.send("\n")
        try:
            i = self.c.expect([p for _, p in STATES], timeout=timeout)
        except pexpect.TIMEOUT:
            return "unknown"
        return STATES[i][0]

    def say(self, line):
        self.c.sendline(line)

    def secret(self, value):
        # Not part of the transcript: logfile_send is deliberately unset.
        self.c.sendline(value)

    def record(self, name, ok, detail=""):
        self.results.append((name, ok, detail))
        print("%s  %s%s" % ("PASS" if ok else "FAIL", name, ("  -- " + detail) if detail else ""), flush=True)

    # -- shell helpers ----------------------------------------------------------
    def run(self, cmd, timeout=20):
        """Run a command at a shell prompt; return (rc, output).

        The command line is echoed back and wraps at the terminal width, so the
        output is taken between a standalone BEGIN line and the END marker.
        """
        self.drain(0.3)
        self.c.sendline("echo __BEGIN__; %s; echo __END_$?__" % cmd)
        self.c.expect(r"\n__BEGIN__\r?\n(.*?)__END_(\d+)__\r?\n", timeout=timeout)
        out = self.c.match.group(1).replace("\r", "").strip()
        rc = int(self.c.match.group(2))
        self.c.expect(P_SHELL, timeout=5)
        return rc, out

    def login(self, password=None):
        """From a login prompt to whatever follows. Returns the next state."""
        self.say("root")
        i = self.c.expect([P_LOGIN_PW, P_NEW, P_SHELL], timeout=20)
        if i == 0:
            self.secret(password if password is not None else "")
            i = self.c.expect([P_NEW, P_SHELL, r"Login incorrect"], timeout=30)
            return ["new_pw", "shell", "bad_login"][i]
        return ["login_pw", "new_pw", "shell"][i]

    def ensure_shell(self):
        """Get to a shell using the bench password, falling back to an empty one."""
        pw = self.password()
        for attempt in range(6):
            state = self.sync(timeout=15)
            if state == "shell":
                return True
            if state == "login":
                state = self.login(pw if attempt % 2 == 0 else "")
                if state == "shell":
                    return True
            if state == "new_pw":
                # First boot after a clean flash: the forced-password prompt. Set the bench password.
                self.secret(pw)
                self.c.expect(P_RETYPE, timeout=15)
                self.secret(pw)
                self.c.expect(P_SHELL, timeout=40)
                return True
        return False


# ---------------------------------------------------------------------------
def cmd_probe(b):
    state = b.sync()
    print("console state: %s" % state)
    print("transcript: %s" % b.log_path)
    return 0


def cmd_capture(b, seconds):
    """Record the console for N seconds, sending nothing (start it, then power-cycle)."""
    print("capturing %ds, sending nothing; transcript: %s" % (seconds, b.log_path), flush=True)
    b.c.expect(pexpect.TIMEOUT, timeout=seconds)
    n = b.log.tell()
    print("captured %d bytes" % n)
    return 0


def cmd_web(b):
    """Web/SSH listener checks from the board itself (the PC cannot reach the router LAN)."""
    if not b.ensure_shell():
        b.record("could not reach a shell", False)
        return 1
    rc, out = b.run("netstat -ltn | awk '$4 ~ /:(22|80|443)$/ {print $4}' | sort | tr '\\n' ' '")
    b.record("SSH listens on the LAN address only, not 0.0.0.0", ":22" in out and "0.0.0.0:22" not in out, out)
    b.record("HTTP and HTTPS listeners present", ":80" in out and ":443" in out, out)
    rc, out = b.run("wget -q -O /dev/null --no-check-certificate https://127.0.0.1/ 2>&1; echo https_rc=$?")
    b.record("LuCI answers over HTTPS (self-signed)", "https_rc=0" in out, out)
    rc, out = b.run("wget -q -O /dev/null http://127.0.0.1/ 2>&1; echo http_rc=$?")
    b.record("LuCI answers over HTTP", "http_rc=0" in out, out)
    rc, out = b.run("ubus call session login '{\"username\":\"root\",\"password\":\"\"}' >/dev/null 2>&1 && echo ACCEPTED || echo REJECTED")
    b.record("rpcd (LuCI's login) rejects an empty password", out.strip() == "REJECTED", out)
    rc, out = b.run("uci -q get dropbear.@dropbear[0].Interface; uci -q get dropbear.@dropbear[0].PasswordAuth")
    b.record("dropbear bound to lan, password auth on", out.split() == ["lan", "on"], out.replace("\n", " "))
    return 0


def push_file(b, local, remote, host_ip, port=9000, tries=3):
    """Copy a file to the board over a one-shot TCP connection and verify its SHA-256.

    Use the wired bench link (see tests/hardware/README.md). Pushing data through the
    serial console was tried and hung the board twice, so it is deliberately not offered.
    """
    import hashlib
    import subprocess
    want = hashlib.sha256(open(local, "rb").read()).hexdigest()
    for _ in range(tries):
        srv = subprocess.Popen(["nc", "-l", "-p", str(port), "-q", "1"], stdin=open(local, "rb"),
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(1)
            rc, out = b.run("nc %s %d > %s; chmod +x %s; sha256sum %s | cut -d' ' -f1" % (host_ip, port, remote, remote, remote), timeout=120)
        finally:
            srv.kill()
            srv.wait()
        if out.strip().split("\n")[-1] == want:
            return True
    return False


def stripped_copy(path):
    import glob
    import shutil
    import subprocess
    import tempfile
    strip = glob.glob(os.path.join(os.path.dirname(__file__), "..", "..", "build", "openwrt", "staging_dir",
                                   "toolchain-*", "bin", "*-strip"))
    out = os.path.join(tempfile.mkdtemp(prefix="bench-"), os.path.basename(path))
    shutil.copy(path, out)
    if strip:
        subprocess.run([strip[0], out], check=True)
    return out


def cmd_cra04(b, binary, host_ip):
    """cra-04 on the real chip, read-only: tropicctl (installed, or pushed to /tmp), nothing is flashed or written."""
    if not b.ensure_shell():
        b.record("could not reach a shell", False)
        return 1
    if binary == "installed":
        tc = "/usr/bin/tropicctl"
        b.record("using the tropicctl installed in the image", b.run("test -x %s && echo yes" % tc)[1].strip() == "yes")
    else:
        local = stripped_copy(binary)
        ok = push_file(b, local, "/tmp/tc-new", host_ip)
        b.record("pushed tropicctl to /tmp over the wired bench link", ok)
        if not ok:
            return 1
        tc = "/tmp/tc-new"

    def run(args, timeout=60):
        """Run tropicctl; returns (exit code, combined output without the key notices)."""
        rc, out = b.run("%s %s >/tmp/tc.out 2>&1; echo rc=$?; cat /tmp/tc.out | head -12" % (tc, args), timeout=timeout)
        lines = out.strip().split("\n")
        code = -1
        for line in lines:
            if line.startswith("rc="):
                code = int(line[3:])
        return code, out

    # The write command must refuse BEFORE touching the chip: no session is opened, so no key is ever chosen or announced.
    for name, args, want in (("without --yes-write-pairing-slot", "pairing write 1 --in /tmp/none", 9),
                             ("for slot 0", "pairing write 0 --in /tmp/none --yes-write-pairing-slot", 9),
                             ("for a file that is not 32 bytes", "pairing write 1 --in /etc/hostname --yes-write-pairing-slot", 2)):
        code, out = run(args)
        b.record("pairing-slot write is refused " + name + ", before any chip access",
                 code == want and "pairing key" not in out.replace("pairing key files", ""), out.replace("\n", " "))
    code, out = run("--dry-run pairing write 1 --in /etc/hostname")
    b.record("a pairing-slot write stays refused for a bad file even as a dry run", code == 2, out.replace("\n", " "))

    code, out = run("session test")
    b.record("shared key still in use when no key files exist (session test)", code == 0 and "no per-unit pairing key" in out, out.replace("\n", " | "))
    code, out = run("info chip")
    b.record("chip identification", code == 0 and "CHIP_ID ver" in out, out.replace("\n", " | ")[:200])
    code, out = run("mem read 1 --out /tmp/c1")
    rc, size = b.run("wc -c < /tmp/c1")
    b.record("secure session with the shared key still works (R-MEM slot 1 read)", code == 0 and size.strip() == "299", "rc=%s size=%s" % (code, size.strip()))
    code, out = run("pairing list")
    b.record("pairing slots inspected: slot 0 populated", code == 0 and "pairing slot 0: populated" in out, out.replace("\n", " | "))
    code, out = run("info fw")
    b.record("firmware version bytes", code == 0 and "RISCV_FW_VER" in out, out.replace("\n", " | "))
    code, out = run("info mode")
    b.record("chip mode", code == 0 and "application" in out, out.replace("\n", " | "))
    code, out = run("config dump")
    rc, n = b.run("%s config dump 2>/dev/null | wc -l" % tc, timeout=60)
    b.record("configuration read: 27 R and 27 I objects", code == 0 and n.strip().split("\n")[-1] == "54", "lines=%s" % n.strip())
    rc, n = b.run("%s ecc list 2>/dev/null | wc -l" % tc, timeout=90)
    b.record("ECC slots listed (read-only)", n.strip().split("\n")[-1] == "32", "lines=%s" % n.strip())
    code, out = run("rng get 16")
    rc, n = b.run("%s rng get 16 2>/dev/null | tr -d '\\n' | wc -c" % tc)
    b.record("16 random bytes", n.strip().split("\n")[-1] == "32", "chars=%s" % n.strip())
    code, out = run("mem read 4000")
    b.record("a slot number outside 0-511 is a usage error", code == 2, out.replace("\n", " | "))

    b.run("mkdir -p /etc/tropic01; chmod 755 /etc/tropic01; printf short > /etc/tropic01/pairing.key; printf short > /etc/tropic01/pairing.pub; echo 1 > /etc/tropic01/pairing.slot")
    code, out = run("mem read 1 --out /tmp/x")
    b.record("unusable key files: refuses (exit 4), no silent fallback to the shared key",
             code == 4 and "unusable" in out and "shared engineering-sample key" not in out.replace("not falling back to the shared key", ""), out.replace("\n", " | "))

    b.run("head -c 32 /dev/urandom > /etc/tropic01/pairing.key; head -c 32 /dev/urandom > /etc/tropic01/pairing.pub; echo 0 > /etc/tropic01/pairing.slot")
    code, out = run("mem read 1 --out /tmp/x")
    b.record("a wrong but well-formed per-unit key is used and rejected by the chip (exit 4)",
             code == 4 and "per-unit pairing key, slot 0" in out, out.replace("\n", " | "))

    b.run("rm -f /etc/tropic01/pairing.key /etc/tropic01/pairing.pub /etc/tropic01/pairing.slot /tmp/x /tmp/c1")
    code, out = run("mem read 1 --out /tmp/c1")
    b.record("after removing the key files the shared key works again (chip not locked out)", code == 0, out.replace("\n", " | "))
    b.run("rm -f /tmp/c1 /tmp/tc-new /tmp/tc.out")
    return 0


def cmd_cra06(b, host_ip, router_ip="192.168.9.1", installed=False):
    """cra-06 security logging on the board: real SSH and LuCI logins from this PC over the wired link."""
    import glob
    import io
    import subprocess
    import tarfile
    import tempfile
    import urllib.error
    import urllib.parse
    import urllib.request

    if not b.ensure_shell():
        b.record("could not reach a shell", False)
        return 1
    pwfile = PW_FILE
    pw = b.password()
    log = "/etc/tropic01/log/security.log"

    def sh(cmd, timeout=40):
        return b.run(cmd, timeout=timeout)[1]

    def in_log(pattern):
        return int((sh("grep -cE '%s' %s 2>/dev/null || echo 0" % (pattern, log)).split() or ["0"])[-1])

    def ssh(password_file=None, wrong=None):
        base = ["ssh", "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR",
                "-o", "PreferredAuthentications=password", "-o", "PubkeyAuthentication=no", "-o", "ConnectTimeout=8",
                "root@%s" % router_ip, "true"]
        if wrong is not None:
            cmd = ["sshpass", "-p", wrong] + base
        else:
            cmd = ["sshpass", "-f", password_file] + base
        return subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30).returncode

    def luci(password):
        data = urllib.parse.urlencode({"luci_username": "root", "luci_password": password}).encode()
        req = urllib.request.Request("http://%s/cgi-bin/luci/" % router_ip, data=data)
        class NoRedirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, *a, **k):
                return None
        try:
            urllib.request.build_opener(NoRedirect).open(req, timeout=15)
        except urllib.error.HTTPError:
            pass

    workdir = tempfile.mkdtemp(prefix="bench-")
    if installed:
        have = sh("test -x /usr/libexec/tropic01-log-recorder && test -x /usr/libexec/tropic01-config-audit && echo yes").strip()
        b.record("security-log package is installed in the image", have == "yes")
        sh("rm -rf /etc/tropic01/log; uci set tropic01-security-log.main.interval=5; uci commit tropic01-security-log; "
           "/etc/init.d/tropic01-security-log restart; sleep 4", timeout=60)
    else:
        # ---- install the built package payload (no flash) -------------------------------------------
        roots = glob.glob(os.path.join(os.path.dirname(__file__), "..", "..", "build", "openwrt", "build_dir",
                                       "target-aarch64_cortex-a53_musl", "tropic01-security-log-0.1.0", "ipkg-*", "tropic01-security-log"))
        if not roots:
            b.record("built package payload found", False, "run: make package PKG=tropic01-security-log")
            return 1
        def own(ti):
            ti.uid = ti.gid = 0
            ti.uname = ti.gname = "root"
            return ti
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:gz") as tf:
            for d in ("etc", "usr"):
                tf.add(os.path.join(roots[0], d), arcname=d, filter=own)
        tmp = os.path.join(tempfile.mkdtemp(prefix="bench-"), "seclog.tgz")
        open(tmp, "wb").write(buf.getvalue())
        ok = push_file(b, tmp, "/tmp/seclog.tgz", host_ip)
        b.record("pushed the package payload", ok)
        if not ok:
            return 1
        sh("tar xzf /tmp/seclog.tgz -C / && rm -f /tmp/seclog.tgz; rm -rf /etc/tropic01/log; "
           "uci set tropic01-security-log.main.interval=5; uci commit tropic01-security-log; "
           "/etc/init.d/tropic01-security-log restart; sleep 4", timeout=60)


    out = sh("tropic01-security-log status")
    b.record("status: recording on and running", "recording: on" in out and "running: yes" in out, out.replace("\n", " | "))
    b.record("log directory is private (0700)", sh("stat -c %a /etc/tropic01/log 2>/dev/null || ls -ld /etc/tropic01/log | cut -c1-10").strip().startswith(("700", "drwx------")))
    b.record("start-up and baseline are recorded", in_log("security log started") >= 1 and in_log("config baseline recorded") >= 1)

    # ---- real logins from this PC ----------------------------------------------------------------
    rc = ssh(wrong="definitely-not-the-password")
    time.sleep(2)
    b.record("failed SSH login is recorded", rc != 0 and in_log("dropbear.*([Bb]ad password|[Ll]ogin attempt|failed)") >= 1)
    rc = ssh(password_file=pwfile)
    time.sleep(2)
    b.record("successful SSH login is recorded", rc == 0 and in_log("dropbear.*(succeeded|Password auth)") >= 1)
    luci("definitely-not-the-password")
    time.sleep(2)
    b.record("failed LuCI login is recorded", in_log("luci.*failed login") >= 1)
    luci(pw)
    time.sleep(2)
    b.record("successful LuCI login is recorded", in_log("luci.*accepted login") >= 1)

    # ---- config and service changes ----------------------------------------------------------------
    sh("uci set system.@system[0].log_size=129 && uci commit system; sleep 9")
    b.record("a config change is recorded (file name only)", in_log("config changed /etc/config/system") >= 1)
    sh("uci set system.@system[0].log_size=128 && uci commit system")
    was = sh("/etc/init.d/tropic01-aws enabled && echo on || echo off").split()[-1]
    sh("/etc/init.d/tropic01-aws %s; sleep 9" % ("disable" if was == "on" else "enable"))
    b.record("a service enablement change is recorded", in_log("service (enabled|disabled) tropic01-aws") >= 1)
    sh("/etc/init.d/tropic01-aws %s" % ("enable" if was == "on" else "disable"))

    # ---- size cap --------------------------------------------------------------------------------------
    sh("uci set tropic01-security-log.main.max_kb=2; uci commit tropic01-security-log; /etc/init.d/tropic01-security-log restart; sleep 3", timeout=60)
    sh("i=0; while [ $i -lt 300 ]; do logger -p authpriv.notice -t tropic01-audit \"cap test line $i with some padding to take up space\"; i=$((i+1)); done; sleep 4", timeout=90)
    size = int((sh("wc -c < %s" % log).split() or ["0"])[-1])
    b.record("log stays near its size cap (2 KiB cap, 300 lines written)", size < 8192, "size=%d bytes" % size)
    sh("uci set tropic01-security-log.main.max_kb=256; uci commit tropic01-security-log; /etc/init.d/tropic01-security-log restart; sleep 3", timeout=60)

    # ---- nothing secret in the log (checked on this PC, nothing printed) ----------------------------
    sh("logger -p authpriv.notice -t tropic01-audit 'leak check marker'; sleep 2")
    # stdin stays an open pipe: with EOF on stdin, `nc -l -q` would quit before the board connects.
    srv = subprocess.Popen(["nc", "-l", "-p", "9100"], stdout=open(os.path.join(workdir, "log.copy"), "wb"),
                           stdin=subprocess.PIPE, stderr=subprocess.DEVNULL)
    time.sleep(1)
    sh("nc %s 9100 < %s" % (host_ip, log), timeout=30)
    try:
        srv.wait(timeout=10)
    except subprocess.TimeoutExpired:
        srv.kill()
        srv.wait()
    copy = open(os.path.join(workdir, "log.copy"), "rb").read()
    b.record("the log has content to check", len(copy) > 200, "%d bytes" % len(copy))
    b.record("log contains neither the real nor the wrong password",
             pw.encode() not in copy and b"definitely-not-the-password" not in copy)

    # ---- opt-out -----------------------------------------------------------------------------------------
    out = sh("tropic01-security-log disable; sleep 2; tropic01-security-log status; pgrep -f 'logread -f -e' >/dev/null && echo RECORDER_UP || echo RECORDER_DOWN")
    b.record("opt-out: disable stops recording and monitoring", "OFF" in out and "RECORDER_DOWN" in out, out.replace("\n", " | "))
    sh("logger -p authpriv.notice -t tropic01-audit 'written-while-opted-out'; sleep 2")
    b.record("opt-out: nothing new is recorded while off", in_log("written-while-opted-out") == 0)
    b.record("opt-out: stored lines are kept until cleared", int((sh("wc -l < %s" % log).split() or ["0"])[-1]) > 0)
    sh("tropic01-security-log enable; sleep 3", timeout=30)
    out = sh("tropic01-security-log status")
    b.record("opt-in again works", "recording: on" in out and "running: yes" in out, out.replace("\n", " | "))
    sh("tropic01-security-log clear")
    out = sh("ls %s %s 2>&1 | head -3" % (log, "/etc/tropic01/log/baseline.sha256"))
    b.record("clear deletes the stored log and baseline", "No such file" in out, out.replace("\n", " | "))
    return 0


def ensure_lan(b, want="192.168.9.1/24"):
    """Put the board's LAN on the wired bench range (a clean flash resets it to 192.168.1.1/24)."""
    rc, out = b.run("uci -q get network.lan.ipaddr")
    if out.strip() != want:
        b.run("uci set network.lan.ipaddr=%s && uci commit network && /etc/init.d/network reload; sleep 8" % want, timeout=60)


def cmd_cra05(b, phase):
    """cra-05 vault. phase 'stack': the whole encryption stack on the board with a throwaway
    RAM image and a random key, no TROPIC01 access at all."""
    if not b.ensure_shell():
        b.record("could not reach a shell", False)
        return 1
    ensure_lan(b)
    if phase != "stack":
        print("unknown phase")
        return 2
    sh = lambda cmd, t=60: b.run(cmd, timeout=t)
    sh("umount /tmp/tvm 2>/dev/null; cryptsetup close tvtest 2>/dev/null; rm -rf /tmp/t.img /tmp/t.key /tmp/w.key /tmp/tvm; mkdir -p /tmp/tvm")
    sh("dd if=/dev/zero of=/tmp/t.img bs=1M count=8 2>/dev/null; head -c 32 /dev/urandom > /tmp/t.key; head -c 32 /dev/urandom > /tmp/w.key")
    rc, out = sh("cryptsetup luksFormat --type luks2 --pbkdf pbkdf2 --pbkdf-force-iterations 1000 --luks2-metadata-size 16k "
                 "--luks2-keyslots-size 256k --offset 2048 --batch-mode --key-file /tmp/t.key /tmp/t.img 2>&1; echo fmt_rc=$?")
    b.record("luksFormat with the vault's parameters", "fmt_rc=0" in out, out.replace("\n", " | ")[-160:])
    rc, out = sh("cryptsetup open --key-file /tmp/t.key /tmp/t.img tvtest 2>&1; echo open_rc=$?; ls /dev/mapper/tvtest 2>&1")
    b.record("open (loop device + dm-crypt) works", "open_rc=0" in out and "/dev/mapper/tvtest" in out, out.replace("\n", " | ")[-160:])
    rc, out = sh("mke2fs -q -t ext4 /dev/mapper/tvtest 2>&1; echo mkfs_rc=$?; mount /dev/mapper/tvtest /tmp/tvm 2>&1; echo mount_rc=$?; df -k /tmp/tvm | tail -1 | awk '{print $2\" KiB\"}'")
    b.record("ext4 inside the mapping mounts", "mkfs_rc=0" in out and "mount_rc=0" in out, out.replace("\n", " | ")[-160:])
    sh("echo SECRET-MARKER-12345 > /tmp/tvm/hello; sync; umount /tmp/tvm; cryptsetup close tvtest")
    rc, out = sh("grep -c SECRET-MARKER-12345 /tmp/t.img; ls /dev/mapper/tvtest 2>&1 | grep -c 'No such'")
    n = out.split()
    b.record("plaintext marker is NOT visible in the image file; mapping is gone", n[:1] == ["0"] and n[-1:] == ["1"], out.replace("\n", " "))
    rc, out = sh("cryptsetup open --key-file /tmp/t.key /tmp/t.img tvtest && mount /dev/mapper/tvtest /tmp/tvm && cat /tmp/tvm/hello; umount /tmp/tvm; cryptsetup close tvtest")
    b.record("reopening with the same key returns the data", "SECRET-MARKER-12345" in out, "")
    rc, out = sh("cryptsetup open --test-passphrase --key-file /tmp/w.key /tmp/t.img 2>&1; echo wrong_rc=$?")
    b.record("a wrong key is refused", "wrong_rc=0" not in out, out.replace("\n", " | ")[-120:])
    sh("umount /tmp/tvm 2>/dev/null; cryptsetup close tvtest 2>/dev/null; rm -rf /tmp/t.img /tmp/t.key /tmp/w.key /tmp/tvm")
    return 0


def cmd_suite(b, host_ip):
    """Everything that needs no irreversible chip step, against the packages INSTALLED in the image.

    Not included on purpose: the vault's init (writes R-MEM slot 100) and destroy (erases it), and any
    pairing-slot write. Those are run by hand with an explicit go-ahead.
    cra01 goes last: it plants an empty-password hash and reboots.
    """
    steps = [("web", lambda: cmd_web(b)),
             ("cra04 (tropicctl installed in the image)", lambda: cmd_cra04(b, "installed", host_ip)),
             ("cra05 stack (no chip access)", lambda: cmd_cra05(b, "stack")),
             ("cra06 (package installed in the image)", lambda: cmd_cra06(b, host_ip, installed=True)),
             ("cra01 (plants an empty hash, reboots, checks the guard)", lambda: cmd_cra01(b))]
    worst = 0
    for name, fn in steps:
        print("\n===== %s =====" % name, flush=True)
        before = len(b.results)
        try:
            rc = fn() or 0
        except (pexpect.TIMEOUT, pexpect.EOF) as e:
            b.record(name + " (harness)", False, "%s; last output: %r" % (type(e).__name__, (b.c.before or "")[-160:]))
            rc = 1
        worst = max(worst, rc)
        part = b.results[before:]
        print("----- %s: %d checks, %d failed" % (name, len(part), sum(1 for r in part if not r[1])), flush=True)
    return worst


def cmd_cra01(b, settle=0, replant=True):
    """Finish the cra-01 console checks from wherever the board is.

    Expects the board to have just booted with an empty-string root hash
    planted (see README), so the boot-time guard (S99) is what blanks it.
    """
    pw = b.password()
    if replant:
        if not b.ensure_shell():
            b.record("could not reach a shell to prepare the test", False)
            return 1
        rc, out = b.run("sed -i 's|^root:[^:]*:|root:" + EMPTY_HASH + ":|' /etc/shadow; "
                        "/usr/libexec/tropic01-console-guard state")
        b.record("planted an empty-password hash", out.strip() == "empty", out.strip())
        b.say("reboot")
        # The console comes back with a login prompt well before S99 has run.
        b.c.expect([r"Please press Enter to activate this console", P_LOGIN], timeout=180)
        settle = max(settle, 70)
    if settle:
        # Let the board finish booting (S99 runs late) with nothing typed.
        print("waiting %ds for the board to boot; nothing is sent" % settle, flush=True)
        b.c.expect(pexpect.TIMEOUT, timeout=settle)
    state = b.sync(timeout=15)
    b.record("console reachable", state != "unknown", "state=%s" % state)
    if state == "unknown":
        return 1

    # Reach the forced-password prompt, noting whether the login hook had to
    # clear an empty password itself (S99 had not run) or found it blank.
    hook_cleared = False
    for _ in range(6):
        if state == "login":
            state = b.login(None)
        elif state == "login_pw":
            b.secret("")
            i = b.c.expect([P_NEW, P_SHELL, r"Login incorrect"], timeout=25)
            state = ["new_pw", "shell", "bad_login"][i]
        elif state == "shell":
            break
        elif state == "new_pw":
            break
        elif state in ("retype", "bad_login", "unknown"):
            state = b.sync()
        if "clearing it so a real one can be set" in (b.c.before or ""):
            hook_cleared = True
        if state == "bad_login":
            b.record("login as root with empty password", False, "rejected")
            return 1
    b.record("reached forced-password prompt", state == "new_pw", "state=%s" % state)
    if state != "new_pw":
        return 1
    b.record("boot-time guard cleared it before login (not the login hook)", not hook_cleared,
             "login hook had to clear it" if hook_cleared else "")

    # Five empty attempts must each be refused, then the session closes.
    refused = 0
    closed = False
    for n in range(5):
        b.say("")  # New password: (empty)
        i = b.c.expect([r"Empty password not allowed", P_RETYPE, r"closing this session", P_NEW], timeout=15)
        if i == 0:
            refused += 1
        elif i == 1:  # patch missing: it asked to retype, so empty was accepted so far
            b.say("")
            break
        elif i == 2:
            closed = True
            break
        # The session ends and the console respawns. The hook's own last line can
        # be cut off by the hangup, so the respawn is the reliable signal.
        j = b.c.expect([r"closing this session", r"Please press Enter to activate this console", P_LOGIN, P_NEW], timeout=15)
        if j in (0, 1, 2):
            closed = True
            break
    b.record("empty password refused on every attempt", refused == 5, "refused=%d" % refused)
    b.record("session closes after five failed attempts", closed)

    # Console respawns; log in again and set the bench password for real.
    state = b.sync(timeout=15)
    if state == "login":
        state = b.login(None)
    b.record("forced prompt again after the session closed", state == "new_pw", "state=%s" % state)
    if state != "new_pw":
        return 1
    b.secret(pw)
    b.c.expect(P_RETYPE, timeout=10)
    b.secret(pw)
    i = b.c.expect([r"The serial console now asks for the root password at login", P_SHELL], timeout=40)
    if i == 0:
        b.c.expect(P_SHELL, timeout=15)
    b.record("real password accepted, reached a shell", True)

    # Evidence from the board itself.
    rc, out = b.run("/usr/libexec/tropic01-console-guard state; uci -q get system.@system[0].ttylogin; "
                    "uci -q get dropbear.@dropbear[0].BannerFile; grep -c 'CRA Annex' /etc/banner")
    lines = out.split("\n")
    b.record("guard state is 'set'", lines[0:1] == ["set"], out.replace("\n", " | "))
    b.record("ttylogin is 1", lines[1:2] == ["1"])
    b.record("blank-password warnings removed (BannerFile unset, MOTD clean)",
             len(lines) >= 3 and lines[-1] == "0" and (len(lines) == 3))
    rc, out = b.run("logread | grep -c 'root accepts an empty password'")
    b.record("boot-time guard logged clearing the empty password", out.strip() not in ("", "0"),
             "log lines=%s" % out.strip())

    # ttylogin repair: knock it back to 0 and let the guard put it right.
    rc, out = b.run("uci set system.@system[0].ttylogin=0 && uci commit system && "
                    "/etc/init.d/tropic01-console-guard start; uci -q get system.@system[0].ttylogin")
    b.record("guard restores ttylogin=1", out.strip().endswith("1") and "now asks for the root password" in out, out.replace("\n", " | "))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["probe", "cra01", "capture", "web", "cra04", "cra05", "cra06", "suite"])
    ap.add_argument("--settle", type=int, default=0,
                    help="capture: seconds to record (default 180). cra01: seconds to wait, sending nothing, before the first newline (use ~75 right after power-up)")
    ap.add_argument("--no-replant", action="store_true",
                    help="cra01: do not log in, plant the empty hash and reboot first; start from the current console state")
    ap.add_argument("--binary", default=os.path.join(os.path.dirname(__file__), "..", "..", "build", "openwrt", "build_dir",
                    "target-aarch64_cortex-a53_musl", "tropicctl-0.1.0", "tropicctl"), help="cra04: tropicctl to push to /tmp (use --binary installed for the one in the image)")
    ap.add_argument("--host-ip", default="192.168.9.2", help="cra04: this PC's address on the wired bench link")
    ap.add_argument("--installed", action="store_true", help="cra06: the package is already in the image; do not push a payload")
    ap.add_argument("--phase", default="stack", help="cra05: stack (no chip access)")
    args = ap.parse_args()
    b = Bench()
    try:
        rc = (cmd_probe(b) if args.command == "probe" else
              cmd_capture(b, args.settle or 180) if args.command == "capture" else
              cmd_web(b) if args.command == "web" else
              cmd_cra04(b, args.binary, args.host_ip) if args.command == "cra04" else
              cmd_cra06(b, args.host_ip, installed=args.installed) if args.command == "cra06" else
              cmd_cra05(b, args.phase) if args.command == "cra05" else
              cmd_suite(b, args.host_ip) if args.command == "suite" else cmd_cra01(b, args.settle, not args.no_replant))
    except (pexpect.TIMEOUT, pexpect.EOF) as e:
        b.record("harness", False, "%s while waiting; last output: %r" % (type(e).__name__, (b.c.before or "")[-200:]))
        rc = 1
    fails = [r for r in b.results if not r[1]]
    if b.results:
        print("%d checks, %d failed. transcript: %s" % (len(b.results), len(fails), b.log_path))
    return rc or (1 if fails else 0)


if __name__ == "__main__":
    sys.exit(main())
