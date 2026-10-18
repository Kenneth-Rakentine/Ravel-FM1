#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build Ravel for the FM-1 on M-VAVE's FM1 Open SDK, on Linux or macOS.

The same steps as the SDK's own sdk/Build.ps1 (Windows): compile with JieLi's PI32v2 clang,
link against the SDK's binary library with its linker script, check the image, and package
it (tools/fm1pack.py, a port of the SDK's Package.cs).

  JIELI_TOOLCHAIN=~/.jieli/toolchain python3 tools/build_fm1.py [--sdk build/fm1_open]

Outputs in build/fm1: ravel.elf, ravel.map, ravel-app.bin, Ravel-FM1.fwsc, report.json.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import fm1pack  # noqa: E402

RAVEL_VERSION = "0.2"
PRODUCT = "FM-1_8960002"              # Ravel's update identity: FM-1_8 + 96 (Ravel) + release 0002
USER_BYTES, UPDATE_FAMILY = 536576, 7

FLAGS = ["-target", "pi32v2", "-integrated-as", "-mcpu=r3", "-mfprev1", "-Wuninitialized",
         "-Wno-invalid-noreturn", "-fno-common", "-flto", "-fallow-pointer-null", "-fprefer-gnu-section",
         "-femulated-tls", "-Wno-shift-negative-value", "-Wframe-larger-than=2560", "-fms-extensions",
         "-fno-unwind-tables", "-ffunction-sections", "-fdata-sections", "-fmessage-length=0",
         "-cl-single-precision-constant", "-mllvm", "-pi32v2-large-program=true", "-D__GCC_PI32V2__", "-O2",
         "-fwrapv", "-fno-fast-math", "-ffp-contract=off"]
LINK_PLUGIN = ["-flto", "--plugin-opt=-pi32v2-always-use-itblock=false", "--plugin-opt=-enable-ipra=true",
               "--plugin-opt=-pi32v2-merge-max-offset=4096", "--plugin-opt=-pi32v2-enable-simd=true", "--plugin-opt=mcpu=r3",
               "--plugin-opt=-global-merge-on-const", "--plugin-opt=-inline-threshold=5", "--plugin-opt=-inline-max-allocated-size=32",
               "--plugin-opt=-inline-normal-into-special-section=true", "--plugin-opt=-dont-used-symbol-list=malloc,free,sprintf,printf,puts,putchar",
               "--plugin-opt=-pi32v2-enable-rep-memop", "--plugin-opt=-emulated-tls", "--sort-common",
               "--plugin-opt=-pi32v2-large-program=true", "--plugin-opt=-pi32v2-csync-before-rti=true"]


def run(cmd, **kw):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build_fm1: {Path(str(cmd[0])).name} failed")
    return r.stdout


def toolchain():
    tc = Path(os.environ.get("JIELI_TOOLCHAIN", Path.home() / ".jieli" / "toolchain"))
    for cand in (tc / "pi32v2" / "bin", tc / "bin"):
        if (cand / "clang").exists():
            return cand
    raise SystemExit("build_fm1: set JIELI_TOOLCHAIN to JieLi's toolchain (tools/get_toolchain.sh)")


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", type=Path, default=ROOT / "build" / "fm1_open")
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "fm1")
    a = ap.parse_args()
    sdk_root = a.sdk.resolve()
    sdk = sdk_root / "sdk"
    if not (sdk / "lib" / "libfm1_engine.a").exists():
        raise SystemExit("build_fm1: no FM1 Open SDK: run tools/get_sdk.sh")
    # the SDK files must be the ones its manifest names
    manifest = json.loads((sdk / "manifest.json").read_text())
    for rel, h in manifest["files"].items():
        if sha(sdk_root / rel) != h:
            raise SystemExit(f"build_fm1: SDK file differs from its manifest: {rel}")
    bin_ = toolchain()
    out = a.out
    obj = out / "obj"
    shutil.rmtree(out, ignore_errors=True)
    obj.mkdir(parents=True)
    gen = ROOT / "build" / "gen"
    gen.mkdir(parents=True, exist_ok=True)
    if not (gen / "rv_font.h").exists():
        run([sys.executable, ROOT / "tools" / "gen_rv_font.py", gen / "rv_font.h"])

    inc = [sdk / "include", sdk / "include" / "libc", sdk / "config", ROOT / "firmware" / "board",
           ROOT / "firmware" / "app", gen]
    base = FLAGS + [f"-I{p}" for p in inc] + ["-DBD_NUM_GRAINS=16", "-DBD_NO_DELAY", "-DBD_READER_ONLY",
                                             "-DRV_CACHE_RAM=__attribute__((section(\".memp_memory_x\")))"]
    # Ravel is built for size: the SDK leaves it about 80 KB of the app area
    units = [
        (ROOT / "firmware" / "app" / "ravel_app.c", ["-Wno-unused-function", "-Oz"]),
        (ROOT / "core" / "beads" / "bd.c", ["-Wno-unused-function", "-Oz"]),
        (ROOT / "core" / "beads" / "bd_tables.c", ["-Oz"]),
        (ROOT / "firmware" / "board" / "ravel_board.c", ["-fno-lto"]),
        (ROOT / "firmware" / "board" / "audio.c", []),
        (ROOT / "firmware" / "board" / "device.c", []),
        (ROOT / "firmware" / "board" / "panel.c", ["-fno-lto", "-fno-pic"]),
    ]
    objs = []
    for i, (src, extra) in enumerate(units):
        o = obj / f"{i}_{src.stem}.o"
        print(f"compile {src.relative_to(ROOT)}")
        run([bin_ / "clang"] + base + extra + ["-c", src, "-o", o])
        objs.append(o)
    # the build identity the device reports after an update, and the fixed layout
    ident_in = [hashlib.sha256((sdk / "manifest.json").read_bytes()).hexdigest()]
    for src, _ in units:
        ident_in.append(f"{src.relative_to(ROOT)}:{sha(src)}")
    build_id = hashlib.sha256("\n".join(ident_in).encode()).hexdigest()
    ident = gen / "build_identity.c"
    ident.write_text('#include "layout.h"\n'
                     f"const struct fm1_firmware_layout fm1_layout = {{{USER_BYTES}U,{UPDATE_FAMILY}U}};\n"
                     "const unsigned char fm1_build_id[32] = {" + ",".join(f"0x{build_id[i:i+2]}" for i in range(0, 64, 2)) + "};\n"
                     f'const unsigned char fm1_update_identity[27] = "{PRODUCT}";\n')
    o = obj / "build_identity.o"
    run([bin_ / "clang"] + base + ["-c", ident, "-o", o])
    objs.append(o)
    # the linker script, as the SDK's build fills it
    ld = (sdk / "config" / "linker.ld.in").read_text().replace("@SBC_DECODER@", "sbc_decoder\n") \
        .replace("@ACL_TX_EXTRA@", " + 1024").replace("@ACL_RX_EXTRA@", " + 4096")
    (gen / "linker.ld").write_text(ld)
    plugin = next((p for p in (bin_ / "LLVMgold.so", bin_.parent / "lib" / "LLVMgold.so") if p.exists()), None)
    linker = bin_ / "ld" if (bin_ / "ld").exists() else bin_ / "pi32v2-ld"
    elf = out / "ravel.elf"
    cmd = [linker] + (["--plugin", plugin] if plugin else []) + ["-o", elf] + objs + LINK_PLUGIN + \
          ["--start-group", "--whole-archive", sdk / "lib" / "libfm1_engine.a", "--no-whole-archive",
           sdk / "lib" / "libc.a", sdk / "lib" / "libcompiler_rt.a", "--end-group", "--gc-sections",
           f"-T{gen / 'linker.ld'}", f"-M={out / 'ravel.map'}", "--undefined=connectivity_wl82_bluetooth_start",
           "--undefined=audio_app_bluetooth_open", "--undefined=bt_get_local_name", "--undefined=bt_get_mac_addr"]
    print("link")
    run(cmd)
    report = check(elf, out, bin_)
    # the app image: .text, .data, .ram0_data, .cache_ram_data, in that order
    image = b""
    objcopy = bin_ / "objcopy" if (bin_ / "objcopy").exists() else bin_ / "llvm-objcopy"
    for sec in ("text", "data", "ram0_data", "cache_ram_data"):
        part = out / f"{sec}.bin"
        run([objcopy, "-O", "binary", "-j", f".{sec}", elf, part])
        image += part.read_bytes()
        part.unlink()
    (out / "ravel-app.bin").write_bytes(image)
    report["product"] = PRODUCT
    report["build_id"] = build_id
    limit = fm1pack.app_limit()
    report.update({"app_bytes": len(image), "app_limit": limit})
    if len(image) > limit:
        (out / "report.json").write_text(json.dumps(report, indent=1))
        raise SystemExit(f"build_fm1: the app image is {len(image)} B, {len(image) - limit} B over the "
                         f"FM-1's {limit} B app area (Ravel's share is about {report['ravel_flash']} B)")
    pkg = fm1pack.create(image, sdk / "tools" / "pack", out / "Ravel-FM1.fwsc", PRODUCT)
    report.update(pkg)
    (out / "report.json").write_text(json.dumps(report, indent=1))
    print(f"app image {pkg['app_bytes']} B of {pkg['app_limit']} B; package {out / 'Ravel-FM1.fwsc'} ({pkg['package_bytes']} B)")
    print(f"RAM: Ravel {report['ravel_ram']} B; runtime heap {report['heap']} B (the SDK needs 49152)")


