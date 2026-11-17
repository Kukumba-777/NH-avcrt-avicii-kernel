#!/usr/bin/env python3
"""Compile the real frame validator in a test-only userspace environment.

Only Python's standard library and a C compiler with UBSan are needed. No
kernel headers, kernel build, wireless hardware, or third-party modules are used.
"""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile


TESTS = Path(__file__).resolve().parent
ROOT = TESTS.parent
SOURCE = ROOT / "core/hdd/src/wlan_hdd_frame_validate.c"
COMMON = ROOT / "core/cds/inc/cds_ieee80211_common.h"
INJECT = ROOT / "core/hdd/inc/wlan_hdd_frame_inject.h"


def define_from(text, name):
    """Copy an actual, single-line object-like definition; fail on drift."""
    matches = re.findall(
        r"^#define[ \t]+" + re.escape(name) + r"[ \t]+([^\n]+)$",
        text,
        re.MULTILINE,
    )
    if len(matches) != 1 or matches[0].rstrip().endswith("\\"):
        raise ValueError("expected one single-line definition of " + name)
    return "#define " + name + " " + matches[0] + "\n"


def prepare(work, source):
    """Copy the complete C source unchanged, stubbing only its includes."""
    common = COMMON.read_text(encoding="utf-8")
    structures = re.findall(
        r"^struct ieee80211_frame \{\n.*?^\} __packed;",
        common,
        re.MULTILINE | re.DOTALL,
    )
    if len(structures) != 1:
        raise ValueError("expected one actual struct ieee80211_frame definition")

    # Import wire layout and every referenced IEEE constant from this tree,
    # rather than maintaining a second set of protocol constants in the tests.
    names = sorted(set(re.findall(r"\bIEEE80211_[A-Z0-9_]+\b", source.decode())))
    protocol = '#include "frame_validate_stubs.h"\n' + structures[0] + "\n"
    protocol += "".join(define_from(common, name) for name in names)
    (work / "cds_ieee80211_common.h").write_text(protocol, encoding="utf-8")
    (work / "wlan_hdd_frame_inject.h").write_text(
        define_from(INJECT.read_text(encoding="utf-8"), "HDD_FRAME_INJECT_MAX_SIZE"),
        encoding="utf-8",
    )

    (work / "frame_validate_stubs.h").write_bytes(
        (TESTS / "frame_validate_stubs.h").read_bytes()
    )
    for name in ("wlan_hdd_includes.h", "qdf_mem.h", "qdf_trace.h"):
        (work / name).write_text(
            '/* TEST-ONLY USERSPACE STUB, NOT A KERNEL HEADER. */\n'
            '#include "frame_validate_stubs.h"\n',
            encoding="utf-8",
        )
    (work / "linux").mkdir()
    (work / "linux/ieee80211.h").write_text(
        "/* TEST-ONLY: needed definitions come from the actual CDS header. */\n",
        encoding="utf-8",
    )
    copied_source = work / SOURCE.name
    copied_source.write_bytes(source)
    if copied_source.read_bytes() != source:
        raise ValueError("validator source copy changed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cc", default=os.environ.get("CC", "cc"),
        help="C compiler command (default: $CC or cc); UBSan support is required",
    )
    args = parser.parse_args()
    compiler = shlex.split(args.cc)
    if not compiler:
        parser.error("--cc must not be empty")

    source = SOURCE.read_bytes()
    print("Source SHA256: " + hashlib.sha256(source).hexdigest(), flush=True)
    print("Build: complete unmodified validator; test-only userspace stubs; UBSan", flush=True)
    # Keep all generated headers, source copies and binaries inside this tree.
    # The context manager removes our private directory on success or failure.
    with tempfile.TemporaryDirectory(prefix=".frame_validate-", dir=TESTS) as name:
        work = Path(name)
        prepare(work, source)
        binary = work / "frame_validate_test"
        command = compiler + [
            "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra",
            # Existing diagnostic-only source variables/comparisons are not
            # relevant to these tests; do not rewrite production code for them.
            "-Wno-unused-but-set-variable", "-Wno-type-limits",
            "-fsanitize=undefined", "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer", "-DFEATURE_FRAME_INJECTION_SUPPORT",
            "-I", str(work), str(TESTS / "frame_validate_test.c"),
            "-o", str(binary),
        ]
        environment = os.environ.copy()
        # Compilers may create intermediate files even without -save-temps.
        # Keep those inside our owned directory as well, not the system /tmp.
        environment["TMPDIR"] = str(work)
        subprocess.run(command, cwd=work, env=environment, check=True)
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        subprocess.run([str(binary)], cwd=work, env=environment, check=True)
    print("Cleanup: private temporary build directory removed", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        sys.exit(1)
