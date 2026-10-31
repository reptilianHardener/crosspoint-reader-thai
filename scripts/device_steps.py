#!/usr/bin/env python3
"""
Drive a device over USB serial with scripted button clicks and screenshots.

Needs a development build (`pio run -e default`), which accepts CMD:KEY:<name>.
Each step is one of:

    KEY:<BACK|CONFIRM|LEFT|RIGHT|UP|DOWN|POWER>   click a physical button
    WAIT:<seconds>                                 keep reading serial output
    SHOT:<name>                                    save <out>/<name>.png
    MEM                                            print the latest heap line

Steps come from the command line or, with --file, one per line (# starts a
comment). After the run the heap lines ([MEM] Free/MaxAlloc) are printed as a
table, and the whole serial log is saved to <out>/serial.log.

Example, from the Home screen into the Library and back:

    python3 scripts/device_steps.py SHOT:home KEY:DOWN KEY:RIGHT KEY:CONFIRM \\
        WAIT:3 SHOT:library KEY:BACK WAIT:12 MEM

Heap lines are printed every 10 seconds, so wait at least that long before MEM.

Golden pages: with --golden <dir>, each SHOT is also compared pixel for pixel
with <dir>/<name>.png. A difference is reported with its pixel count and saved
as <out>/<name>.diff.png (changed pixels in black), and the run exits with
status 1. --record saves the screenshots of this run as the new goldens.
Capture the pages once on a known-good firmware, then rerun the same steps
file after a change:

    python3 scripts/device_steps.py --file pages.steps --golden device-steps/golden --record
    python3 scripts/device_steps.py --file pages.steps --golden device-steps/golden

Requires pyserial and Pillow (python3 -m pip install -r scripts/requirements.txt).
"""

from __future__ import annotations

import argparse
import glob
import platform
import shutil
import re
import sys
import time
from pathlib import Path

import serial
from PIL import Image, ImageChops

KEYS = {"BACK", "CONFIRM", "LEFT", "RIGHT", "UP", "DOWN", "POWER"}
MEM_LINE = re.compile(rb"\[(\d+)\] \[INF\] \[MEM\] Free: (\d+) bytes.*?Min Free: (\d+) bytes, MaxAlloc: (\d+)")


def detect_port() -> str | None:
    pattern = "/dev/cu.usbmodem*" if platform.system() == "Darwin" else "/dev/ttyACM*"
    ports = sorted(glob.glob(pattern))
    return ports[0] if ports else None


class Device:
    def __init__(self, port: str, log_path: Path) -> None:
        self.ser = serial.Serial(port, 115200, timeout=0.2)
        self.log = log_path.open("ab")
        self.pending = b""

    def keep(self, data: bytes) -> None:
        """Logs serial output and keeps it for the heap table."""
        self.log.write(data)
        self.pending += data

    def read_for(self, seconds: float) -> bytes:
        data = b""
        end = time.time() + seconds
        while time.time() < end:
            data += self.ser.read(4096)
        self.keep(data)
        return data

    def key(self, name: str, settle: float) -> None:
        for _ in range(20):
            self.ser.write(f"CMD:KEY:{name}\n".encode())
            reply = self.wait_for_reply(name)
            if reply == "KEY_OK":
                self.read_for(settle)
                return
            if reply == "KEY_UNKNOWN":
                sys.exit(f"device does not know key {name}")
            if reply is None:
                sys.exit("no KEY reply: is this a development build (pio run -e default)?")
            time.sleep(0.3)  # KEY_BUSY: the previous click is not finished
        sys.exit(f"key {name} stayed busy")

    def wait_for_reply(self, name: str) -> str | None:
        end = time.time() + 3
        buffer = b""
        while time.time() < end:
            buffer += self.read_for(0.2)
            match = re.search(rb"(KEY_OK|KEY_BUSY|KEY_UNKNOWN) " + re.escape(name.encode()), buffer)
            if match:
                return match.group(1).decode()
        return None

    def screenshot(self, path: Path, width: int, height: int, portrait: bool) -> None:
        self.ser.write(b"CMD:SCREENSHOT\n")
        buffer = b""
        end = time.time() + 15
        while b"SCREENSHOT_END" not in buffer and time.time() < end:
            buffer += self.ser.read(8192)
        start = buffer.find(b"SCREENSHOT_START:")
        if start < 0:
            self.keep(buffer)
            sys.exit("no screenshot received")
        newline = buffer.index(b"\n", start)
        size = int(buffer[start + len(b"SCREENSHOT_START:") : newline])
        pixels = buffer[newline + 1 : newline + 1 + size]
        after = buffer[newline + 1 + size :]
        self.keep(buffer[:start] + after[len(b"SCREENSHOT_END") :])
        if size != width * height // 8 or len(pixels) != size:
            sys.exit(f"screenshot is {size} bytes, expected {width}x{height} at 1 bit per pixel")
        # Log output from another task inside the image would shift it: refuse it.
        if not after.startswith(b"SCREENSHOT_END"):
            sys.exit("screenshot was interrupted by other output; wait longer before SHOT")
        image = Image.frombytes("1", (width, height), pixels)
        if portrait:  # upright, as the reader shows it; same turn as debugging_monitor.py
            image = image.transpose(Image.ROTATE_270)
        image.save(path)