def check(elf, out, bin_):
    """the checks that matter for Ravel: entry, sections, heap, SRAM placement"""
    nm = bin_ / "nm" if (bin_ / "nm").exists() else bin_ / "llvm-nm"
    syms = {}
    for line in run([nm, "--print-size", elf]).splitlines():
        m = re.match(r"^([0-9a-fA-F]+)\s+(?:([0-9a-fA-F]+)\s+)?(\w)\s+(\S+)", line)
        if m:
            syms[m.group(4)] = (int(m.group(1), 16), int(m.group(2) or "0", 16), m.group(3))
    mp = (out / "ravel.map").read_text(errors="replace")
    heap = re.search(r"0x([0-9a-fA-F]+)[^\n]*_MALLOC_SIZE\s*=", mp)
    heap_b = int(heap.group(1), 16) if heap else 0
    if heap_b < 49152:
        raise SystemExit(f"build_fm1: the SDK's heap is {heap_b} B, it needs 49152: Ravel uses too much RAM")
    for name in ("fm1_audio_irq", "ravel_audio_block", "fm1_input_tick", "fm1_panel_irq"):
        if name not in syms or not (0x01C00000 <= syms[name][0] < 0x01C80000):
            raise SystemExit(f"build_fm1: {name} must be in internal SRAM")
    for name in ("_start",):
        if syms.get(name, (0,))[0] != 0x02000120:
            raise SystemExit("build_fm1: the entry must be the fixed ROM entry")
    ravel = 0
    for name, (addr, size, kind) in syms.items():
        if kind in "bBdD" and 0x01C00000 <= addr < 0x01C80000 and (name.startswith(("rv", "tape_", "fb_", "dly_", "cut_", "trk", "vs", "ss", "gb", "ui", "voc_", "rev_", "beads", "evq", "sq", "pat", "wt", "sine_tab"))):
            ravel += size
    # Ravel's share of the app image: its own objects' code and constants
    flash = 0
    for m in re.finditer(r"^ (\.\S+)\s*\n?\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+)$", mp, re.M):
        sec, addr, size, obj = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
        if addr >= 0x02000000 and ("lto-llvm" in obj or "/obj/" in obj) and "bss" not in sec:
            flash += size
    return {"heap": heap_b, "ravel_ram": ravel, "ravel_flash": flash}


if __name__ == "__main__":
    main()
