#!/usr/bin/env python3
"""Screenshots and remote input for the Dial, over its serial console."""

import argparse
import base64
import glob
import os
import struct
import sys
import time
import zlib

import serial


def default_port():
    if os.environ.get("DIAL_PORT"):
        return os.environ["DIAL_PORT"]
    ports = sorted(glob.glob("/dev/serial/by-id/usb-Espressif_*"))
    return ports[0] if ports else "/dev/ttyACM0"


def open_port(port):
    # DTR and RTS stay as the kernel raises them on open: changing either one
    # makes the USB-JTAG bridge reset the chip
    return serial.Serial(port, 115200, timeout=0.1, write_timeout=2)


def lines(s, seconds):
    deadline = time.monotonic() + seconds
    buf = b""
    while time.monotonic() < deadline:
        buf += s.read(65536)
        *complete, buf = buf.split(b"\n")
        for line in complete:
            yield line.rstrip(b"\r")


def send(s, command):
    try:
        s.write(command.encode() + b"\n")
        s.flush()
    except serial.SerialTimeoutException:
        sys.exit("the Dial is asleep; turn the knob and try again")


def rgb565(px):
    out = bytearray(len(px) // 2 * 3)
    for i in range(len(px) // 2):
        v = px[2 * i] | px[2 * i + 1] << 8
        out[3 * i] = (v >> 11 & 0x1F) * 255 // 31
        out[3 * i + 1] = (v >> 5 & 0x3F) * 255 // 63
        out[3 * i + 2] = (v & 0x1F) * 255 // 31
    return out


def write_png(path, w, h, rgb):
    cx, cy, r2 = (w - 1) / 2, (h - 1) / 2, (w / 2) ** 2
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            o = (y * w + x) * 3
            raw += rgb[o : o + 3]
            raw.append(255 if (x - cx) ** 2 + (y - cy) ** 2 <= r2 else 0)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def shot(s, path):
    s.reset_input_buffer()
    send(s, "shot")
    img, w, h = None, 0, 0
    for line in lines(s, 15):
        if line.startswith(b"SHOT END"):
            write_png(path, w, h, img)
            print(path)
            return
        if line.startswith(b"SHOT "):
            w, h = map(int, line.split()[1:3])
            img = bytearray(w * h * 3)
        elif line.startswith(b"A ") and img is not None:
            _, x, y, aw, ah, data = line.split(b" ", 5)
            x, y, aw, ah = map(int, (x, y, aw, ah))
            px = rgb565(base64.b64decode(data))
            for row in range(ah):
                o = ((y + row) * w + x) * 3
                img[o : o + aw * 3] = px[row * aw * 3 : (row + 1) * aw * 3]
    sys.exit("no screenshot received")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=default_port())
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("shot", help="save the screen as a PNG")
    p.add_argument("path", nargs="?", default="shot.png")
    p = sub.add_parser("send", help="run console commands: rot N, click, long, tap X Y, hold X Y, state, ...")
    p.add_argument("commands", nargs="+")
    p.add_argument("--delay", type=float, default=0.4, help="seconds between commands")
    p.add_argument("--shot", help="save a screenshot afterwards")
    p = sub.add_parser("log", help="print the console")
    p.add_argument("--seconds", type=float, default=1e9)
    args = parser.parse_args()

    s = open_port(args.port)
    if args.cmd == "shot":
        shot(s, args.path)
    elif args.cmd == "send":
        for command in args.commands:
            send(s, command)
            for line in lines(s, args.delay):
                print(line.decode(errors="replace"))
        if args.shot:
            shot(s, args.shot)
    else:
        try:
            for line in lines(s, args.seconds):
                print(line.decode(errors="replace"), flush=True)
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
