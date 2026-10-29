#!/usr/bin/env python3
"""
Non-interactive serial monitor for coding agents.

`idf.py monitor` is interactive (needs a TTY, never exits) — useless for an
agent. This tool resets the board, captures the log for a bounded time,
understands the firmware's @@ marker protocol, and returns an exit code:

    0  @@RESULT PASS seen (or --until pattern matched)
    1  @@RESULT FAIL, panic/abort/stack overflow/brownout, or reboot loop
    2  timeout — nothing conclusive within --timeout seconds
    3  serial port problem (not found, busy, permission denied)

Extras:
  --save-images DIR   rebuild frames sent with @@IMG_BEGIN/@@IMG/@@IMG_END
  --elf FILE          decode "Backtrace:" lines with xtensa addr2line
  --json FILE         machine-readable summary (checks, frames, stats, panic)
  --log FILE          full log, ANSI colours stripped, host timestamps

Only dependency: pyserial (bundled with ESP-IDF's Python env).
"""
from __future__ import annotations

import argparse
import base64
import fcntl
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zlib
from dataclasses import dataclass, field
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    print("esp_monitor: pyserial missing (pip install pyserial, or run inside the ESP-IDF env)", file=sys.stderr)
    sys.exit(3)

EXIT_PASS, EXIT_FAIL, EXIT_TIMEOUT, EXIT_PORT = 0, 1, 2, 3

ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
KV_RE = re.compile(r"(\w+)=(\S+)")
PANIC_PATTERNS = [
    re.compile(p)
    for p in (
        r"Guru Meditation Error",
        r"abort\(\) was called",
        r"\*\*\*ERROR\*\*\* A stack overflow",
        r"Stack smashing protect failure",
        r"Brownout detector was triggered",
        r"assert failed:",
        r"E \(\d+\) task_wdt: Task watchdog got triggered",
    )
]
BOOT_BANNER_RE = re.compile(r"^(ESP-ROM:|rst:0x)")
BACKTRACE_RE = re.compile(r"Backtrace:((?:\s*0x[0-9a-fA-F]+:0x[0-9a-fA-F]+)+)")

KNOWN_USB_IDS = {
    (0x303A, 0x1001): "Espressif USB-Serial-JTAG (native USB)",
    (0x1A86, 0x55D3): "WCH CH343 UART bridge",
    (0x1A86, 0x7523): "WCH CH340 UART bridge",
    (0x10C4, 0xEA60): "Silabs CP210x UART bridge",
}


# --------------------------------------------------------------------------- ports
def candidate_ports() -> list[tuple[str, str]]:
    found = []
    for p in list_ports.comports():
        key = (p.vid, p.pid)
        if key in KNOWN_USB_IDS:
            found.append((p.device, f"{KNOWN_USB_IDS[key]} sn={p.serial_number}"))
    return found


def resolve_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    env = os.environ.get("ESP_PORT")
    if env:
        return env
    for link in ("/dev/esp32cam-jtag", "/dev/esp32cam-uart"):
        if os.path.exists(link):
            return link
    cands = candidate_ports()
    native = [d for d, desc in cands if "USB-Serial-JTAG" in desc]
    if native:
        return native[0]
    if cands:
        return cands[0][0]
    raise FileNotFoundError("no ESP32 serial port found (set ESP_PORT or pass --port)")


