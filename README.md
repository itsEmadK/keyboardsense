# KeyboardSense

A PS5 ELF payload that maps a keyboard and mouse **plugged directly into the
console** to one virtual DualSense. WASD drives the left stick; mouse movement
drives the right stick. Games receive controller input, including games without
native keyboard/mouse support. No PC is needed during play. An input lab and settings sidebar are served by
the payload at **http://<PS5-IP>:1405/**; an experimental native sidebar can
open over the game with **F8** or **Ctrl+Alt+M**.

**Status: experimental, compiled but not yet tested on a PS5.** The virtual-pad
ABI comes from [AnyController](https://github.com/BallerIsLeet/AnyController), whose
author reports testing on firmware **13.60**. KeyboardSense's USB attachment,
assignment UI, button bits, and behavior in actual games still need console
validation. Earlier jailbroken firmware up to 13.60 is a desired compatibility
range, not a verified support claim. The native sidebar now injects a renderer
into SceShellUI using public overlay sources. Its bootstrap is gated to firmware
families **9.60 and 11.00–13.60**, and still needs hardware validation. Other
firmware keeps the web settings editor available. See `native/README.md`.

## Requirements and device scope

- A jailbroken PS5 with an ELF loader (normally TCP 9021) and a HEN/runtime
  allowing payload access to `/dev/hid`, USB control requests, and driver detach.
- USB **HID boot-protocol** keyboard and mouse interfaces. The payload explicitly
  switches them to boot protocol. A composite receiver is supported in the code
  if it exposes separate boot keyboard and mouse interfaces.
- A way to send the ELF once, or a payload manager on the console.

This version reads six-key-rollover keyboard reports, all eight modifier keys,
relative 8-bit mouse X/Y, and the first three mouse buttons. It selects only HID
boot interfaces and their interrupt IN endpoints; it does not detach unrelated
interfaces on a composite device. Up to eight interfaces merge into one pad.
Bluetooth, mouse wheels, side buttons, report-only NKRO keyboards, proprietary
gaming-mouse reports, gyro, touch coordinates, adaptive triggers, and haptics are
not implemented. Switch a gaming keyboard to BIOS/6KRO mode if it has one; a
standard wired office keyboard/mouse is the best first test.

Mouse aiming is **right-stick emulation**. Each game's controller acceleration,
deadzone, and maximum turn speed still apply. This is not native mouse aiming.
Use a reasonably low mouse DPI initially; boot reports have 8-bit relative axes.

## Run

1. Load your console's normal jailbreak/HEN and ELF loader.
2. Send the supplied `dist/keyboardsense.elf`, for example:

   ```sh
   python3 tools/send_payload.py 192.168.1.50 dist/keyboardsense.elf
   ```

   Or launch the ELF with your existing payload manager. The payload serves its embedded testing page on TCP 1405; it exposes no
   remote-input endpoint. Sending the same payload while it runs is rejected by a
   file lock; stop the old instance first.
3. Plug the keyboard and mouse into the PS5 (a USB hub is fine if the console
   enumerates its devices). Watch for the KeyboardSense notification. USB attach
   details and errors are written to the console's kernel log.
4. Assign the new virtual controller to your player in the PS5's controller/user
   picker. Use WASD/arrows and Space (Cross), or your real DualSense to complete
   the picker. Then turn off the real controller if a game keeps reading that
   controller rather than the virtual one. The physical and virtual controllers
   must not compete for the game's first pad slot.
5. Test the home screen, then a game. Tune the game's controller sensitivity and
   the payload's settings together, using the sidebar or web editor.

A full reboot requires loading the payload again. Rest-mode survival is not
verified. If stopped or unable to attach, unplug and reconnect the devices to
restore the PS5's native keyboard/mouse drivers: FreeBSD driver detach has no
matching attach ioctl in this backend. Clean shutdown returns devices to report
protocol, releases interfaces, submits a neutral pad, and deletes the virtual
pad. A crash/forced kill may bypass cleanup; reboot if a virtual pad remains.

## Web input lab (port 1405)

After loading the **v0.3.1** ELF, open **Media → KeyboardSense** on the console,
or open `http://<PS5-IP>:1405/` from a browser on
its LAN. All HTML, JavaScript, CSS, keyboard glyphs, and controller sprites are
embedded in the ELF: no CDN, web folder upload, or browser plugin is required.

- **Left:** a full ANSI 104-key keyboard highlights the USB keys the payload is
  reading. Mapped keys show their actions. Mouse buttons and X/Y deltas appear
  below it, alongside attached USB interfaces.
- **Right:** a DualSense animates its sticks, face buttons, D-pad, shoulder
  buttons, and triggers from the state submitted to `/dev/hid`. Values and the
  input → mapping → output path are displayed below. This reflects submission
  status, not an independent readback proving that a game accepted the input.
- **PS5 live** uses only console USB telemetry. Browser keypresses never drive
  the console. F10 pauses game input while raw USB telemetry remains visible.
- **Browser demo** is a local simulation using the active profile. Click
  **Start test**, then type, click the displayed keys, or move/click inside the
  mouse area. **Lock cursor** optionally captures relative mouse motion. Focus
  loss, stopping the test, and source switching clear all held demo input.
  OS/browser-reserved shortcuts may be intercepted before the page receives
  them. The demo mapper is tested against the C mapper at identical timings.
- A faint 100-ms afterglow preserves short live key/button taps between browser
  polls. Solid fill means currently held; the held-key counter and numeric pad
  values always describe the current snapshot.

The HTTP worker is nonblocking, bounds requests/clients/timeouts, and runs on a
separate thread. Gameplay publishes snapshots with a try-lock rather than waiting
for browsers. `GET /api/state` is read only. Settings changes use a bounded
`POST /api/config` queue, a custom header/content type, and a profile revision
checked again by the input thread. There is no endpoint to inject remote key or
mouse input. Anyone on the reachable LAN can view telemetry **and change/save
settings** while the payload runs; this is an unauthenticated LAN interface.
F12 stops the controller and HTTP listener.

For a local preview without a console (C compiler + Python 3 required):

```sh
make preview
# Open http://127.0.0.1:1405/ and click Start test in Browser demo.
# Ctrl+C stops the preview.
```

This preview uses the same C HTTP server and embedded assets as the PS5 build,
binds only to loopback, and labels itself **Local preview**. It cannot control a
PS5. If port 1405 is already occupied, stop the other local service first.

Artwork was found through web/GitHub research: DualSense sprites from
[ps5-dualsense-overlay](https://github.com/erickdavestech/ps5-dualsense-overlay)
(original art by [Zacksly](https://zacksly.itch.io), CC BY 3.0), and
[SVG Keyboard Icons by George Black](https://github.com/georgemblack/svg-keyboard-icons)
(MIT). Full attribution is linked from the page footer and included in the source
archive. No assets are downloaded by the runtime.

### Media shortcut

After the server binds port 1405, the payload installs/registers a
**KeyboardSense** tile in the PS5's **Media** section. Its launch URI is
`http://127.0.0.1:1405/`, so it opens this console's web UI without knowing its
LAN address. The icon and metadata are embedded in the ELF; no separate PKG,
internet connection, or app upload is needed. This uses the same title-directory
registration pattern as PS5 Web File Manager and PS5 Browser Search, with a
dedicated title ID `KBSN01405`.

The tile is persistent, but **it does not start the payload**. After a reboot,
load KeyboardSense through your ELF loader/payload manager before opening the
tile. F12 stops the web server; the tile opens again once the payload is running.
Opening it needs console validation, including the built-in browser's support
for the input lab and settings UI on each firmware.

Launcher files live only in `/user/app/KBSN01405/`. Matching assets are not
rewritten on each startup, but the payload re-registers its own title to recover
from database rebuilds or a failed previous attempt. It refuses to overwrite an
existing title directory without its `.keyboardsense-webui` ownership marker.
It does not edit app databases directly or register other app directories.
Missing API/permissions or a registration failure are logged, and input/the
web server continue normally. Registration is retried on the next startup.

To remove the tile, select it and use **Options → Delete** on the PS5. It will
be created again the next time this payload starts. The host preview never
registers a console shortcut.

## Sidebar, bindings, and profiles

The right-hand sidebar has **Bindings**, **Mouse**, and **Profiles** tabs, with
**Apply**, **Save for this game**, and **Cancel** at the bottom. In the web input
lab, click Settings or press F8 / Ctrl+Alt+M. Opening it stops browser-demo capture.

Bindings work from either end:

- **Controller → Input:** click a DualSense control (or choose its action), then
  press the desired key or capture a mouse button in the listening box. Adding
  Z to Cross keeps Space assigned too. Individual binding chips can be removed.
- **Input → Controller:** select a key on the displayed keyboard, choose an
  input from the list, or listen for it, then select its DualSense action.
  If W already drives the left stick, changing it to Cross requires choosing
  **Replace**; **Keep** preserves its existing mapping.

One input has one action; multiple inputs can share an action. Right-stick
arrow actions are also bindable. Physical USB/US-QWERTY positions are used.
Mouse movement remains right-stick emulation; three mouse buttons can be remapped.

**Mouse** exposes hip sensitivity, ADS multiplier, smoothing, anti-deadzone,
walk speed, and invert Y. Values have sliders and numeric fields in the web UI.
Changes are drafts until applied or saved. Apply changes the current session;
Save persists a complete profile. With no game detected, Save writes the global
profile. Profiles can copy the global settings or reset the draft to defaults.

The payload detects the running game's title ID. It loads
`/data/keyboardsense/profiles/<TITLE_ID>.ini` when present, otherwise
`/data/keyboardsense/config.ini`. Changing games closes old drafts and loads the
new scope; web submissions for an old title/revision are rejected. Each game
file is a complete snapshot, so later global changes do not merge into it.
Session-only changes end on profile reload, game change, or payload restart.
The local preview uses temporary storage and cannot detect a PS5 game.

### Native sidebar on the TV (experimental)

With a game running, give ShellUI/bootstrap time to become ready, then press
**F8** or **Ctrl+Alt+M** on the PS5 keyboard. The payload only captures menu input
when the renderer acknowledges readiness. If unavailable, a notification directs
you to port 1405. No separate homebrew app is required by this implementation.

- **Tab:** switch sections. **Up/Down:** choose a row. **Left/Right:** adjust a
  mouse setting. The mouse moves a cursor; click tabs, rows, or footer buttons.
- **F2:** toggle binding direction. Controller-first: select an action and press
  Enter, release activation keys, then press the desired input. Input-first:
  press Enter to listen, press the input, choose its action, then Enter to assign.
- A conflict requires **Enter** to replace or **Escape** to keep the old binding.
  **Backspace** unbinds the selected input. **F8** closes capture/menu.
- **F5:** Apply. **F6:** Save to this game (global if no title). **Escape:** Cancel.
  During capture Escape itself can be bound; use F8 to exit capture.

Game input is neutral while the native sidebar is open and until held inputs
are released after it closes. **The game itself is not paused**. A game change
or lost renderer heartbeat discards the draft and releases capture. Do not edit
from the TV and web simultaneously; the backend rejects conflicting drafts.

The renderer hooks SceShellUI's Mono/PUI update method. Do not run it alongside
Common FPS, SimpleFPS, or another DualSense overlay that hooks the same method.
After F12, the renderer stays resident and hides on heartbeat expiry. Reboot
before loading a changed renderer or another overlay. Shutdown can wait roughly
a minute for an in-progress bootstrap transaction to finish restoring ShellUI.
These lifecycle behaviors require console validation, just like the USB backend.

## Default controls

| Input | DualSense |
| --- | --- |
| W/A/S/D | Left stick (diagonals normalized) |
| Mouse movement | Right stick |
| Left / right mouse | R2 / L2 (fire / aim in common FPS layouts) |
| Middle mouse | R3 |
| Space / Left Ctrl | Cross / Circle |
| R / E | Square / Triangle |
| Q / F | L1 / R1 |
| Left Shift | L3 |
| Left Alt | Walk (45% stick deflection) |
| Arrow keys | D-pad |
| Enter / Tab / Escape | Options / Touchpad click / PS |
| F8 / Ctrl+Alt+M | Open/close native sidebar |
| F9 | Reload global/current-game configuration |
| F10 | Pause/resume virtual input (pause sends neutral) |
| F12 | Stop and remove the virtual controller |

The PS bit is inherited from upstream and needs verification here. The Create
bit is explicitly unverified upstream and is not bound by default.
F10 pauses emulated input; it does **not** restore native keyboard/mouse drivers.

## Configuration

Copy `config/config.ini` to `/data/keyboardsense/config.ini` using your console's
usual file-transfer tooling. Create the directory first. Defaults work without
this file. Press F9 to reload it without restarting; a malformed file is rejected
as a whole, keeping the current settings. Startup falls back to defaults on an
invalid file and shows an error notification.

```ini
sensitivity = 1.0
ads_multiplier = 0.6
deadzone = 0.08
smoothing_ms = 12
walk_speed = 0.45
invert_y = false
bind.space = cross
bind.mouse_right = l2
```

- `sensitivity`: 0.01–20; at 1, 2,000 mouse counts/second requests full stick
  deflection before smoothing. Higher values turn faster up to the game's limit.
- `ads_multiplier`: 0.01–4; sensitivity multiplier while any binding holds L2.
- `deadzone`: 0–0.5; radial anti-deadzone added to nonzero mouse output. Match it
  to the game's deadzone; excessive values make fine movement jumpy.
- `smoothing_ms`: 0–100; exponential smoothing time constant; 0 disables it.
  Mouse output recenters after 32 ms without motion to prevent persistent drift.
- `walk_speed`: 0.05–1; left-stick scale while holding a `walk` binding.
- `invert_y`: `true` or `false`.

Bindings use `bind.<key> = <action>`. Unspecified keys retain defaults; use `none`
to remove one. Letter names describe **physical USB/US-QWERTY positions**, not
localized characters. Names/values are case sensitive. Available keys: `a`–`z`,
`0`–`9`, `enter`, `escape`, `backspace`, `tab`, `space`, `minus`, `equal`,
`leftbracket`, `rightbracket`, `backslash`, `semicolon`, `apostrophe`, `grave`,
`comma`, `period`, `slash`, `capslock`, `insert`, `home`, `pageup`, `delete`, `end`,
`pagedown`, `up`, `down`, `left`, `right`, `leftctrl`, `leftshift`, `leftalt`,
`leftmeta`, `rightctrl`, `rightshift`, `rightalt`, `rightmeta`, `f1`–`f7`, `f11`,
`mouse_left`, `mouse_right`, `mouse_middle`. `usb_XX` accepts other physical HID
usage IDs (hex), including numpad keys. F8/F9/F10/F12 are reserved; configurations
that bind them are rejected. The Ctrl+Alt+M chord is consumed as a shortcut.

Actions: `none`, `left`, `right`, `up`, `down`, `walk`, `cross`, `circle`, `square`,
`triangle`, `l1`, `r1`, `l2`, `r2`, `l3`, `r3`, `dpad_up`, `dpad_down`, `dpad_left`,
`dpad_right`, `options`, `touchpad`, `ps`, `create`, `rs_left`, `rs_right`,
`rs_up`, `rs_down`. Movement actions drive the left stick; `rs_*` drives the
right stick, combined with mouse output and normalized; `dpad_*` drives the D-pad. Triggers are digital 0/255.

## Build and test

Use the public [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk), **v0.45**
(the pinned baseline) or a compatible newer version, plus LLVM, LLD, make, and
Python 3. Node.js 18+ is used for web tests. No proprietary Sony SDK is used.

On macOS, install `llvm` and `lld` with Homebrew and put LLVM's bin directory in
PATH. On Linux/WSL, install clang, lld, llvm, make, curl, and unzip. The SDK's
wrappers locate LLVM using `llvm-config`.

```sh
curl -fL https://github.com/ps5-payload-dev/sdk/releases/download/v0.45/ps5-payload-sdk.zip -o sdk.zip
# SHA-256 of the SDK archive used for this build:
# 5fac64093d0e3e69f2584ce7935f24f703f622d9681f1495c85b9f00d1291a2c
unzip sdk.zip -d /your/sdk/location
export PS5_PAYLOAD_SDK=/your/sdk/location/ps5-payload-sdk
# macOS Homebrew, if needed:
# export PATH="$(brew --prefix llvm)/bin:$(brew --prefix lld)/bin:$PATH"
make
make package
```

`make package` writes the ELF, configuration, license, instructions, corresponding
source archive, and SHA-256 checksums into `dist/`. `make send PS5_HOST=192.168.1.50` deploys a fresh build to
an already running loader. `make clean` deletes generated build files.

Host-side verification requires a C compiler, Python 3, Node.js 18+, and
permission to bind ephemeral loopback sockets; it does not require a PS5 SDK:

```sh
make test
make test-sanitize
```

The tests cover boot reports/rollover, button and stick mapping, diagonal
normalization, opposing keys, trigger bits, ADS sensitivity, inversion, idle
recentering, transactional configuration reload, composite USB descriptors,
20,000 malformed descriptor cases, composite-interface attach/retry and hotplug
simulation, short clicks, ELF validation, and the sender. Web tests additionally cover all 104 keys, 1,000 exact C/JS mapper
parity frames (including digital right-stick bindings), JSON bounds/escaping,
real HTTP requests, slow/parallel clients, short-tap afterglow, shutdown, and
restart. Editor tests cover both binding directions, alias preservation, conflict
confirmation, per-game/global saves, cancellation, stale revisions, renderer
loss, reserved keys, and settings validation. The native renderer build also
checks its shared-ELF entry, imports, relocations, and initializer contract. These checks
cannot establish that Sony's USB/HID ioctls work on a particular firmware.

Before claiming console support, record firmware + HEN, device models, klog,
assignment behavior, and results in at least one PS5 game. Check short clicks,
held movement, mouse stop/reversal, unplug/replug while holding inputs, F9/F10/F12,
and loading a second instance. Also validate title changes, native rendering,
binding capture, conflict/cancel flows, saves surviving restart, and loss of
renderer readiness. See `TESTING.md` for the current validation record.

## License and attribution

Application code: GPL-3.0-only. Third-party artwork retains its CC BY 3.0 and
MIT licenses; see `web/CREDITS.txt`, `third_party/`, `LICENSE`, and `NOTICE`. The virtual-pad ABI is adapted from
AnyController (commit recorded in `NOTICE`), which credits FGG-XSense. Native
renderer sources and shsrv helpers retain GPL-3.0-or-later notices; see
`native/README.md` and included licenses. Ghostpad
and PS5 DevLoop provided research context for controller assignment and game
input behavior. Not affiliated with Sony.