def compare_golden(shot: Path, golden: Path, diff: Path) -> str | None:
    """Compares a screenshot with its golden; returns a problem description, or None when they match."""
    if not golden.exists():
        return f"no golden {golden}"
    with Image.open(shot) as actual, Image.open(golden) as expected:
        if actual.size != expected.size:
            return f"size {actual.size[0]}x{actual.size[1]} differs from golden {expected.size[0]}x{expected.size[1]}"
        changed = ImageChops.difference(actual.convert("1"), expected.convert("1"))
    count = sum(1 for pixel in changed.getdata() if pixel)
    if count == 0:
        diff.unlink(missing_ok=True)  # a leftover from an earlier failing run
        return None
    ImageChops.invert(changed).save(diff)  # black where the pixels differ
    return f"{count} pixels differ from {golden}; see {diff}"


def valid_step(step: str) -> bool:
    kind, _, value = step.partition(":")
    kind = kind.upper()
    return (
        (kind == "KEY" and value.upper() in KEYS)
        or (kind == "WAIT" and re.fullmatch(r"\d+(\.\d+)?", value) is not None)
        or (kind == "SHOT" and bool(value))
        or (kind == "MEM" and not value)
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("steps", nargs="*", help="steps to run, e.g. KEY:DOWN WAIT:2 SHOT:home")
    parser.add_argument("--file", type=Path, help="read steps from this file, one per line")
    parser.add_argument("--port", help="serial port (default: first USB modem port)")
    parser.add_argument("--out", type=Path, default=Path("device-steps"), help="folder for screenshots and the log")
    parser.add_argument("--settle", type=float, default=1.2, help="seconds to wait after each click (default 1.2)")
    parser.add_argument("--panel", default="800x480", help="native framebuffer size (default 800x480)")
    parser.add_argument("--landscape", action="store_true", help="save screenshots unrotated")
    parser.add_argument("--golden", type=Path, help="folder of golden screenshots to compare each SHOT with")
    parser.add_argument("--record", action="store_true", help="save this run's screenshots as the goldens")
    args = parser.parse_args()
    if args.record and not args.golden:
        parser.error("--record needs --golden")

    steps = list(args.steps)
    if args.file:
        for line in args.file.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                steps.extend(line.split())
    if not steps:
        parser.error("no steps given")

    for step in steps:
        if not valid_step(step):
            parser.error(f"unknown step: {step}")

    try:
        width, height = (int(v) for v in args.panel.lower().split("x"))
    except ValueError:
        parser.error("--panel must look like 800x480")

    port = args.port or detect_port()
    if not port:
        sys.exit("no device port found; pass --port")
    args.out.mkdir(parents=True, exist_ok=True)
    device = Device(port, args.out / "serial.log")
    device.read_for(0.5)
    if args.record:
        args.golden.mkdir(parents=True, exist_ok=True)
    problems: list[str] = []

    for step in steps:
        kind, _, value = step.partition(":")
        kind = kind.upper()
        if kind == "KEY":
            device.key(value.upper(), args.settle)
        elif kind == "WAIT":
            device.read_for(float(value))
        elif kind == "SHOT":
            shot = args.out / f"{value}.png"
            device.screenshot(shot, width, height, not args.landscape)
            print(f"saved {shot}")
            if args.record:
                shutil.copyfile(shot, args.golden / f"{value}.png")
                print(f"recorded {args.golden / value}.png")
            elif args.golden:
                problem = compare_golden(shot, args.golden / f"{value}.png", args.out / f"{value}.diff.png")
                if problem:
                    problems.append(f"{value}: {problem}")
                    print(f"DIFF {value}: {problem}")
                else:
                    print(f"same {value}")
        elif kind == "MEM":
            samples = MEM_LINE.findall(device.pending)
            print("MEM", "no sample yet" if not samples else "free=%s maxalloc=%s" % (samples[-1][1].decode(), samples[-1][3].decode()))

    samples = MEM_LINE.findall(device.pending)
    if samples:
        print(f"{'time ms':>9} {'free':>8} {'min free':>9} {'max alloc':>10}")
        for t, free, min_free, max_alloc in samples:
            print(f"{t.decode():>9} {free.decode():>8} {min_free.decode():>9} {max_alloc.decode():>10}")

    if problems:
        sys.exit(f"{len(problems)} screenshot(s) differ from the goldens")


if __name__ == "__main__":
    main()
