# Third-party code in Ravel

| What | Where | Licence |
| --- | --- | --- |
| Mutable Instruments Beads (grain engine), rewritten in fixed point | `core/beads/` | MIT, `core/beads/LICENSE-beads` |
| FM1 Open board code (audio interrupt, panel, screen, input wiring) | `firmware/board/` except `ravel_board.c` | GPL-3.0 with M-VAVE's FM1 SDK linking exception, `firmware/board/FM1-APP-LICENSE.md` |
| FM1 Open packager (`Package.cs`), ported to Python | `tools/fm1pack.py` | MIT, notice in the file |
| JieLi toolchain download script | `tools/get_toolchain.sh` | GPL-3.0, notice in the file |
| Barlow Semi Condensed (screen fonts) | `assets/fonts/` | SIL Open Font License 1.1 |
| PNG writer of the desktop simulator | `host/ravel_host.c` | GPL-3.0 (from X0X) |

Not in this repository, fetched at build time by `tools/get_sdk.sh`: M-VAVE's FM1 Open SDK
(binary library, C runtime, boot and update resources). A Ravel firmware package contains parts
of it under M-VAVE's terms; see `firmware/board/FM1-THIRD_PARTY.md`.
