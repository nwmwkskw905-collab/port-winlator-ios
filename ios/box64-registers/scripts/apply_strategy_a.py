#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FASE 04 - STRATEGY A: guest R8 : host x18 -> host x9.

Applies the register-model adaptation to a box64 source tree with targeted,
assert-guarded edits.  It is NOT a textual substitution: every edit is anchored
on a context string that must match exactly once (or an expected number of
times), and the two `_WIN32` TEB sites are deliberately left writing physical
x18, because there x18 is the *platform* register, not guest R8.

Upstream base: box64 v0.4.4, commit 2f130fab1d6e1a4ee8a71dc60cfdfcc839ad192a
Usage:  python3 apply_strategy_a.py <box64-src-root>

Exit code 0 = all edits applied and verified.  Anything else = nothing was
written (every anchor is validated before the first write).

Edits
-----
arm64_mapping.h          xR8 18->9 ; +xPLATFORM 18 ; TO_NAT -> table ; IS_GPR -> bitmap
                         (TO_NAT and IS_GPR are GENERATED from the mapping above them)
dynarec_arm64_helper.c   _WIN32 TEB load -> xPLATFORM                (2 sites, mandatory)
                         checkCRC              -> R8 spill/reload    (mandatory)
                         flagsCacheTransform   -> R8 spill/reload    (defensive)
