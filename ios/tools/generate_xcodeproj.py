#!/usr/bin/env python3
"""generate_xcodeproj.py — generate ios/WinlatorPhase02.xcodeproj from the tree.

PHASE_02_RECONSTRUCTED_POC. The surviving ios/README.md states that the Xcode
project was *generated* rather than hand-maintained ("generado por
tools/generate_xcodeproj.py"); this is the reconstruction of that generator.

The output is deterministic: every object identifier is an md5-derived 24-hex
string of a stable label, so regenerating on another machine produces the same
project file. Nothing is invented: the file lists come from what actually exists
on disk, and the script refuses to run if a required source is missing.
"""

from __future__ import annotations

import hashlib
import os
import sys
from pathlib import Path

TARGET_NAME = "WinlatorPhase02"
PRODUCT_NAME = "WinlatorPhase02"
BUNDLE_IDENTIFIER = "io.winlator.phase02.reconstructed"  # NEW_RECONSTRUCTED_BUNDLE_IDENTIFIER
DEPLOYMENT_TARGET = "16.0"                                # NEW_RECONSTRUCTED_DEPLOYMENT_TARGET
SWIFT_VERSION = "5.0"

# Same warning policy as ios/CMakeLists.txt (Section 23 of the reconstruction brief).
STRICT_WARNINGS = [
    "-Wall", "-Wextra", "-Wpedantic", "-Wshadow", "-Wconversion",
    "-Wsign-conversion", "-Wpointer-arith", "-Wcast-align",
    "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wno-unused-parameter",
]

RUNTIME_CORE = "RuntimeCore"
DIAGNOSTICS = "Diagnostics"
RUNTIME_POC = "RuntimePoC"

PLIST_NAME = "Info.plist"
BRIDGING_HEADER = "Phase02-Bridging-Header.h"


def uid(label: str) -> str:
    """Deterministic 24-hex object identifier."""
    return hashlib.md5(label.encode("utf-8")).hexdigest()[:24].upper()


def collect_files(root: Path) -> dict:
    """Discover the sources that actually exist; fail loudly when one is missing."""
    core_src = sorted(p.name for p in (root / RUNTIME_CORE / "src").glob("*.c"))
    core_inc = sorted(p.name for p in (root / RUNTIME_CORE / "include").glob("*.h"))
    diag_src = sorted(p.name for p in (root / DIAGNOSTICS / "src").glob("*.c"))
    diag_inc = sorted(p.name for p in (root / DIAGNOSTICS / "include").glob("*.h"))
    objc_src = sorted(p.name for p in (root / RUNTIME_POC).glob("*.m"))
    swift_src = sorted(p.name for p in (root / RUNTIME_POC).glob("*.swift"))

    problems = []
    if not core_src:
        problems.append(f"no C sources under {RUNTIME_CORE}/src")
    if not diag_src:
        problems.append(f"no C sources under {DIAGNOSTICS}/src")
    if not objc_src:
        problems.append(f"no Objective-C bridge under {RUNTIME_POC}")
    if not swift_src:
        problems.append(f"no Swift sources under {RUNTIME_POC}")
    if not (root / RUNTIME_POC / PLIST_NAME).is_file():
        problems.append(f"missing {RUNTIME_POC}/{PLIST_NAME}")
    if not (root / RUNTIME_POC / BRIDGING_HEADER).is_file():
        problems.append(f"missing {RUNTIME_POC}/{BRIDGING_HEADER}")
    if problems:
        for issue in problems:
            print(f"generate_xcodeproj: ERROR: {issue}", file=sys.stderr)
        raise SystemExit(2)

    return {
        "core_src": core_src,
        "core_inc": core_inc,
        "diag_src": diag_src,
        "diag_inc": diag_inc,
        "objc_src": objc_src,
        "swift_src": swift_src,
    }