class PortLock:
    """Advisory lock so two agents never fight over the same serial port."""

    def __init__(self, port: str, wait_s: float):
        name = Path(os.path.realpath(port)).name
        self.path = f"/tmp/esp32cam-{name}.lock"
        self.wait_s = wait_s
        self.fh = None

    def __enter__(self):
        if os.environ.get("ESP_PORT_LOCK_HELD") == "1":
            return self  # a parent script (scripts/test_hw.sh) already holds it
        self.fh = open(self.path, "w")
        deadline = time.monotonic() + self.wait_s
        while True:
            try:
                fcntl.flock(self.fh, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return self
            except BlockingIOError:
                if time.monotonic() > deadline:
                    raise TimeoutError(f"port busy (lock {self.path} held by another process)")
                time.sleep(0.2)

    def __exit__(self, *exc):
        if self.fh:
            fcntl.flock(self.fh, fcntl.LOCK_UN)
            self.fh.close()


def open_serial(port: str, baud: int) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.1
    # De-assert before open: on Linux pyserial would otherwise raise DTR+RTS,
    # which on USB-Serial-JTAG can hold the chip in reset / download mode.
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser


def hard_reset(ser: serial.Serial) -> None:
    """RTS pulse with DTR released: EN low -> high, IO0 high => normal boot.
    Works for both native USB-Serial-JTAG and the classic 2-transistor
    auto-reset circuit on CH34x/CP210x boards."""
    try:
        ser.dtr = False
        ser.rts = True
        time.sleep(0.12)
        ser.rts = False
        time.sleep(0.05)
    except OSError as e:
        print(f"esp_monitor: port has no modem control lines ({e}); reset skipped", file=sys.stderr)


# --------------------------------------------------------------------------- parsing
@dataclass
class ImageAssembly:
    name: str
    length: int
    crc32: int
    chunks: list[str] = field(default_factory=list)


@dataclass
class Session:
    result: str | None = None
    fail_reason: str | None = None
    boot: dict = field(default_factory=dict)
    checks: dict = field(default_factory=dict)
    frames: list = field(default_factory=list)
    stats: dict = field(default_factory=dict)
    heap: dict = field(default_factory=dict)
    images: list = field(default_factory=list)
    image_errors: list = field(default_factory=list)
    panic: str | None = None
    backtraces: list = field(default_factory=list)
    decoded_backtraces: list = field(default_factory=list)
    boot_count: int = 0
    custom_match: str | None = None


def kv(line: str) -> dict:
    return dict(KV_RE.findall(line))


class Parser:
    def __init__(self, s: Session, image_dir: Path | None, until: re.Pattern | None):
        self.s = s
        self.image_dir = image_dir
        self.until = until
        self.img: ImageAssembly | None = None

    def feed(self, line: str) -> None:
        s = self.s
        if BOOT_BANNER_RE.match(line):
            if line.startswith("ESP-ROM:"):
                s.boot_count += 1
            return
        if s.panic is None:
            for pat in PANIC_PATTERNS:
                if pat.search(line):
                    s.panic = line.strip()
                    break
        m = BACKTRACE_RE.search(line)
        if m:
            s.backtraces.append(m.group(1).strip())

        if self.until and s.custom_match is None and self.until.search(line):
            s.custom_match = line.strip()

        if not line.startswith("@@"):
            return
        tag, _, rest = line.partition(" ")
        if line.startswith("@@IMG:"):
            if self.img:
                self.img.chunks.append(line[len("@@IMG:"):].strip())
            return
        if tag == "@@BOOT":
            s.boot = kv(rest)
        elif tag == "@@CHECK":
            d = kv(rest)
            for k, v in d.items():
                if v in ("OK", "FAIL"):
                    s.checks[k] = {"status": v, **{x: y for x, y in d.items() if x != k}}
                    break
        elif tag == "@@FRAME":
            s.frames.append(kv(rest))
        elif tag == "@@STATS":
            s.stats = kv(rest)
        elif tag == "@@HEAP":
            s.heap = kv(rest)
        elif tag == "@@RESULT":
            word = rest.split()[0] if rest.split() else ""
            s.result = word
            s.fail_reason = kv(rest).get("reason")
        elif tag == "@@IMG_BEGIN":
            d = kv(rest)
            self.img = ImageAssembly(d.get("name", "image.bin"), int(d.get("len", "0")), int(d.get("crc32", "0"), 16))
        elif tag == "@@IMG_ABORT":
            s.image_errors.append(f"device aborted dump: {rest}")
            self.img = None
        elif tag == "@@IMG_END":
            self._finish_image()

    def _finish_image(self) -> None:
        img, self.img = self.img, None
        if img is None:
            return
        try:
            data = base64.b64decode("".join(img.chunks), validate=True)
        except Exception as e:  # noqa: BLE001
            self.s.image_errors.append(f"{img.name}: base64 decode error {e}")
            return
        if len(data) != img.length:
            self.s.image_errors.append(f"{img.name}: length {len(data)} != {img.length} (lost serial lines?)")
            return
        crc = zlib.crc32(data) & 0xFFFFFFFF
        if crc != img.crc32:
            self.s.image_errors.append(f"{img.name}: crc32 {crc:08x} != {img.crc32:08x}")
            return
        if self.image_dir:
            self.image_dir.mkdir(parents=True, exist_ok=True)
            safe = re.sub(r"[^A-Za-z0-9_.-]", "_", img.name)
            out = self.image_dir / safe
            out.write_bytes(data)
            self.s.images.append(str(out))


def decode_backtrace(bt: str, elf: Path) -> str | None:
    tool = shutil.which("xtensa-esp32s3-elf-addr2line")
    if not tool or not elf.exists():
        return None
    pcs = [pair.split(":")[0] for pair in bt.split()]
    try:
        out = subprocess.run([tool, "-pfiaC", "-e", str(elf), *pcs], capture_output=True, text=True, timeout=20)
        return out.stdout.strip()
    except Exception:  # noqa: BLE001
        return None


# --------------------------------------------------------------------------- main
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: $ESP_PORT, /dev/esp32cam-*, autodetect)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=30.0, help="seconds to wait for a verdict")
    ap.add_argument("--no-reset", action="store_true", help="attach without resetting the chip")
    ap.add_argument("--until", help="regex: stop with exit 0 when a log line matches (instead of @@RESULT)")
    ap.add_argument("--settle", type=float, default=0.5, help="extra seconds to read after the verdict")
    ap.add_argument("--max-boots", type=int, default=2, help="boot banners allowed before declaring a reboot loop")
    ap.add_argument("--log", type=Path, help="write the full stripped log here")
    ap.add_argument("--json", type=Path, help="write a JSON summary here")
    ap.add_argument("--save-images", type=Path, help="directory for frames dumped over serial")
    ap.add_argument("--elf", type=Path, default=Path("build/esp32s3_cam.elf"), help="ELF for backtrace decoding")
    ap.add_argument("--lock-wait", type=float, default=60.0, help="seconds to wait for the port lock")
    ap.add_argument("--quiet", action="store_true", help="do not echo device output (summary only)")
    ap.add_argument("--list", action="store_true", help="list detected ESP32 ports and exit")
    ap.add_argument("--resolve-port", action="store_true", help="print the port that would be used and exit")
    args = ap.parse_args()

    if args.resolve_port:
        try:
            port = resolve_port(args.port)
        except FileNotFoundError as e:
            print(f"esp_monitor: {e}", file=sys.stderr)
            return EXIT_PORT
        print(port)
        print(f"/tmp/esp32cam-{Path(os.path.realpath(port)).name}.lock")
        return EXIT_PASS

    if args.list:
        for dev, desc in candidate_ports():
            print(f"{dev}\t{desc}")
        for link in sorted(glob.glob("/dev/esp32cam-*")):
            print(f"{link}\t-> {os.path.realpath(link)}")
        return EXIT_PASS

    try:
        port = resolve_port(args.port)
    except FileNotFoundError as e:
        print(f"esp_monitor: {e}", file=sys.stderr)
        return EXIT_PORT

    session = Session()
    parser = Parser(session, args.save_images, re.compile(args.until) if args.until else None)
    logf = open(args.log, "w", encoding="utf-8") if args.log else None
    exit_code = EXIT_TIMEOUT
    stop_at: float | None = None

    try:
        with PortLock(port, args.lock_wait):
            try:
                ser = open_serial(port, args.baud)
            except (serial.SerialException, OSError) as e:
                print(f"esp_monitor: cannot open {port}: {e}", file=sys.stderr)
                return EXIT_PORT
            with ser:
                if not args.no_reset:
                    hard_reset(ser)
                start = time.monotonic()
                buf = b""
                while True:
                    now = time.monotonic()
                    if stop_at is not None and now >= stop_at:
                        break
                    if stop_at is None and now - start > args.timeout:
                        break
                    try:
                        chunk = ser.read(4096)
                    except serial.SerialException as e:
                        # Native USB re-enumerates on reset; give it a moment.
                        print(f"esp_monitor: read error ({e}), reopening", file=sys.stderr)
                        time.sleep(0.5)
                        try:
                            ser.close()
                            ser.open()
                        except Exception:  # noqa: BLE001
                            pass
                        continue
                    if not chunk:
                        continue
                    buf += chunk
                    while b"\n" in buf:
                        raw, buf = buf.split(b"\n", 1)
                        line = ANSI_RE.sub("", raw.decode("utf-8", errors="replace")).rstrip("\r")
                        is_img_payload = line.startswith("@@IMG:")
                        if not args.quiet and not is_img_payload:
                            print(line, flush=True)
                        if logf and not is_img_payload:
                            logf.write(f"[{now - start:8.3f}] {line}\n")
                        parser.feed(line)

                        if stop_at is None:
                            t = time.monotonic()
                            if session.panic:
                                # keep reading: register dump + backtrace follow the panic line
                                exit_code, stop_at = EXIT_FAIL, t + max(args.settle, 2.0)
                            elif session.boot_count > args.max_boots:
                                session.panic = f"reboot loop ({session.boot_count} boots)"
                                exit_code, stop_at = EXIT_FAIL, t + args.settle
                            elif args.until and session.custom_match:
                                exit_code, stop_at = EXIT_PASS, t + args.settle
                            elif not args.until and session.result in ("PASS", "FAIL"):
                                exit_code = EXIT_PASS if session.result == "PASS" else EXIT_FAIL
                                stop_at = t + args.settle
    except TimeoutError as e:
        print(f"esp_monitor: {e}", file=sys.stderr)
        return EXIT_PORT
    finally:
        if logf:
            logf.close()

    if exit_code == EXIT_PASS and session.image_errors:
        exit_code = EXIT_FAIL

    for bt in session.backtraces:
        dec = decode_backtrace(bt, args.elf)
        if dec:
            session.decoded_backtraces.append(dec)

    verdict = {EXIT_PASS: "PASS", EXIT_FAIL: "FAIL", EXIT_TIMEOUT: "TIMEOUT"}[exit_code]
    summary = {
        "verdict": verdict,
        "exit_code": exit_code,
        "port": port,
        "result": session.result,
        "fail_reason": session.fail_reason,
        "panic": session.panic,
        "boot": session.boot,
        "checks": session.checks,
        "frames": session.frames,
        "stats": session.stats,
        "heap": session.heap,
        "images": session.images,
        "image_errors": session.image_errors,
        "backtraces": session.backtraces,
        "decoded_backtraces": session.decoded_backtraces,
        "boot_count": session.boot_count,
        "matched": session.custom_match,
    }
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(summary, indent=2))

    print("\n== esp_monitor summary ==", file=sys.stderr)
    print(f"verdict: {verdict} (exit {exit_code})  port: {port}", file=sys.stderr)
    for name, c in session.checks.items():
        print(f"  check {name}: {c['status']}", file=sys.stderr)
    if session.fail_reason:
        print(f"  fail_reason: {session.fail_reason}", file=sys.stderr)
    if session.panic:
        print(f"  panic: {session.panic}", file=sys.stderr)
    for dec in session.decoded_backtraces:
        print("  backtrace:\n    " + dec.replace("\n", "\n    "), file=sys.stderr)
    for img in session.images:
        print(f"  image saved: {img}", file=sys.stderr)
    for err in session.image_errors:
        print(f"  image error: {err}", file=sys.stderr)
    if exit_code == EXIT_TIMEOUT:
        print("  hint: no @@RESULT — wrong port? firmware not flashed? board stuck in download mode (press RST)?", file=sys.stderr)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
