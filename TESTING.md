# Validation record

Date: 2026-10-27. Version: 0.1.0 experimental.

## Completed locally

- Host: macOS arm64; Apple Clang 17.0.0.
- Target compiler/linker: Homebrew LLVM/LLD 23.1.4.
- PS5 payload SDK: v0.45; archive SHA-256
  `5fac64093d0e3e69f2584ce7935f24f703f622d9681f1495c85b9f00d1291a2c`.
- `make test`: core input/configuration/descriptor tests, USB simulation, and
  four Python ELF/sender tests passed.
- `make test-sanitize`: core and USB simulation passed with AddressSanitizer and
  UndefinedBehaviorSanitizer, including 20,000 malformed descriptor cases.
- Target build: all sources compile with `-Wall -Wextra -Werror` and link as a
  PS5 x86-64 ELF. ELF validation checks executable entry mapping and 16-KiB
  segment alignment. SDK `_start` is legitimately at virtual address zero.
- USB simulation verifies composite-interface selection, retry of a failed
  second interface, short key/mouse clicks, modifier keys, held inputs,
  disconnect/reconnect, protocol restoration, and descriptor/fd cleanup.

The USB simulation replaces system calls. It does not verify Sony's USB/HID ABI,
permissions, timing, firmware behavior, or actual device reports.

## Console validation — pending

No console was connected or modified. No firmware is currently certified for
KeyboardSense. Upstream AnyController reports firmware 13.60 testing; earlier
firmware is unverified for this project.

Record these fields for the first hardware test:

| Field | Result |
| --- | --- |
| Console firmware / HEN / ELF loader version | Pending |
| Keyboard and mouse model, VID:PID | Pending |
| Boot keyboard and mouse interfaces attach (klog) | Pending |
| Virtual controller appears and can be assigned | Pending |
| Home screen input, PS / Options / touchpad click | Pending |
| PS5 game title/version and controller settings | Pending |
| In-game WASD, mouse look, fire/ADS, face buttons | Pending |
| Mouse stop/reversal; F10 pause/resume | Pending |
| Held-input unplug/replug releases state | Pending |
| Short clicks and simultaneous key combinations | Pending |
| F9 valid/invalid config reload | Pending |
| F12 cleanup and repeat deployment | Pending |
| Second instance rejected while first runs | Pending |
| Native keyboard/mouse restored after replug | Pending |

Use klog for attach errors. This payload logs interface/endpoint information and
failures; it does not log typed keys or raw reports. Start with a standard wired
boot-protocol keyboard/mouse and a hub only after separate devices work. Perform
the first test with another controller available to navigate the shell if needed.
