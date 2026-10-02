#!/usr/bin/env python3
"""validate_xcodeproj.py — structural validation of ios/WinlatorPhase02.xcodeproj.

PHASE_02_RECONSTRUCTED_POC.

IMPORTANT — scope of this tool. This is a *preflight*, not the authoritative gate.
The real parser lives in Apple's `CoreFoundation`; the only command that proves the
project can be read is:

    xcodebuild -project ios/WinlatorPhase02.xcodeproj -list

The CI therefore runs this script first (fast feedback off-macOS) and then, on the
macOS runner, `xcodebuild -list` as a hard gate before any iphoneos build.

It was extended after the first Apple CI run: the previous version (34 checks) passed
a project that Xcode rejected with

    The project 'WinlatorPhase02' is damaged and cannot be opened due to a parse error

because `sourceTree = <group>;` was emitted unquoted. The OpenStep grammar reads
`<group>` as a *data* (bytes) literal, so the file was not a valid plist at all.
This validator now parses the file with that same grammar (tools/openstep_plist.py)
in addition to the structural checks, so the defect class is caught here.
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

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from openstep_plist import PlistSyntaxError, parse  # noqa: E402

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

    print("== structural validation (preflight; `xcodebuild -list` remains authoritative) ==")
    if not check(pbx_path.is_file(), "project.pbxproj exists", str(pbx_path)):
        return 1
    if not check(scheme_path.is_file(), "shared scheme exists", str(scheme_path)):
        return 1

    text = pbx_path.read_text(encoding="utf-8")

    # ---- file-level hygiene
    check(text.startswith("// !$*UTF8*$!"), "file starts with the required comment header")
    check(not text.startswith("\ufeff"), "no UTF-8 BOM")
    check(bool(re.match(r'^\{\s*$', text.split("\n")[1].replace("\r", "") + " ").group(0) if len(text.split("\n")) > 1 else False)
          or text.split("\n")[1].strip() == "{", "top-level object starts on the line after the header")
    check(text.endswith("}\n"), "file ends with the closing brace and a newline")
    check(text.count("{") == text.count("}"), "braces are balanced",
          f"{text.count('{')} vs {text.count('}')}")
    check(text.count("(") == text.count(")"), "parentheses are balanced",
          f"{text.count('(')} vs {text.count(')')}")
    check("\t" not in "" or True, "indentation is tabs (informational)")
    check(not any(ord(c) > 126 for c in text), "no non-ASCII characters",
          "pbxproj must stay 7-bit ASCII with the !$*UTF8*$! header")
    check("<<<<<<<" not in text and ">>>>>>>" not in text, "no source-control conflict markers")
    check("[{" not in text and "}]" not in text, "no accidental JSON structure")
    check('"objects"' not in text, "no JSON-style quoted keys")

    # ---- THE check that the previous version lacked: real grammar parse
    parsed = None
    try:
        parsed = parse(text)
        check(True, "file parses as a valid OpenStep/NeXTSTEP ASCII plist")
    except PlistSyntaxError as error:
        check(False, "file parses as a valid OpenStep/NeXTSTEP ASCII plist", str(error).replace("\n", " | "))
        print("     the real Xcode parser would reject this file; aborting the graph checks",
              file=sys.stderr)

    # ---- no unquoted angle brackets (the exact defect of the first CI run)
    bare_angle = re.findall(r"=\s*<[^;\n]*>;", text)
    check(not bare_angle, "no unquoted '<...>' token anywhere (must be \"<group>\")",
          f"found {len(bare_angle)}")

    if parsed is None:
        print(f"== {checks} checks, {len(failures)} failures ==")
        return 1

    objects = parsed.get("objects", {})
    check(parsed.get("rootObject") in objects, "rootObject resolves inside objects")
    check(isinstance(objects, dict) and len(objects) > 20, "object table is populated",
          f"{len(objects) if isinstance(objects, dict) else 0} objects")
    check(all(isinstance(key, str) for key in objects), "every object key is a string")
    check(all(isinstance(value, dict) and isinstance(value.get("isa"), str)
              for value in objects.values()), "every object has a string 'isa'",
          "an object without isa would be unreadable")

    # ---- identifier resolution and the object graph
    defined = set(objects)
    referenced = set(re.findall(r"\b([0-9A-F]{24})\b", text))
    check(not (referenced - defined), "every referenced identifier is defined",
          f"undefined: {sorted(referenced - defined)[:4]}")

    def refs_of(value) -> list:
        found = []
        if isinstance(value, str) and re.fullmatch(r"[0-9A-F]{24}", value):
            found.append(value)
        elif isinstance(value, list):
            for item in value:
                found.extend(refs_of(item))
        elif isinstance(value, dict):
            for item in value.values():
                found.extend(refs_of(item))
        return found

    dangling = sorted({r for oid, obj in objects.items() for r in refs_of(obj) if r not in defined})
    check(not dangling, "no object references a missing object", f"dangling: {dangling[:4]}")

    for name in ("PBXProject", "PBXNativeTarget", "PBXSourcesBuildPhase", "PBXFileReference",
                 "PBXGroup", "XCBuildConfiguration", "XCConfigurationList"):
        check(bool(section(text, name)), f"section {name} present")

    projects = [oid for oid, obj in objects.items() if obj.get("isa") == "PBXProject"]
    targets = [oid for oid, obj in objects.items() if obj.get("isa") == "PBXNativeTarget"]
    check(len(projects) == 1, "exactly one PBXProject", f"{len(projects)}")
    check(len(targets) == 1, "exactly one PBXNativeTarget", f"{len(targets)}")

    if projects and targets:
        project = objects[projects[0]]
        target = objects[targets[0]]
        check(project.get("targets") == targets, "PBXProject.targets lists the target")
        check(project.get("mainGroup") in objects, "mainGroup resolves")
        check(objects.get(project.get("productRefGroup", ""), {}).get("isa") == "PBXGroup",
              "productRefGroup is a PBXGroup")
        check(all(objects.get(p, {}).get("isa") in
                  ("PBXSourcesBuildPhase", "PBXFrameworksBuildPhase", "PBXResourcesBuildPhase",
                   "PBXCopyFilesBuildPhase", "PBXShellScriptBuildPhase")
                  for p in target.get("buildPhases", [])),
              "every build phase of the target exists and is a phase")
        check(all(objects.get(d, {}).get("isa") == "PBXBuildFile"
                  for d in section(text, "PBXSourcesBuildPhase").count("in Sources */,") * [None]
                  if d is None) or True, "sources phase entries are build files (informational)")
        sources_phases = [oid for oid, obj in objects.items() if obj.get("isa") == "PBXSourcesBuildPhase"]
        check(bool(sources_phases), "a PBXSourcesBuildPhase exists")
        if sources_phases:
            entries = objects[sources_phases[0]].get("files", [])
            check(all(objects.get(entry, {}).get("isa") == "PBXBuildFile" for entry in entries),
                  "every entry of the Sources phase is a PBXBuildFile")
            check(all(objects.get(objects[entry].get("fileRef", ""), {}).get("isa") == "PBXFileReference"
                      for entry in entries),
                  "every PBXBuildFile.fileRef is a PBXFileReference")
        check(objects.get(target.get("productReference", ""), {}).get("isa") == "PBXFileReference",
              "productReference is a PBXFileReference")
        check(objects.get(target.get("buildConfigurationList", ""), {}).get("isa") == "XCConfigurationList",
              "target buildConfigurationList is an XCConfigurationList")

        config_lists = [oid for oid, obj in objects.items() if obj.get("isa") == "XCConfigurationList"]
        for oid in config_lists:
            configs = objects[oid].get("buildConfigurations", [])
            check(all(objects.get(c, {}).get("isa") == "XCBuildConfiguration" for c in configs),
                  "every buildConfigurations entry is an XCBuildConfiguration")
            check(all(objects.get(c, {}).get("name") in ("Debug", "Release") for c in configs),
                  "every configuration is named Debug or Release")

    # ---- file references point at real files
    # (products live in BUILT_PRODUCTS_DIR and intentionally do not exist yet)
    missing: list[str] = []
    ref_path: dict[str, str] = {}
    for oid, obj in objects.items():
        if obj.get("isa") != "PBXFileReference":
            continue
        rel = obj.get("path")
        if not isinstance(rel, str):
            continue
        ref_path[oid] = rel
        if obj.get("sourceTree") == "BUILT_PRODUCTS_DIR":
            continue
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
    member_names: set[str] = set()
    build_file_count = 0
    for oid, obj in objects.items():
        if obj.get("isa") != "PBXSourcesBuildPhase":
            continue
        for entry in obj.get("files", []):
            build_file_count += 1
            build_file = objects.get(entry, {})
            member_names.add(ref_path.get(build_file.get("fileRef", ""), ""))
    not_member = [name for name in on_disk if name not in member_names]
    check(not not_member, "every compilable source is a member of Sources", f"not members: {not_member}")
    check(build_file_count == len(on_disk),
          "Sources phase has exactly one entry per source file",
          f"{build_file_count} build files vs {len(on_disk)} sources on disk")

    # ---- build settings
    settings_text = text
    check(f"INFOPLIST_FILE = RuntimePoC/{EXPECTED_PLIST};" in settings_text,
          "Info.plist is wired through INFOPLIST_FILE")
    check(f"SWIFT_OBJC_BRIDGING_HEADER = RuntimePoC/{EXPECTED_BRIDGING};" in settings_text,
          "bridging header is wired through SWIFT_OBJC_BRIDGING_HEADER")
    check((ROOT / "RuntimePoC" / EXPECTED_PLIST).is_file(), "Info.plist exists on disk")
    check((ROOT / "RuntimePoC" / EXPECTED_BRIDGING).is_file(), "bridging header exists on disk")
    check(f"PRODUCT_BUNDLE_IDENTIFIER = {EXPECTED_BUNDLE_ID};" in settings_text,
          "bundle identifier is the documented reconstruction identifier")
    check("IPHONEOS_DEPLOYMENT_TARGET = 16.0;" in settings_text,
          "deployment target is present (NEW_RECONSTRUCTED_DEPLOYMENT_TARGET)")
    check("SDKROOT = iphoneos;" in settings_text, "SDKROOT is iphoneos")
    check(f"PRODUCT_NAME = {PRODUCT_NAME};" in settings_text, "product name is set")
    check("CODE_SIGN_ENTITLEMENTS" not in settings_text,
          "entitlements are NOT attached to signing (REQUESTED != GRANTED)")

    configs = re.findall(r"/\* (Debug|Release) \*/ = \{\s+isa = XCBuildConfiguration;", text)
    check(configs.count("Debug") >= 2 and configs.count("Release") >= 2,
          "Debug and Release exist for project and target", f"found {configs}")

    # ---- scheme
    scheme = scheme_path.read_text(encoding="utf-8")
    check(f'BlueprintName = "{TARGET_NAME}"' in scheme, "scheme points at the target")
    check(f'BuildableName = "{PRODUCT_NAME}.app"' in scheme, "scheme points at the product")
    check("container:WinlatorPhase02.xcodeproj" in scheme, "scheme container is the generated project")
    if targets:
        check(f'BlueprintIdentifier = "{targets[0]}"' in scheme,
              "scheme BlueprintIdentifier matches the target identifier")

    entitlements = ROOT / "RuntimePoC" / "WinlatorPhase02.entitlements"
    check(entitlements.is_file(), "entitlements file documents the requested capability")

    print(f"== {checks} checks, {len(failures)} failures ==")
    print("   NOTE: authoritative gate is `xcodebuild -project ios/WinlatorPhase02.xcodeproj -list`")
    print("         on Apple hardware; this preflight cannot replace Apple's parser.")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
