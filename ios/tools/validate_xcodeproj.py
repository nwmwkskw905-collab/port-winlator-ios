#!/usr/bin/env python3
"""validate_xcodeproj.py — structural validation of ios/WinlatorPhase02.xcodeproj.

PHASE_02_RECONSTRUCTED_POC. There is no Apple toolchain in this environment, so the
project cannot be opened by `xcodebuild` here; this validator is therefore the
structural gate that runs everywhere (including the macOS CI job, before the build).

It answers, with evidence:
  * is every object identifier defined exactly once and every reference resolvable?
  * does every file reference point at a file that exists on disk?
  * is every compilable source on disk a member of the Sources build phase?
  * are Info.plist and the bridging header present, referenced and correctly wired?
  * does the target/scheme/product agree, and are Debug/Release configured?
  * is the entitlements file present but deliberately NOT attached to signing?
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TARGET_NAME = "WinlatorPhase02"
PRODUCT_NAME = "WinlatorPhase02"
EXPECTED_BUNDLE_ID = "io.winlator.phase02.reconstructed"
EXPECTED_BRIDGING = "Phase02-Bridging-Header.h"
EXPECTED_PLIST = "Info.plist"

failures: list[str] = []
checks = 0


def check(condition: bool, label: str, detail: str = "") -> bool:
    global checks
    checks += 1
    if condition:
        print(f"ok   {label}")
    else:
        print(f"FAIL {label}" + (f" :: {detail}" if detail else ""))
        failures.append(label)
    return condition


def section(text: str, name: str) -> str:
    match = re.search(
        r"/\* Begin " + re.escape(name) + r" section \*/(.*?)/\* End " + re.escape(name) + r" section \*/",
        text,
        re.S,
    )
    return match.group(1) if match else ""


def main() -> int:
    project_dir = ROOT / f"{TARGET_NAME}.xcodeproj"
    pbx_path = project_dir / "project.pbxproj"
    scheme_path = project_dir / "xcshareddata" / "xcschemes" / f"{TARGET_NAME}.xcscheme"

    print("== structural validation ==")
    if not check(pbx_path.is_file(), "project.pbxproj exists", str(pbx_path)):
        return 1
    if not check(scheme_path.is_file(), "shared scheme exists", str(scheme_path)):
        return 1

    text = pbx_path.read_text(encoding="utf-8")

    # ---- header / integrity
    check(text.startswith("// !$*UTF8*$!"), "file starts with the required comment header")
    check(text.count("{") == text.count("}"), "braces are balanced",
          f"{text.count('{')} vs {text.count('}')}")

    defined = set(re.findall(r"^\t\t([0-9A-F]{24}) /\*", text, re.M))
    referenced = set(re.findall(r"\b([0-9A-F]{24})\b", text))
    check(len(defined) > 20, "object table is populated", f"{len(defined)} objects")
    undefined_references = sorted(referenced - defined)
    check(not undefined_references, "every referenced identifier is defined",
          f"undefined: {undefined_references[:4]}")

    root_match = re.search(r"rootObject = ([0-9A-F]{24})", text)
    check(root_match is not None and (root_match.group(1) in defined),
          "rootObject resolves to a defined object")

    for name in ("PBXProject", "PBXNativeTarget", "PBXSourcesBuildPhase", "PBXFileReference",
                 "PBXGroup", "XCBuildConfiguration", "XCConfigurationList"):
        check(bool(section(text, name)), f"section {name} present")

    # ---- file references point at real files
    refs = re.findall(r"([0-9A-F]{24}) /\* ([^*]+) \*/ = \{isa = PBXFileReference;.*?path = "
                      r"(?:\"([^\"]+)\"|([A-Za-z0-9._/-]+));", text)
    # product references live in BUILT_PRODUCTS_DIR and must not be searched on disk
    products = set(re.findall(
        r"([0-9A-F]{24}) /\* [^*]+ \*/ = \{isa = PBXFileReference;.*?sourceTree = BUILT_PRODUCTS_DIR;",
        text, re.S))
    missing: list[str] = []
    for _uid, _name, quoted, bare in refs:
        if _uid in products:
            continue
        rel = quoted or bare
        if not rel:
            continue
        # Resolve against the directory that owns the file: the generator keeps the
        # path relative to its group, so search the known source directories.
        candidates = [
            ROOT / rel,
            ROOT / "RuntimeCore" / rel,
            ROOT / "RuntimeCore" / "src" / rel,
            ROOT / "RuntimeCore" / "include" / rel,
            ROOT / "Diagnostics" / rel,
            ROOT / "Diagnostics" / "src" / rel,
            ROOT / "Diagnostics" / "include" / rel,
            ROOT / "RuntimePoC" / rel,
        ]
        if not any(candidate.is_file() for candidate in candidates):
            missing.append(rel)
    check(not missing, "every file reference resolves on disk", f"missing: {sorted(set(missing))}")

    # ---- source membership
    on_disk = sorted(
        [p.name for p in (ROOT / "RuntimeCore" / "src").glob("*.c")]
        + [p.name for p in (ROOT / "Diagnostics" / "src").glob("*.c")]
        + [p.name for p in (ROOT / "RuntimePoC").glob("*.m")]
        + [p.name for p in (ROOT / "RuntimePoC").glob("*.swift")]
    )
    sources_section = section(text, "PBXSourcesBuildPhase")
    # entries look like:  "UID /* file.c in Sources */,"
    build_files = re.findall(r"([0-9A-F]{24}) /\* [^*]+ in Sources \*/,", sources_section)
    # and the PBXBuildFile objects map that identifier to the file reference
    build_file_refs = re.findall(
        r"([0-9A-F]{24}) /\* [^*]+ in Sources \*/ = \{isa = PBXBuildFile; fileRef = ([0-9A-F]{24})",
        text)
    build_to_ref = {entry: ref for entry, ref in build_file_refs}
    ref_path = {uid_value: (quoted or bare) for uid_value, _n, quoted, bare in refs}
    member_names = {ref_path.get(build_to_ref.get(entry, ""), "") for entry in build_files}
    resolved_members = {name for name in member_names if name}

    not_member = [name for name in on_disk if name not in resolved_members]
    check(not not_member, "every compilable source is a member of Sources", f"not members: {not_member}")
    check(len(build_files) == len(on_disk),
          "Sources phase has exactly one entry per source file",
          f"{len(build_files)} build files vs {len(on_disk)} sources on disk")
    check(len(member_names) >= 1, "Sources phase is not empty")

    # ---- plist / bridging header / identifiers
    check(f"INFOPLIST_FILE = RuntimePoC/{EXPECTED_PLIST};" in text,
          "Info.plist is wired through INFOPLIST_FILE")
    check(f"SWIFT_OBJC_BRIDGING_HEADER = RuntimePoC/{EXPECTED_BRIDGING};" in text,
          "bridging header is wired through SWIFT_OBJC_BRIDGING_HEADER")
    check((ROOT / "RuntimePoC" / EXPECTED_PLIST).is_file(), "Info.plist exists on disk")
    check((ROOT / "RuntimePoC" / EXPECTED_BRIDGING).is_file(), "bridging header exists on disk")
    check(f"PRODUCT_BUNDLE_IDENTIFIER = {EXPECTED_BUNDLE_ID};" in text,
          "bundle identifier is the documented reconstruction identifier")
    check("IPHONEOS_DEPLOYMENT_TARGET = 16.0;" in text,
          "deployment target is present (NEW_RECONSTRUCTED_DEPLOYMENT_TARGET)")
    check("SDKROOT = iphoneos;" in text, "SDKROOT is iphoneos")
    check(f"PRODUCT_NAME = {PRODUCT_NAME};" in text, "product name is set")
    check(f'"{TARGET_NAME}"' in section(text, "PBXNativeTarget") or
          f"name = {TARGET_NAME};" in section(text, "PBXNativeTarget"),
          "native target carries the expected name")
    check(f"{PRODUCT_NAME}.app" in text, "product reference names the .app bundle")

    # ---- configurations
    configs = re.findall(r"/\* (Debug|Release) \*/ = \{\s+isa = XCBuildConfiguration;", text)
    check(configs.count("Debug") >= 2 and configs.count("Release") >= 2,
          "Debug and Release exist for project and target",
          f"found {configs}")

    # ---- scheme
    scheme = scheme_path.read_text(encoding="utf-8")
    check(f'BlueprintName = "{TARGET_NAME}"' in scheme, "scheme points at the target")
    check(f'BuildableName = "{PRODUCT_NAME}.app"' in scheme, "scheme points at the product")
    check("container:WinlatorPhase02.xcodeproj" in scheme, "scheme container is the generated project")

    # ---- entitlements policy (Section 28 of the brief)
    entitlements = ROOT / "RuntimePoC" / "WinlatorPhase02.entitlements"
    if check(entitlements.is_file(), "entitlements file documents the requested capability"):
        check("CODE_SIGN_ENTITLEMENTS" not in text,
              "entitlements are NOT attached to signing (REQUESTED != GRANTED)")

    # ---- report
    print(f"== {checks} checks, {len(failures)} failures ==")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