def build_objects(files: dict) -> str:
    lines = []
    add = lines.append

    # ---- identifier map
    project_uid = uid("project")
    target_uid = uid("target")
    product_uid = uid("product")
    main_group_uid = uid("group:main")
    products_group_uid = uid("group:products")
    frameworks_phase_uid = uid("phase:frameworks")
    sources_phase_uid = uid("phase:sources")
    resources_phase_uid = uid("phase:resources")
    project_config_list_uid = uid("configlist:project")
    target_config_list_uid = uid("configlist:target")
    project_debug_uid = uid("config:project:debug")
    project_release_uid = uid("config:project:release")
    target_debug_uid = uid("config:target:debug")
    target_release_uid = uid("config:target:release")

    def file_ref(uid_value: str, name: str, path: str, filetype: str, source_tree: str = "<group>") -> str:
        return (
            f"\t\t{uid_value} /* {name} */ = {{isa = PBXFileReference; "
            f"fileEncoding = 4; lastKnownFileType = {filetype}; path = {quote(path)}; "
            f"sourceTree = {source_tree}; }};"
        )

    def build_file(uid_value: str, ref_uid: str, name: str) -> str:
        return (f"\t\t{uid_value} /* {name} in Sources */ = {{isa = PBXBuildFile; "
                f"fileRef = {ref_uid} /* {name} */; }};")

    def quote(value: str) -> str:
        if value and all(c.isalnum() or c in "._/-" for c in value):
            return value
        return '"' + value.replace('"', '\\"') + '"'

    file_refs = []
    group_children = {}

    def add_source(group_key: str, name: str, path: str, filetype: str, dot_path: str) -> str:
        ref = uid(f"fileref:{dot_path}")
        file_refs.append(file_ref(ref, name, path, filetype))
        group_children.setdefault(group_key, []).append(ref)
        return ref

    # RuntimeCore
    core_group = uid("group:RuntimeCore")
    core_inc_group = uid("group:RuntimeCore/include")
    core_src_group = uid("group:RuntimeCore/src")
    for name in files["core_inc"]:
        add_source(core_inc_group, name, name, "sourcecode.c.h", f"{RUNTIME_CORE}/include/{name}")
    for name in files["core_src"]:
        add_source(core_src_group, name, name, "sourcecode.c.c", f"{RUNTIME_CORE}/src/{name}")

    # Diagnostics
    diag_group = uid("group:Diagnostics")
    diag_inc_group = uid("group:Diagnostics/include")
    diag_src_group = uid("group:Diagnostics/src")
    for name in files["diag_inc"]:
        add_source(diag_inc_group, name, name, "sourcecode.c.h", f"{DIAGNOSTICS}/include/{name}")
    for name in files["diag_src"]:
        add_source(diag_src_group, name, name, "sourcecode.c.c", f"{DIAGNOSTICS}/src/{name}")

    # RuntimePoC (app sources + plist + bridging header)
    poc_group = uid("group:RuntimePoC")
    plist_ref = add_source(poc_group, PLIST_NAME, PLIST_NAME, "text.plist.xml", f"{RUNTIME_POC}/{PLIST_NAME}")
    bridging_ref = add_source(poc_group, BRIDGING_HEADER, BRIDGING_HEADER, "sourcecode.c.h",
                              f"{RUNTIME_POC}/{BRIDGING_HEADER}")
    objc_refs = [add_source(poc_group, name, name, "sourcecode.c.objc", f"{RUNTIME_POC}/{name}")
                 for name in files["objc_src"]]
    swift_refs = [add_source(poc_group, name, name, "sourcecode.swift", f"{RUNTIME_POC}/{name}")
                  for name in files["swift_src"]]

    compiled_refs = (
        [uid(f"fileref:{RUNTIME_CORE}/src/{n}") for n in files["core_src"]]
        + [uid(f"fileref:{DIAGNOSTICS}/src/{n}") for n in files["diag_src"]]
        + objc_refs
        + swift_refs
    )

    # ---- PBXBuildFile
    ref_name = {}
    for group_key, ref_ids in group_children.items():
        del group_key
    for name in files["core_src"]:
        ref_name[uid(f"fileref:{RUNTIME_CORE}/src/{name}")] = name
    for name in files["diag_src"]:
        ref_name[uid(f"fileref:{DIAGNOSTICS}/src/{name}")] = name
    for name in files["objc_src"]:
        ref_name[uid(f"fileref:{RUNTIME_POC}/{name}")] = name
    for name in files["swift_src"]:
        ref_name[uid(f"fileref:{RUNTIME_POC}/{name}")] = name

    add("/* Begin PBXBuildFile section */")
    for ref in compiled_refs:
        add(build_file(uid(f"buildfile:{ref}"), ref, ref_name.get(ref, ref)))
    add("/* End PBXBuildFile section */")
    add("")

    # ---- PBXFileReference
    add("/* Begin PBXFileReference section */")
    add(f"\t\t{product_uid} /* {PRODUCT_NAME}.app */ = {{isa = PBXFileReference; "
        f"explicitFileType = wrapper.application; includeInIndex = 0; path = {PRODUCT_NAME}.app; "
        f"sourceTree = BUILT_PRODUCTS_DIR; }};")
    for entry in file_refs:
        add(entry)
    add("/* End PBXFileReference section */")
    add("")

    # ---- PBXFrameworksBuildPhase
    add("/* Begin PBXFrameworksBuildPhase section */")
    add(f"\t\t{frameworks_phase_uid} /* Frameworks */ = {{isa = PBXFrameworksBuildPhase; "
        f"buildActionMask = 2147483647; files = ( ); runOnlyForDeploymentPostprocessing = 0; }};")
    add("/* End PBXFrameworksBuildPhase section */")
    add("")

    # ---- PBXGroup
    add("/* Begin PBXGroup section */")

    def group_block(uid_value: str, name: str, children: list, path: str | None = None) -> None:
        path_line = f"path = {quote(path)}; " if path else ""
        rendered = "\n".join(f"\t\t\t\t{child} /* child */," for child in children)
        add(f"\t\t{uid_value} /* {name} */ = {{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n{rendered}\n"
            f"\t\t\t);\n\t\t\t{path_line}name = {quote(name)};\n\t\t\tsourceTree = \"<group>\";\n\t\t}};")

    group_block(core_inc_group, "include", group_children.get(core_inc_group, []), "include")
    group_block(core_src_group, "src", group_children.get(core_src_group, []), "src")
    group_block(core_group, RUNTIME_CORE, [core_inc_group, core_src_group], RUNTIME_CORE)
    group_block(diag_inc_group, "include", group_children.get(diag_inc_group, []), "include")
    group_block(diag_src_group, "src", group_children.get(diag_src_group, []), "src")
    group_block(diag_group, DIAGNOSTICS, [diag_inc_group, diag_src_group], DIAGNOSTICS)
    group_block(poc_group, RUNTIME_POC,
                [plist_ref, bridging_ref] + objc_refs + swift_refs, RUNTIME_POC)
    group_block(products_group_uid, "Products", [product_uid])
    group_block(main_group_uid, "WinlatorPhase02",
                [core_group, diag_group, poc_group, products_group_uid])
    add("/* End PBXGroup section */")
    add("")

    # ---- PBXNativeTarget
    add("/* Begin PBXNativeTarget section */")
    build_phase_entries = "\n".join(
        f"\t\t\t\t{phase} /* phase */," for phase in (sources_phase_uid, frameworks_phase_uid, resources_phase_uid)
    )
    source_file_entries = "\n".join(
        f"\t\t\t\t{uid(f'buildfile:{ref}')} /* {ref_name.get(ref, ref)} in Sources */,"
        for ref in compiled_refs
    )
    add(f"""\t\t{target_uid} /* {TARGET_NAME} */ = {{
\t\t\tisa = PBXNativeTarget;
\t\t\tbuildConfigurationList = {target_config_list_uid} /* Build configuration list for PBXNativeTarget "{TARGET_NAME}" */;
\t\t\tbuildPhases = (
{build_phase_entries}
\t\t\t);
\t\t\tbuildRules = (
\t\t\t);
\t\t\tdependencies = (
\t\t\t);
\t\t\tname = {TARGET_NAME};
\t\t\tproductName = {TARGET_NAME};
\t\t\tproductReference = {product_uid} /* {PRODUCT_NAME}.app */;
\t\t\tproductType = "com.apple.product-type.application";
\t\t}};""")
    add("/* End PBXNativeTarget section */")
    add("")

    # ---- PBXProject
    add("/* Begin PBXProject section */")
    add(f"""\t\t{project_uid} /* Project object */ = {{
\t\t\tisa = PBXProject;
\t\t\tattributes = {{
\t\t\t\tBuildIndependentTargetsInParallel = 1;
\t\t\t\tLastUpgradeCheck = 1500;
\t\t\t\tTargetAttributes = {{
\t\t\t\t\t{target_uid} = {{
\t\t\t\t\t\tCreatedOnToolsVersion = 15.0;
\t\t\t\t\t}};
\t\t\t\t}};
\t\t\t}};
\t\t\tbuildConfigurationList = {project_config_list_uid} /* Build configuration list for PBXProject "WinlatorPhase02" */;
\t\t\tcompatibilityVersion = "Xcode 14.0";
\t\t\tdevelopmentRegion = en;
\t\t\thasScannedForEncodings = 0;
\t\t\tknownRegions = (
\t\t\t\ten,
\t\t\t\tBase,
\t\t\t);
\t\t\tmainGroup = {main_group_uid};
\t\t\tproductRefGroup = {products_group_uid} /* Products */;
\t\t\tprojectDirPath = "";
\t\t\tprojectRoot = "";
\t\t\ttargets = (
\t\t\t\t{target_uid} /* {TARGET_NAME} */,
\t\t\t);
\t\t}};""")
    add("/* End PBXProject section */")
    add("")

    # ---- PBXResourcesBuildPhase
    add("/* Begin PBXResourcesBuildPhase section */")
    add(f"\t\t{resources_phase_uid} /* Resources */ = {{isa = PBXResourcesBuildPhase; "
        f"buildActionMask = 2147483647; files = ( ); runOnlyForDeploymentPostprocessing = 0; }};")
    add("/* End PBXResourcesBuildPhase section */")
    add("")

    # ---- PBXSourcesBuildPhase
    add("/* Begin PBXSourcesBuildPhase section */")
    add(f"\t\t{sources_phase_uid} /* Sources */ = {{\n\t\t\tisa = PBXSourcesBuildPhase;\n"
        f"\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n{source_file_entries}\n\t\t\t);\n"
        f"\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t}};")
    add("/* End PBXSourcesBuildPhase section */")
    add("")

    # ---- XCBuildConfiguration
    common_project = [
        "ALWAYS_SEARCH_USER_PATHS = NO;",
        "CLANG_ENABLE_MODULES = YES;",
        "CLANG_ENABLE_OBJC_ARC = YES;",
        "ENABLE_STRICT_OBJC_MSGSEND = YES;",
        "GCC_C_LANGUAGE_STANDARD = gnu11;",
        "IPHONEOS_DEPLOYMENT_TARGET = " + DEPLOYMENT_TARGET + ";",
        "SDKROOT = iphoneos;",
        "SWIFT_VERSION = " + SWIFT_VERSION + ";",
        "ONLY_ACTIVE_ARCH = NO;",
    ]
    target_common = [
        f"PRODUCT_NAME = {PRODUCT_NAME};",
        f"PRODUCT_BUNDLE_IDENTIFIER = {BUNDLE_IDENTIFIER};",
        f"INFOPLIST_FILE = {RUNTIME_POC}/{PLIST_NAME};",
        f"SWIFT_OBJC_BRIDGING_HEADER = {RUNTIME_POC}/{BRIDGING_HEADER};",
        "GENERATE_INFOPLIST_FILE = NO;",
        'HEADER_SEARCH_PATHS = ("$(SRCROOT)/RuntimeCore/include", "$(SRCROOT)/Diagnostics/include");',
        'OTHER_CFLAGS = (' + ", ".join(f'"{flag}"' for flag in STRICT_WARNINGS) + ");",
        "TARGETED_DEVICE_FAMILY = \"1,2\";",
        "ASSETCATALOG_COMPILER_APPICON_NAME = \"\";",
        "CODE_SIGN_STYLE = Automatic;",
        "ENABLE_USER_SCRIPT_SANDBOXING = NO;",
    ]

    add("/* Begin XCBuildConfiguration section */")

    def configuration(uid_value: str, name: str, settings: list, debug: bool) -> None:
        entries = "\n".join(f"\t\t\t\t{line}" for line in settings)
        add(f"\t\t{uid_value} /* {name} */ = {{\n\t\t\tisa = XCBuildConfiguration;\n"
            f"\t\t\tbuildSettings = {{\n{entries}\n\t\t\t}};\n\t\t\tname = {name};\n\t\t}};")

    configuration(project_debug_uid, "Debug", common_project + [
        "DEBUG_INFORMATION_FORMAT = dwarf;",
        "ENABLE_TESTABILITY = YES;",
        "GCC_OPTIMIZATION_LEVEL = 0;",
        'GCC_PREPROCESSOR_DEFINITIONS = ("DEBUG=1", "$(inherited)");',
        "MTL_ENABLE_DEBUG_INFO = INCLUDE_SOURCE;",
        "SWIFT_ACTIVE_COMPILATION_CONDITIONS = DEBUG;",
        "SWIFT_OPTIMIZATION_LEVEL = \"-Onone\";",
    ], debug=True)
    configuration(project_release_uid, "Release", common_project + [
        "DEBUG_INFORMATION_FORMAT = \"dwarf-with-dsym\";",
        "ENABLE_NS_ASSERTIONS = NO;",
        "SWIFT_COMPILATION_MODE = wholemodule;",
        "SWIFT_OPTIMIZATION_LEVEL = \"-O\";",
        "VALIDATE_PRODUCT = YES;",
    ], debug=False)
    configuration(target_debug_uid, "Debug", target_common, debug=True)
    configuration(target_release_uid, "Release", target_common, debug=False)
    add("/* End XCBuildConfiguration section */")
    add("")

    # ---- XCConfigurationList
    add("/* Begin XCConfigurationList section */")
    add(f"""\t\t{project_config_list_uid} /* Build configuration list for PBXProject "WinlatorPhase02" */ = {{
\t\t\tisa = XCConfigurationList;
\t\t\tbuildConfigurations = (
\t\t\t\t{project_debug_uid} /* Debug */,
\t\t\t\t{project_release_uid} /* Release */,
\t\t\t);
\t\t\tdefaultConfigurationIsVisible = 0;
\t\t\tdefaultConfigurationName = Release;
\t\t}};""")
    add(f"""\t\t{target_config_list_uid} /* Build configuration list for PBXNativeTarget "{TARGET_NAME}" */ = {{
\t\t\tisa = XCConfigurationList;
\t\t\tbuildConfigurations = (
\t\t\t\t{target_debug_uid} /* Debug */,
\t\t\t\t{target_release_uid} /* Release */,
\t\t\t);
\t\t\tdefaultConfigurationIsVisible = 0;
\t\t\tdefaultConfigurationName = Release;
\t\t}};""")
    add("/* End XCConfigurationList section */")

    header = "// !$*UTF8*$!\n{\n\tarchiveVersion = 1;\n\tclasses = {\n\t};\n\tobjectVersion = 54;\n\tobjects = {\n\n"
    footer = f"\n\t}};\n\trootObject = {project_uid} /* Project object */;\n}}\n"
    return header + "\n".join(lines) + footer


