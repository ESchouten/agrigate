"""The buttons' firmware on a computer: PIN and text editors, the signed-update manifest, the web API, a
syntax check of main.cpp against stand-in M5/Arduino headers, and every screen drawn once.
Run: python3 tests/test_firmware.py"""
import os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "firmware", "test_host"))
from driver import host_flags  # noqa: E402
import lvgl_host  # noqa: E402

BIN = os.path.join(tempfile.gettempdir(), "clean_fw_test")


def check(name, cond, err):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        print(err); sys.exit(1)


def editors_and_manifest():
    exe = BIN + "_pin"
    subprocess.check_call(["g++", "-std=c++17", "-Wall",
                           os.path.join(ROOT, "firmware", "test_host", "pin_editor_test.cpp"), "-o", exe])
    subprocess.check_call([exe])
    exe = BIN + "_manifest"
    subprocess.check_call(["g++", "-std=c++17", "-Wall",
                           os.path.join(ROOT, "firmware", "test_host", "manifest_test.cpp"), "-o", exe])
    subprocess.check_call([exe])
    # the M5 and the release signer agree on what is signed, for every board in update.ini
    fw = os.path.join(ROOT, "firmware")
    sign = os.path.join(fw, "sign_release.py")
    with tempfile.TemporaryDirectory() as tmp:
        key, man = os.path.join(tmp, "k.pem"), os.path.join(tmp, "update.ini")
        subprocess.check_call(["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", key])
        text = "# test\n"
        for board in ("m5stack-core", "other-board"):
            binf = os.path.join(tmp, f"agrigate-clean-{board}-0.0.1.bin")
            with open(binf, "wb") as f:
                f.write(os.urandom(3000))
            text += subprocess.check_output([sys.executable, sign, binf, "--key", key, "--board", board,
                                             "--version", "0.0.1", "--build", "7"], text=True)
        with open(man, "w") as f:
            f.write(text)
        check("manifest: names its file", "file=agrigate-clean-m5stack-core-0.0.1.bin" in text, text)
        py = subprocess.check_output([sys.executable, sign, "--message", man], text=True)
        cc = "".join(subprocess.run([exe, b], input=text, capture_output=True, text=True, check=True).stdout
                     for b in ("m5stack-core", "other-board"))
        check("manifest: signer and M5 sign the same text", py == cc, py + cc)
    exe = BIN + "_api"
    subprocess.check_call(["g++", "-std=c++17", "-Wall",
                           os.path.join(ROOT, "firmware", "test_host", "api_test.cpp"), "-o", exe])
    subprocess.check_call([exe])
    exe = BIN + "_log"
    subprocess.check_call(["g++", "-std=c++17", "-Wall",
                           os.path.join(ROOT, "firmware", "test_host", "log_test.cpp"), "-o", exe])
    subprocess.check_call([exe])
    exe = BIN + "_text"
    subprocess.check_call(["g++", "-std=c++17", "-Wall",
                           os.path.join(ROOT, "firmware", "test_host", "text_editor_test.cpp"), "-o", exe])
    subprocess.check_call([exe])


def firmware_syntax():
    """main.cpp against stub M5/Arduino headers: catches C++ mistakes, not hardware ones."""
    fw = os.path.join(ROOT, "firmware")
    subprocess.check_call(["g++", "-std=c++17", "-Wall", "-Werror=return-type", "-fsyntax-only", "-I", os.path.join(fw, "test_host", "stubs"),
                           *host_flags(), *lvgl_host.flags(), os.path.join(fw, "src", "main.cpp")])
    print("ok   firmware main.cpp compiles (with stubs)")


def screens():
    """Every screen drawn with the firmware's own LVGL code: the pictures in the docs come from here."""
    render = os.path.join(ROOT, "firmware", "test_host", "preview", "render.py")
    with tempfile.TemporaryDirectory() as tmp:
        out = subprocess.check_output([sys.executable, render, tmp], text=True)
    check("screens: all drawn, in both languages", "nl-1-home.png" in out and "en-14-wps.png" in out, out)


if __name__ == "__main__":
    editors_and_manifest()
    firmware_syntax()
    screens()
    print("all firmware tests passed")
