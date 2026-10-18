#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""One-file emulator page: web/emu/index.html with ravel.wasm embedded, to build/web/ravel-emulator.html."""
import base64, pathlib, sys

here = pathlib.Path(__file__).resolve().parent
root = here.parent.parent
page = (here / "index.html").read_text()
wasm = base64.b64encode((here / "ravel.wasm").read_bytes()).decode()
assert '"__RAVEL_WASM__"' in page
out = root / "build" / "web" / "ravel-emulator.html"
if len(sys.argv) > 1:
    out = pathlib.Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(page.replace('"__RAVEL_WASM__"', '"' + wasm + '"', 1))
print(f"web: {out} ({out.stat().st_size} B)")