def scheme_xml() -> str:
    target_uid = uid("target")
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<Scheme LastUpgradeVersion = "1500" version = "1.7">
   <BuildAction parallelizeBuildables = "YES" buildImplicitDependencies = "YES">
      <BuildActionEntries>
         <BuildActionEntry buildForTesting = "YES" buildForRunning = "YES" buildForProfiling = "YES" buildForArchiving = "YES" buildForAnalyzing = "YES">
            <BuildableReference
               BuildableIdentifier = "primary"
               BlueprintIdentifier = "{target_uid}"
               BuildableName = "{PRODUCT_NAME}.app"
               BlueprintName = "{TARGET_NAME}"
               ReferencedContainer = "container:{TARGET_NAME}.xcodeproj">
            </BuildableReference>
         </BuildActionEntry>
      </BuildActionEntries>
   </BuildAction>
   <TestAction buildConfiguration = "Debug" selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB" shouldUseLaunchSchemeArgsEnv = "YES">
      <Testables>
      </Testables>
   </TestAction>
   <LaunchAction buildConfiguration = "Debug" selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB" launchStyle = "0" useCustomWorkingDirectory = "NO" ignoresPersistentStateOnLaunch = "NO" debugDocumentVersioning = "YES" debugServiceExtension = "internal" allowLocationSimulation = "YES">
      <BuildableProductRunnable runnableDebuggingMode = "0">
         <BuildableReference
            BuildableIdentifier = "primary"
            BlueprintIdentifier = "{target_uid}"
            BuildableName = "{PRODUCT_NAME}.app"
            BlueprintName = "{TARGET_NAME}"
            ReferencedContainer = "container:{TARGET_NAME}.xcodeproj">
         </BuildableReference>
      </BuildableProductRunnable>
   </LaunchAction>
   <ProfileAction buildConfiguration = "Release" shouldUseLaunchSchemeArgsEnv = "YES" savedToolIdentifier = "" useCustomWorkingDirectory = "NO" debugDocumentVersioning = "YES">
   </ProfileAction>
   <AnalyzeAction buildConfiguration = "Debug">
   </AnalyzeAction>
   <ArchiveAction buildConfiguration = "Release" revealArchiveInOrganizer = "YES">
   </ArchiveAction>
</Scheme>
"""


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    files = collect_files(root)

    project_dir = root / f"{TARGET_NAME}.xcodeproj"
    scheme_dir = project_dir / "xcshareddata" / "xcschemes"
    scheme_dir.mkdir(parents=True, exist_ok=True)

    (project_dir / "project.pbxproj").write_text(build_objects(files), encoding="utf-8")
    (scheme_dir / f"{TARGET_NAME}.xcscheme").write_text(scheme_xml(), encoding="utf-8")

    print(f"generated {project_dir}/project.pbxproj")
    print(f"generated {scheme_dir}/{TARGET_NAME}.xcscheme")
    print(f"  target={TARGET_NAME} bundle_id={BUNDLE_IDENTIFIER} deployment={DEPLOYMENT_TARGET}")
    print(f"  C sources: {len(files['core_src'])} core + {len(files['diag_src'])} diagnostics; "
          f"ObjC: {len(files['objc_src'])}; Swift: {len(files['swift_src'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
