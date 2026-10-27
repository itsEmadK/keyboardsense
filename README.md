# KeyboardSense

A PS5 ELF payload that maps a keyboard and mouse **plugged directly into the
console** to one virtual DualSense. WASD drives the left stick; mouse movement
drives the right stick. Games receive controller input, including games without
native keyboard/mouse support. No PC is needed during play.

**Status: experimental, compiled but not yet tested on a PS5.** The virtual-pad
ABI comes from [AnyController](https://github.com/BallerIsLeet/AnyController), whose
author reports testing on firmware **13.60**. KeyboardSense's USB attachment,
assignment UI, button bits, and behavior in actual games still need console
validation. Earlier jailbroken firmware up to 13.60 is a desired compatibility
range, not a verified support claim. There are no firmware-specific memory
patches or shell injection in this payload.

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

   Or launch the ELF with your existing payload manager. The payload creates no network listener. Sending the same payload while it runs is rejected by a
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
   the payload's settings together.

A full reboot requires loading the payload again. Rest-mode survival is not
verified. If stopped or unable to attach, unplug and reconnect the devices to
restore the PS5's native keyboard/mouse drivers: FreeBSD driver detach has no
matching attach ioctl in this backend. Clean shutdown returns devices to report
protocol, releases interfaces, submits a neutral pad, and deletes the virtual
pad. A crash/forced kill may bypass cleanup; reboot if a virtual pad remains.

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
| F9 | Reload configuration |
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
`leftmeta`, `rightctrl`, `rightshift`, `rightalt`, `rightmeta`, `f1`–`f8`, `f11`,
`mouse_left`, `mouse_right`, `mouse_middle`. F9/F10/F12 are reserved.

Actions: `none`, `left`, `right`, `up`, `down`, `walk`, `cross`, `circle`, `square`,
`triangle`, `l1`, `r1`, `l2`, `r2`, `l3`, `r3`, `dpad_up`, `dpad_down`, `dpad_left`,
`dpad_right`, `options`, `touchpad`, `ps`, `create`. Movement actions drive the
left stick; `dpad_*` actions drive the D-pad. Triggers are digital 0/255.

## Build and test

Use the public [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk), **v0.45**
(the pinned baseline) or a compatible newer version, plus LLVM, LLD, make, and
Python 3. No proprietary Sony SDK is used.

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

Host-side verification requires a C compiler and Python 3, but no PS5 SDK:

```sh
make test
make test-sanitize
```

The tests cover boot reports/rollover, button and stick mapping, diagonal
normalization, opposing keys, trigger bits, ADS sensitivity, inversion, idle
recentering, transactional configuration reload, composite USB descriptors,
20,000 malformed descriptor cases, composite-interface attach/retry and hotplug
simulation, short clicks, ELF validation, and the sender. These checks
cannot establish that Sony's USB/HID ioctls work on a particular firmware.

Before claiming console support, record firmware + HEN, device models, klog,
assignment behavior, and results in at least one PS5 game. Check short clicks,
held movement, mouse stop/reversal, unplug/replug while holding inputs, F9/F10/F12,
and loading a second instance. See `TESTING.md` for the current validation record.

## License and attribution

GPL-3.0-only. See `LICENSE` and `NOTICE`. The virtual-pad ABI is adapted from
AnyController (commit recorded in `NOTICE`), which credits FGG-XSense. Ghostpad
and PS5 DevLoop provided research context for controller assignment and game
input behavior. Not affiliated with Sony.
