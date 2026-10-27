# Native sidebar renderer

This directory contains an experimental SceShellUI renderer and its bootstrap.
The main payload embeds `build/sidebar_renderer.elf`; send only
`dist/keyboardsense.elf` to the console. The renderer is a shared object for the
bootstrap, not an independently deployable payload.

The bootstrap, Mono/PUI helpers, scene lifecycle, and update hook are adapted
from [ps5-dualsense-overlay](https://github.com/erickdavestech/ps5-dualsense-overlay)
at `e1de419707860a8da01d3604b0b5632771159756`, GPL-3.0-or-later. Upstream
credits Common FPS by porhe911 and SimpleFPS by khalifa007. Original copyright
notices remain in the files; the GPL text is in `LICENSE`.

`shsrv/pt.c`, `pt.h`, `elfldr.c`, and `elfldr.h` are vendored from
[ps5-payload-dev/shsrv](https://github.com/ps5-payload-dev/shsrv)
at `6f320637d56d344a0e7797753099e33238bbf146` (GPL-3.0-or-later;
license in `shsrv/LICENSE`). `stable_elfldr_bridge.c` uses these helpers and the
upstream loader source. These sources are included in the release archive.

KeyboardSense changes replace the FPS/controller display with a sidebar,
namespace runtime files, embed a solid PNG background, and exchange bounded
`KsSidebarFrame` / `KsSidebarAck` datagrams on **loopback UDP 1406**. Profiles,
raw USB input, menu navigation, and binding capture remain owned by the main
input thread. The renderer receives display state only. It does not read
`/data`; its background is generated in `/Temp`. Bootstrap diagnostics use
`/data/KeyboardSenseOverlay*` and `/system_tmp/keyboardsense*`.

The bridge enables bootstrap attempts only for firmware families 9.60 and
11.00–13.60 represented by the borrowed source. This is a gate, not a
compatibility certification. No KeyboardSense console testing has been done.
An active game and stable ShellUI are required. Without renderer acknowledgments
the shortcut leaves input available and directs the user to port 1405. Losing
acknowledgments closes the draft and waits for held inputs to be released.

Do not run Common FPS, SimpleFPS, or another DualSense overlay concurrently:
they can hook the same ShellUI update method. The renderer stays resident in
ShellUI after the main payload stops, hiding after its heartbeat expires.
Returning an injected thread or attempting a live unhook is deliberately avoided,
following upstream's lifecycle contract. Reboot before testing a changed renderer
or a different overlay. Shutdown waits for an in-progress bootstrap transaction
to restore ShellUI; this can take around a minute.

`tools/check_renderer.py` checks the shared ELF's relocations, entry, initializer
contract, and imports against the bootstrap's resolver. It does not prove that
Mono/PUI methods, hook instructions, permissions, or process APIs work on hardware.
