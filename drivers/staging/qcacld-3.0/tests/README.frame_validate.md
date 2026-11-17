# Frame validator regression tests

From the `qcacld-3.0` directory:

```sh
python3 tests/test_frame_validate.py
```

Requirements: Python 3's standard library and a system C compiler supporting
GNU C11 and UndefinedBehaviorSanitizer (for example, GCC or Clang). No packages,
external kernel tree, kernel build, root privileges, network, phone, or wireless
hardware are needed. To choose a compiler:

```sh
python3 tests/test_frame_validate.py --cc gcc
CC=clang python3 tests/test_frame_validate.py
```

## What is compiled

The runner reads `core/hdd/src/wlan_hdd_frame_validate.c` and copies its **entire
contents unchanged** into a private temporary directory below `tests/`. The C
harness includes that copy, enabling both the actual static validation helpers
and public entry points to be exercised in one translation unit. The production
source is never edited; there is no second implementation of its validator or
sanitizer logic.

The runner copies the actual `struct ieee80211_frame` definition and referenced
`IEEE80211_*` constants from `core/cds/inc/cds_ieee80211_common.h`. It also imports
`HDD_FRAME_INJECT_MAX_SIZE` from `core/hdd/inc/wlan_hdd_frame_inject.h`. Extraction
fails rather than silently substituting definitions if these declarations drift
to an unsupported form. Static assertions check key wire-layout offsets.

**This is a userspace test build, not a kernel build.** Minimal, clearly marked
stub headers supply standard integer types, QDF status symbols, MAC-address
predicates, and no-output logging. Stub status numbers are test-local, not an
assertion about the kernel ABI. All diagnostic arguments are still evaluated.
The empty `linux/ieee80211.h` stub intentionally avoids host kernel dependencies.

Compilation enables `FEATURE_FRAME_INJECTION_SUPPORT` and uses:

```text
-std=gnu11 -O1 -g -Wall -Wextra
-Wno-unused-but-set-variable -Wno-type-limits
-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
```

The two warning exclusions concern existing diagnostic-only source code, not
test rewrites of production logic. Compiler errors, failed assertions, and UBSan
diagnostics make the runner exit unsuccessfully. No unsanitized fallback is used.
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` is set for the test process.

## Coverage and limits

- Versions **0, 1, 2, and 3** on association, reassociation, ACK, and QoS data
  layouts: exercise the actual header helper, sanitizer, and public validator.
  Only version 0 succeeds; rejection must not rewrite any byte, including the
  Protected bit.
- All **256 FC1 byte values** on each of those four layouts: sanitizer and public
  validation must leave the complete input unchanged. This includes the 128
  combinations with Protected (`0x40`) set, plus DS, Order, and other flags.
  The QoS fixture has room for the largest tested Addr4/QoS/HT Control layout.
  Acceptance here characterizes current checks; it does not declare every flag
  combination protocol-valid, or authorize injection of protected frames.
- Association request **27 rejected / 28 accepted**, and reassociation request
  **33 rejected / 34 accepted**, through both the actual management helper and
  public validator.
- Literal, FCS-free 802.11 control wire fixtures, through both the control helper
  and public validator, test minimum-minus-one rejection and minimum acceptance:

  | Subtype | Reject length | Accept length |
  | --- | ---: | ---: |
  | RTS | 15 | 16 |
  | CTS | 9 | 10 |
  | ACK | 9 | 10 |
  | PS-Poll | 15 | 16 |
  | CF-End | 15 | 16 |
  | CF-End+CF-Ack | 15 | 16 |
  | Single-TID compressed BAR | 19 | 20 |
  | Single-TID compressed BA | 27 | 28 |

- **Known BA limitation, explicitly characterized rather than fixed:** a basic
  BA wire fixture with a 128-byte bitmap is accepted at its full 148 bytes, but
  its invalid 28-byte prefix is also currently accepted. The test prints this
  limitation and expects the present behavior. If BA Control-dependent validation
  is implemented, update this characterization to expect rejection. These tests
  do **not** establish complete BA, BAR variant, semantic, IE, crypto, FCS, or
  injection-path validation.

Each call uses a freshly allocated buffer of exactly the supplied length and
compares every byte after the call. Buffers are normally aligned; this is not an
unaligned-input test. UBSan alone is not an exhaustive bounds or memory-safety
proof, and the userspace stubs do not establish kernel integration correctness.

The private `.frame_validate-*` directory contains all generated headers, the
source copy, and the executable; compiler `TMPDIR` also points there. The
directory is removed on ordinary success or failure.
The runner never cleans other directories or other people's files. An uncatchable
process termination can leave its private directory behind.
