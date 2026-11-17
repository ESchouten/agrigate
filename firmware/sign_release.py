"""Signs a release with the release key: a firmware build for the devices' own updates (prints
its block for update.ini), or a desktop app's update feed.

    python3 firmware/sign_release.py out/scraper-control-m5stack-core-1.6.412.bin --key key.pem \\
        --version 1.6.412 --build 412 >> out/update.ini
    python3 firmware/sign_release.py --message out/update.ini     # prints what the signatures cover
    python3 firmware/sign_release.py --feed releases.win.json --key key.pem > releases.win.signed.json

update.ini holds one block per board ([m5stack-core], ...) and is the only file with a fixed
name; each block names its firmware file.

The key is the ECDSA P-256 release key (CI secret FIRMWARE_SIGNING_KEY); the devices have the
public half in src/ReleaseKey.h (and the desktop apps in DesktopUpdates.RELEASE_KEY). Same
format as src/Manifest.h.
"""
import argparse
import base64
import hashlib
import json
import os
import subprocess
import sys
import tempfile

BOARD = "m5stack-core"
FEED_PREFIX = "scraper-control desktop feed v1\n"   # as DesktopUpdates.PREFIX
FIELDS = ("version", "build", "size", "sha256")


def message(m):
    return f"scraper-knop|{m['board']}|{m['version']}|{m['build']}|{m['size']}|{m['sha256']}"


def parse(text):
    """{board: fields} of update.ini."""
    blocks, cur = {}, None
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            cur = blocks.setdefault(line[1:-1], {"board": line[1:-1]})
        elif "=" in line and not line.startswith("#"):
            k, v = line.split("=", 1)
            if cur is not None:
                cur[k] = v
    return blocks


def sign(text, key):
    with tempfile.TemporaryDirectory() as tmp:
        if "BEGIN" in key:
            path = os.path.join(tmp, "key.pem")
            with open(path, "w") as f:
                f.write(key)
            key = path
        msg = os.path.join(tmp, "msg")
        with open(msg, "wb") as f:   # bytes: the same message on every system
            f.write(text.encode())
        return subprocess.check_output(["openssl", "dgst", "-sha256", "-sign", key, msg]).hex()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bin", nargs="?")
    ap.add_argument("--key", help="PEM file, or the PEM text itself")
    ap.add_argument("--version")
    ap.add_argument("--build", type=int)
    ap.add_argument("--board", default=BOARD)
    ap.add_argument("--message", help="print the signed message of every board in this manifest")
    ap.add_argument("--feed", help="a desktop app's update feed (Velopack releases.<channel>.json): print it signed")
    a = ap.parse_args()
    if a.feed:   # the desktop apps check this (native/shared/.../app/DesktopUpdates.kt)
        feed = open(a.feed, "rb").read()
        print(json.dumps({"feed": base64.b64encode(feed).decode(), "sig": sign(FEED_PREFIX + feed.decode(), a.key)}))
        return
    if a.message:
        for m in parse(open(a.message).read()).values():
            print(message(m))
        return
    data = open(a.bin, "rb").read()
    m = {"board": a.board, "version": a.version, "build": a.build, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    sig = sign(message(m), a.key)
    print(f"[{a.board}]")
    print(f"file={os.path.basename(a.bin)}")
    for k in FIELDS:
        print(f"{k}={m[k]}")
    print(f"sig={sig}")


if __name__ == "__main__":
    sys.exit(main())
