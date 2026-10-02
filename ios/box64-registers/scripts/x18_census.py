#!/usr/bin/env python3
"""
x18_census.py - definitive census of native register 18 in a box64 arm64 binary.

Phase 04 exists because guest x86-64 R8 lives in the host register x18
(`#define xR8 18` in src/dynarec/arm64/arm64_mapping.h) and Apple's arm64 ABI reserves
x18 as the platform register. This tool measures the size of that dependency exactly,
and - more importantly - separates the three populations that look identical under a
naive `grep x18`:

  1. GUEST_R8    : x18 used as the guest R8 mapping by box64's own dynarec code and by
                   the hand written prolog/epilog/next stubs. THIS is the port's problem.
  2. HOST_SCRATCH: x18 used as an ordinary temporary by the host C compiler (legal on
                   Linux/Android, where x18 is not reserved). On Darwin the compiler
                   cannot do this, so these sites cost nothing to port.
  3. W18         : the 32-bit view of the same register (guest R8D). Same problem as (1).

Method (each step is a measurement, not an inference):

  * disassemble with objdump and keep only real instruction lines;
  * match the register as a WORD (`\bx18\b`), never as a substring: a naive substring
    match also hits hex literals such as `#0x18`, `[x0, #0x180]`, `+0x18>` and inflates
    the census by roughly a factor of two (this is exactly how the Fase 03 report's
    "2361 functions" figure was produced - see the Fase 04 report, section 4);
  * attribute every instruction to the translation unit that contains it, using the
    ELF symbol table: `STT_FILE` entries partition the symbol list, so each `STT_FUNC`
    symbol belongs to the FILE symbol that precedes it.

Usage:  x18_census.py <box64-binary> [--json <path>] [--top <n>]
Exit:   0 always (the tool reports; it does not gate).
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
from collections import defaultdict


def _tool(name):
    """Pick the cross-tool for aarch64 when the host binutils cannot handle the input.

    A host x86-64 objdump refuses an AArch64 binary outright, so the tool is selected
    explicitly rather than assumed: the choice is reported, because it is part of the
    measurement.
    """
    cross = 'aarch64-linux-gnu-' + name
    if shutil.which(cross):
        return cross
    return name

# With --no-show-raw-insn objdump prints "<address>:<tab><mnemonic><tab><operands>".
# The address is matched strictly; everything after it is the instruction text, so a
# mnemonic that happens to look like hex ("add", "cbz", ...) cannot be mistaken for bytes.
INSN_RE = re.compile(r'^\s+([0-9a-f]+):\s+(.*)$')
FUNC_HDR_RE = re.compile(r'^([0-9a-f]+) <(.+)>:$')
X18_RE = re.compile(r'\bx18\b')          # the 64-bit view: guest R8
W18_RE = re.compile(r'\bw18\b')          # the 32-bit view: guest R8D


def read_symbol_tu_map(binary):
    """Return {function_name_or_address: translation_unit} from the ELF symbol table."""
    out = subprocess.run([_tool('readelf'), '-sW', binary],
                         capture_output=True, text=True, check=True).stdout
    tu_by_addr = {}
    current_tu = None
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 8 or not parts[0].endswith(':'):
            continue
        sym_type, name = parts[3], parts[7] if len(parts) > 7 else ''
        if sym_type == 'FILE':
            current_tu = name
            continue
        if sym_type in ('FUNC', 'IFUNC') and current_tu:
            try:
                addr = int(parts[1], 16)
            except ValueError:
                continue
            if addr:
                tu_by_addr[addr] = current_tu
    return tu_by_addr


def disassemble(binary):
    return subprocess.run([_tool('objdump'), '-d', '--no-show-raw-insn', binary],
                          capture_output=True, text=True, check=True).stdout


def census(binary):
    tu_by_addr = read_symbol_tu_map(binary)
    disasm = disassemble(binary)
    current_func = None
    current_func_addr = 0
    stats = {
        'instructions_total': 0,
        'x18_instructions': 0,
        'w18_instructions': 0,
        'x18_word_and_substring_gap': 0,   # instructions where the naive substring hits
        'functions_with_x18': set(),
        'functions_with_w18': set(),
        'functions_x18_or_w18': set(),
        'x18_by_function': defaultdict(int),
        'x18_by_tu': defaultdict(int),
        'func_by_tu': defaultdict(set),
    }
    for line in disasm.splitlines():
        hdr = FUNC_HDR_RE.match(line)
        if hdr:
            current_func = hdr.group(2)
            current_func_addr = int(hdr.group(1), 16)
            continue
        m = INSN_RE.match(line)
        if not m:
            continue
        text = m.group(2)
        if not text or text[0].isdigit():
            # A continuation line of the byte dump, not an instruction.
            continue
        stats['instructions_total'] += 1
        has_x18 = bool(X18_RE.search(text))
        has_w18 = bool(W18_RE.search(text))
        if 'x18' in text and not has_x18:
            stats['x18_word_and_substring_gap'] += 1
        if has_x18:
            stats['x18_instructions'] += 1
            key = (current_func_addr, current_func)
            stats['functions_with_x18'].add(key)
            stats['x18_by_function'][current_func] += 1
            tu = tu_by_addr.get(current_func_addr, '<unknown>')
            stats['x18_by_tu'][tu] += 1
            stats['func_by_tu'][tu].add(current_func)
        if has_w18:
            stats['w18_instructions'] += 1
            stats['functions_with_w18'].add((current_func_addr, current_func))
    stats['functions_x18_or_w18'] = stats['functions_with_x18'] | stats['functions_with_w18']
    return stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary')
    ap.add_argument('--json')
    ap.add_argument('--top', type=int, default=15)
    args = ap.parse_args()

    s = census(args.binary)

    print('X18 CENSUS - %s' % args.binary)
    print('tools: %s, %s' % (_tool('objdump'), _tool('readelf')))
    print('=' * 74)
    print('instructions disassembled              : %d' % s['instructions_total'])
    print('instructions referencing x18 (word)    : %d' % s['x18_instructions'])
    print('instructions referencing w18 (word)    : %d' % s['w18_instructions'])
    print('x18+w18 references (union of lines)    : %d' % (s['x18_instructions'] + s['w18_instructions']))
    print('instructions where "x18" is only part of a hex literal: %d'
          % s['x18_word_and_substring_gap'])
    print()
    print('functions containing x18               : %d' % len(s['functions_with_x18']))
    print('functions containing w18               : %d' % len(s['functions_with_w18']))
    print('functions containing x18 or w18        : %d' % len(s['functions_x18_or_w18']))
    print()
    print('-- x18 references grouped by translation unit (top %d) --' % args.top)
    for tu, count in sorted(s['x18_by_tu'].items(), key=lambda kv: -kv[1])[:args.top]:
        print('  %6d  %-46s  (%d functions)' % (count, tu, len(s['func_by_tu'][tu])))
    print()
    print('-- functions with the most x18 references (top %d) --' % args.top)
    for name, count in sorted(s['x18_by_function'].items(), key=lambda kv: -kv[1])[:args.top]:
        print('  %6d  %s' % (count, name))

    if args.json:
        payload = {
            'binary': args.binary,
            'instructions_total': s['instructions_total'],
            'x18_instructions': s['x18_instructions'],
            'w18_instructions': s['w18_instructions'],
            'x18_substring_only': s['x18_word_and_substring_gap'],
            'functions_with_x18': len(s['functions_with_x18']),
            'functions_with_w18': len(s['functions_with_w18']),
            'functions_with_x18_or_w18': len(s['functions_x18_or_w18']),
            'x18_by_tu': dict(sorted(s['x18_by_tu'].items(), key=lambda kv: -kv[1])),
            'x18_by_function': dict(sorted(s['x18_by_function'].items(), key=lambda kv: -kv[1])),
        }
        with open(args.json, 'w') as fh:
            json.dump(payload, fh, indent=2, sort_keys=False)
        print('\njson written to %s' % args.json)
    return 0


if __name__ == '__main__':
    sys.exit(main())