dynarec_arm64_helper.h   READFLAGS / GRABFLAGS  -> R8 spill/reload    (defensive)
dynarec_arm64_functions.c  disassembly annotation table, r8 rows
arm64_printer.c            debug printer tables (index 9 becomes xR8, 18 becomes xPLATFORM)
arm64_prolog.S             guest R8/R9 pair load      -> x9, x19
arm64_epilog.S             guest R8/R9 pair store     -> x9, x19
arm64_next.S               R8 save/restore around LinkNext -> x9, x27
"""

import os
import re
import sys

EDITS = []
FAILED = []


def edit(relpath, old, new, count=1, regex=False):
    EDITS.append((relpath, regex, old, new, count))


# ---------------------------------------------------------------------------
# 0. The mapping data, in one place.  TO_NAT and IS_GPR are GENERATED from it,
#    so the tables can never drift from the #defines (an earlier hand-written
#    bitmap was one entry short and the test harness caught it).
# ---------------------------------------------------------------------------
HOST = {                       # guest slot name -> host register number
    "xRAX": 10, "xRCX": 11, "xRDX": 12, "xRBX": 13, "xRSP": 14, "xRBP": 15,
    "xRSI": 16, "xRDI": 17, "xR8": 9, "xR9": 19, "xR10": 20, "xR11": 21,
    "xR12": 22, "xR13": 23, "xR14": 24, "xR15": 25,
}
ORDER = ["xRAX", "xRCX", "xRDX", "xRBX", "xRSP", "xRBP", "xRSI", "xRDI",
         "xR8", "xR9", "xR10", "xR11", "xR12", "xR13", "xR14", "xR15"]
PLATFORM = 18                  # x18: Windows TEB under _WIN32, reserved on Darwin

assert len(set(HOST.values())) == 16, "host registers must be 16 distinct"
assert PLATFORM not in HOST.values(), "no guest register may live in x18"

gpr_bitmap = [0] * 32
for _n in ORDER:
    gpr_bitmap[HOST[_n]] = 1
gpr_bitmap[26] = gpr_bitmap[27] = 1        # xFlags, xRIP (kept from the old range test)
assert len(gpr_bitmap) == 32


def _row(vals):
    return " ".join("%d," % v for v in vals)


TONAT_LIST = ", ".join(ORDER)
GPR_ROWS = [_row(gpr_bitmap[i:i + 8]) for i in range(0, 32, 8)]

# ---------------------------------------------------------------------------
# 1. arm64_mapping.h
# ---------------------------------------------------------------------------
MAPPING_OLD_R8 = "#define xR8     18"

MAPPING_NEW_R8 = ("// Guest R8 lives in x9, NOT in x18 (Fase 04 / Darwin).\n"
"// AAPCS64 calls x18 \"The Platform Register, if needed; otherwise a Caller-saved\n"
"// register\" and advises platform-independent code to avoid it; arm64-apple-ios\n"
"// reserves it outright (\"The platforms reserve register x18. Don't use this\n"
"// register.\").  x9 is a plain caller-saved temporary this backend never used for\n"
"// anything else, so no guest register loses its home and x8 stays free as an\n"
"// emergency scratch.\n"
"// Because x9 is caller-saved, guest R8 is now spilled/reloaded explicitly at every\n"
"// boundary that can reach code without a \"preserves guest registers\" contract:\n"
"// call_c/call_d already do it for every guest GPR, and READFLAGS/GRABFLAGS,\n"
"// flagsCacheTransform and checkCRC now do it for R8.\n"
"// This mapping is platform independent on purpose: Linux, Android and Darwin keep\n"
"// ONE register model (no per-platform #ifdef); x18 is simply unused except as the\n"
"// Windows TEB in the _WIN32 paths (see xPLATFORM below).\n"
"#define xR8     9")

MAPPING_OLD_TONAT = ("// convert a x86 register to native according to the register mapping\n"
"#define TO_NAT(A) (xRAX + (A))\n"
"#define IS_GPR(A) ((A)>=xRAX && (A)<=xRIP)")

MAPPING_NEW_TONAT = (
"// Host platform register.  NOT a guest register: x18 holds the Windows TEB under\n"
"// _WIN32 (win64_teb), is unused on Linux/Android, and is reserved by\n"
"// arm64-apple-ios.\n"
"#define xPLATFORM 18\n"
"\n"
"// convert a x86 register to native according to the register mapping.\n"
"// Table form, like the RV64/LA64 backends: with guest R8 moved out of the\n"
"// xRAX..xRIP range the mapping is no longer an affine function of the guest\n"
"// register number, so xRAX+(A) cannot express it any more.  Unchanged entries are\n"
"// spelled with their names so the table stays readable and checkable entry by\n"
"// entry.  (Generated from the #defines above - do not hand-edit.)\n"
"#define TO_NAT(A) (((uint8_t[]) { " + TONAT_LIST + " })[(A)])\n"
"// Guest GPR slots by host register number (bitmap, same reason as TO_NAT).\n"
"// Generated from the same data, so it cannot drift:\n"
"//    0.. 8 = 0 (not guest)      9 = 1 (xR8, moved here by Strategy A)\n"
"//   10..17 = 1 (xRAX..xRDI)    18 = 0 (xPLATFORM, NOT a guest register)\n"
"//   19..25 = 1 (xR9..xR15)     26,27 = 1 (xFlags, xRIP)   28..31 = 0\n"
"#define IS_GPR(A) (((uint8_t[]) { \\\n"
"    /*  0.. 7 */ " + GPR_ROWS[0] + " \\\n"
"    /*  8..15 */ " + GPR_ROWS[1] + " \\\n"
"    /* 16..23 */ " + GPR_ROWS[2] + " \\\n"
"    /* 24..31 */ " + GPR_ROWS[3] + " \\\n"
"    })[(A)])")

edit("src/dynarec/arm64/arm64_mapping.h", MAPPING_OLD_R8, MAPPING_NEW_R8)
edit("src/dynarec/arm64/arm64_mapping.h", MAPPING_OLD_TONAT, MAPPING_NEW_TONAT)

# ---------------------------------------------------------------------------
# 2. dynarec_arm64_helper.c
# ---------------------------------------------------------------------------
# 2a. _WIN32 TEB loads keep writing the *platform* register.
edit("src/dynarec/arm64/dynarec_arm64_helper.c",
     "    LDRx_U12(xR8, xEmu, offsetof(x64emu_t, win64_teb));",
     "    // x18 is the platform register here (Windows TEB), not guest R8: use xPLATFORM\n"
     "    LDRx_U12(xPLATFORM, xEmu, offsetof(x64emu_t, win64_teb));",
     count=2)

# 2b. checkCRC: mandatory spill.  Its callees (arm64_crc / arm64_x31_hash in
#     arm64_lock.S and the arm64_next_invalid trampoline) are reached through the
#     platform ABI, so guest R8 must be in memory across them.
edit("src/dynarec/arm64/dynarec_arm64_helper.c",
     "    // move away xEMU to X6, should be safe there\n    MOVx_REG(x6, xEmu);",
     "    // move away xEMU to X6, should be safe there\n"
     "    MOVx_REG(x6, xEmu);\n"
     "    // guest R8 is in a caller-saved host register (x9) and the crc/hash helpers are\n"
     "    // reached through the platform ABI, so spill it before xEmu is reused as the\n"
     "    // first argument, and reload it once xEmu is back (Strategy A, mandatory)\n"
     "    STRx_U12(xR8, xEmu, offsetof(x64emu_t, regs[_R8]));")

edit("src/dynarec/arm64/dynarec_arm64_helper.c",
     "    // done, move away result and restore xEmu\n    MOVw_REG(x1, xEmu);\n    MOVx_REG(xEmu, x6);",
     "    // done, move away result and restore xEmu\n"
     "    MOVw_REG(x1, xEmu);\n"
     "    MOVx_REG(xEmu, x6);\n"
     "    // restore guest R8 now that xEmu is valid again (Strategy A, mandatory)\n"
     "    LDRx_U12(xR8, xEmu, offsetof(x64emu_t, regs[_R8]));")

# 2c. flagsCacheTransform: defensive spill around the generated flags block.
edit("src/dynarec/arm64/dynarec_arm64_helper.c",
     "        if(dyn->insts[ninst].need_nat_flags)\n            MRS_nzcv(x6);\n"
     "        TABLE64C(x1, const_updateflags_arm64);\n        BLR(x1);",
     "        if(dyn->insts[ninst].need_nat_flags)\n            MRS_nzcv(x6);\n"
     "        // defensive spill (Strategy A): keeps the invariant \"guest R8 is explicit at\n"
     "        // every cross-code-unit boundary\" even though the generated flags block\n"
     "        // measurably only writes x1..x5/xFlags\n"
     "        STRx_U12(xR8, xEmu, offsetof(x64emu_t, regs[_R8]));\n"
     "        TABLE64C(x1, const_updateflags_arm64);\n        BLR(x1);\n"
     "        LDRx_U12(xR8, xEmu, offsetof(x64emu_t, regs[_R8]));")

# ---------------------------------------------------------------------------
# 3. dynarec_arm64_helper.h : READFLAGS / GRABFLAGS (defensive)
# ---------------------------------------------------------------------------
edit("src/dynarec/arm64/dynarec_arm64_helper.h",
     "#define READFLAGS(A) \\\n"
     "    if((A)!=X_PEND                                          \\\n"
     "    && (dyn->f==status_unk)) {                              \\\n"
     "        TABLE64C(x6, const_updateflags_arm64);              \\\n"
     "        BLR(x6);                                            \\\n"
     "        dyn->f = status_none;                               \\\n"
     "    } else if((A)==X_ALL) flushNative(dyn, ninst);",
     "#define READFLAGS(A) \\\n"
     "    if((A)!=X_PEND                                          \\\n"
     "    && (dyn->f==status_unk)) {                              \\\n"
     "        /* defensive spill: guest R8 is in caller-saved x9 */ \\\n"
     "        STRx_U12(xR8, xEmu, offsetof(x64emu_t, regs[_R8])); \\\n"
     "        TABLE64C(x6, const_updateflags_arm64);              \\\n"
     "        BLR(x6);                                            \\\n"
     "        LDRx_U12(xR8, xEmu, offsetof(x64emu_t, regs[_R8])); \\\n"
     "        dyn->f = status_none;                               \\\n"
     "    } else if((A)==X_ALL) flushNative(dyn, ninst);")

edit("src/dynarec/arm64/dynarec_arm64_helper.h",
     "#define GRABFLAGS(A) \\\n"
     "    if((A)!=X_PEND                                          \\\n"
     "    && ((dyn->f==status_unk) || (dyn->f==status_set))) {    \\\n"
     "        TABLE64C(x6, const_updateflags_arm64);              \\\n"
     "        BLR(x6);                                            \\\n"
     "        dyn->f = status_none;                               \\\n"
     "    }",
     "#define GRABFLAGS(A) \\\n"
     "    if((A)!=X_PEND                                          \\\n"
     "    && ((dyn->f==status_unk) || (dyn->f==status_set))) {    \\\n"
     "        /* defensive spill: guest R8 is in caller-saved x9 */ \\\n"
     "        STRx_U12(xR8, xEmu, offsetof(x64emu_t, regs[_R8])); \\\n"
     "        TABLE64C(x6, const_updateflags_arm64);              \\\n"
     "        BLR(x6);                                            \\\n"
     "        LDRx_U12(xR8, xEmu, offsetof(x64emu_t, regs[_R8])); \\\n"
     "        dyn->f = status_none;                               \\\n"
     "    }")

# ---------------------------------------------------------------------------
# 4. dynarec_arm64_functions.c : disassembly annotation table (must not lie)
# ---------------------------------------------------------------------------
edit("src/dynarec/arm64/dynarec_arm64_functions.c",
     '    { "r8", "x18" },\n    { "r8d", "w18" },\n    { "r8w", "x18" },\n    { "r8b", "x18" },',
     '    { "r8", "x9" },\n    { "r8d", "w9" },\n    { "r8w", "x9" },\n    { "r8b", "x9" },')

# ---------------------------------------------------------------------------
# 5. arm64_printer.c : tables are indexed by HOST register number (CRLF file)
# ---------------------------------------------------------------------------
edit("src/dynarec/arm64/arm64_printer.c", '"x8", "x9", "xRAX"', '"x8", "xR8", "xRAX"', count=2)
edit("src/dynarec/arm64/arm64_printer.c", '"xRDI", "xR8", "xR9"', '"xRDI", "xPLATFORM", "xR9"', count=2)
edit("src/dynarec/arm64/arm64_printer.c", '"w8", "w9", "wEAX"', '"w8", "wR8", "wEAX"', count=2)
edit("src/dynarec/arm64/arm64_printer.c", '"wEDI", "wR8", "wR9"', '"wEDI", "wPLATFORM", "wR9"', count=2)

# ---------------------------------------------------------------------------
# 6. the three .S files (guest R8 only; the _WIN32 x18 TEB loads are untouched)
# ---------------------------------------------------------------------------
edit("src/dynarec/arm64/arm64_prolog.S",
     r"(ldp\s+)x18(\s*,\s*x19,\s*\[x0, \(8 \*  8\)\])",
     r"\1x9\2      // guest R8 is in x9 (Strategy A); x18 stays the platform register",
     regex=True)

edit("src/dynarec/arm64/arm64_epilog.S",
     r"(stp\s+)x18(\s*,\s*x19,\s*\[x0, \(8 \*  8\)\])",
     r"\1x9\2      // guest R8 is in x9 (Strategy A)",
     regex=True)

edit("src/dynarec/arm64/arm64_next.S",
     r"(stp\s+)x18(\s*,\s*x27,\s*\[sp, \(8 \* 10\)\])",
     r"\1x9\2", count=2, regex=True)
edit("src/dynarec/arm64/arm64_next.S",
     r"(ldp\s+)x18(\s*,\s*x27,\s*\[sp, \(8 \* 10\)\])",
     r"\1x9\2", count=2, regex=True)


def check_patched_map(text):
    """Structural self-checks on the patched mapping header."""
    errs = []
    m = re.search(r"#define xR8\s+(\d+)", text)
    if not m or m.group(1) != "9":
        errs.append("xR8 does not end up as 9")
    m = re.search(r"#define xPLATFORM\s+(\d+)", text)
    if not m or m.group(1) != "18":
        errs.append("xPLATFORM does not end up as 18")
    m = re.search(r"IS_GPR\(A\) \(\(\(uint8_t\[\]\) \{(.*?)\}\)", text, re.S)
    if not m:
        errs.append("IS_GPR bitmap not found")
    else:
        body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)   # drop the row comments
        vals = [int(v) for v in re.findall(r"\b\d+\b", body)]
        if len(vals) != 32:
            errs.append("IS_GPR bitmap has %d entries, expected 32" % len(vals))
        elif vals != gpr_bitmap:
            errs.append("IS_GPR bitmap content does not match the mapping")
    m = re.search(r"TO_NAT\(A\) \(\(\(uint8_t\[\]\) \{(.*?)\}\)", text, re.S)
    if not m:
        errs.append("TO_NAT table not found")
    else:
        names = re.findall(r"xR[A-Z0-9]+", m.group(1))
        if names != ORDER:
            errs.append("TO_NAT table is %s, expected %s" % (names, ORDER))
    for name in list(HOST) + ["xPLATFORM", "xEmu", "x1", "x2", "x3", "x4", "x5",
                              "x6", "x87pc", "xFlags", "xRIP", "xSavedSP"]:
        if not re.search(r"#define %s\s+\d+" % re.escape(name), text):
            errs.append("mapping: %s missing after patch" % name)
    return errs


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    if not os.path.isdir(os.path.join(root, "src/dynarec/arm64")):
        print("ERROR: %s does not look like a box64 source root" % root)
        return 2

    # ---- validate every anchor first: all or nothing ------------------------
    contents = {}
    for rel, is_re, old, new, count in EDITS:
        path = os.path.join(root, rel)
        if path not in contents:
            with open(path, "r", encoding="utf-8", newline="") as f:
                contents[path] = f.read()
        data = contents[path]
        n = len(re.findall(old, data)) if is_re else data.count(old)
        if n != count:
            FAILED.append("%s: expected %d match(es), found %d for %r"
                          % (rel, count, n, old[:60]))

    # ---- simulate the mapping patch and run the structural self-checks -----
    if not FAILED:
        mp = os.path.join(root, "src/dynarec/arm64/arm64_mapping.h")
        sim = contents[mp].replace(MAPPING_OLD_R8, MAPPING_NEW_R8, 1)
        sim = sim.replace(MAPPING_OLD_TONAT, MAPPING_NEW_TONAT, 1)
        FAILED.extend(check_patched_map(sim))

    if FAILED:
        print("ABORTED, no file written:")
        for f in FAILED:
            print("  - " + f)
        return 1

    # ---- apply -------------------------------------------------------------
    for rel, is_re, old, new, count in EDITS:
        path = os.path.join(root, rel)
        data = contents[path]
        if is_re:
            data, n = re.subn(old, new, data, count=count)
        else:
            data = data.replace(old, new, count)
            n = count
        contents[path] = data
        print("patched %-46s %d edit(s)" % (rel, n))

    # ---- verify in the produced text --------------------------------------
    errs = []
    for path, data in contents.items():
        base = os.path.basename(path)
        if base == "arm64_mapping.h":
            errs.extend(check_patched_map(data))
        if base in ("arm64_prolog.S", "arm64_epilog.S", "arm64_next.S"):
            # x18 may only be EXECUTED as the _WIN32 TEB load (offset 3104);
            # comments are allowed to mention it.
            for i, line in enumerate(data.splitlines(), 1):
                code = line.split("//")[0]
                if re.search(r"\bx18\b", code) and "3104" not in code:
                    errs.append("%s:%d still uses x18 as a guest register: %s"
                                % (base, i, line.strip()))
        if base == "dynarec_arm64_helper.c":
            for i, line in enumerate(data.splitlines(), 1):
                if re.search(r"\bxR8\b", line) and "win64_teb" in line:
                    errs.append("%s:%d TEB load still uses xR8" % (base, i))
    if errs:
        print("POST-CHECK FAILED (files not written):")
        for e in errs:
            print("  - " + e)
        return 1

    for path, data in contents.items():
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(data)
    print("OK: Strategy A applied and verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
