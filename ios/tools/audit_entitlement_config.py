#!/usr/bin/env python3
"""audit_entitlement_config.py — static audit of the entitlement configuration (pass 03).

The question this audit answers, and the answer it enforces:

  A .entitlements file existed in the tree while nothing referenced it, and the physical run
  #2 note simply said "exists but is NOT wired". Pass 03 investigated it and resolved it as a
  DOCUMENTED CONFIGURATION DECISION, not a code defect:

    * on iOS-based platforms every entitlement must be allowlisted by the provisioning
      profile, and a free/personal-team or sideload profile cannot allowlist a JIT
      entitlement — attaching it obtains nothing;
    * an entitlements file that requests an entitlement the profile does not allow makes the
      signed build or the install fail ("provisioning profile does not include the ...
      entitlement"; device 0xE8008016). That would break the only path that can validate
      anything — the owner's own install on the iPhone — for zero capability gain.

  What was actually missing was the audit and the documented opt-in, so the state can no
  longer be "a file nobody references, recorded nowhere". These rules enforce exactly that,
  and they also enforce the shape the wiring must have IF it is ever enabled (all target
  configurations, existing file, never a single configuration).

Rules:
  E1 L1_FILE            the requested-entitlement file exists, names the JIT key, and carries
                        no certificate/private-key/provisioning material.
  E2 WIRING_COHERENCE   either every target configuration references an existing entitlements
                        file, or no configuration does AND the deliberate decision plus the
                        opt-in are recorded in the tree. A subset (some configurations wired,
                        others not) is always a violation.
  E3 NO_GRANT_CLAIMS    nothing in the tree claims the entitlement is granted/applied or that
                        JIT is enabled, and the harness prints the four levels separately.
  E4 UNSIGNED_FACT      the build/CI really is unsigned and says so (the built product embeds
                        nothing: a fact, not a failure).
  E5 INSPECT_SCOPE      the inspection script prints key names and booleans only, never
                        certificate data or a profile dump, and reports the honest
                        ENTITLEMENT_SIGNATURE_INSPECTION=UNTESTED label without an Apple
                        toolchain.
  E6 NO_SECRETS         no provisioning profile, certificate, private key or committed secret
                        lives in the repository.
  E7 PHYSICAL_BASELINE  run #1 and run #2 documents are present and run #2 still carries its
                        verbatim counts (history is never retro-changed).

Output: one line per rule plus ENTITLEMENT_CONFIG_AUDIT=<violations>.

Usage: python3 tools/audit_entitlement_config.py [--root <dir>]
"""
import argparse
import pathlib
import re
import sys

ENTITLEMENTS = "RuntimePoC/WinlatorPhase02.entitlements"
PBXPROJ = "WinlatorPhase02.xcodeproj/project.pbxproj"
GENERATOR = "tools/generate_xcodeproj.py"
BUILD_SCRIPT = "tools/build_ios.sh"
INSPECT = "tools/inspect_entitlements.sh"
HARNESS = "Diagnostics/src/phase02_harness.c"
WORKFLOW = "../.github/workflows/ios-runtime-poc.yml"
RUN1 = "Documentation/IPHONE13_PHYSICAL_RUN_01.md"
RUN2 = "Documentation/IPHONE13_PHYSICAL_RUN_02.md"

JIT_KEY = "com.apple.security.cs.allow-jit"
RUN2_COUNTS = "records=52 assertions=0 pass=43 fail=0 blocked=5 unsupported=0 untested=1 not_applicable=3 summary=BLOCKED"

SECRET_MARKERS = ["-----BEGIN CERTIFICATE-----", "-----BEGIN PRIVATE KEY-----",
                  "-----BEGIN RSA PRIVATE KEY-----", "-----BEGIN EC PRIVATE KEY-----"]
SECRET_SUFFIXES = (".mobileprovision", ".p12", ".cer", ".key", ".pem", ".provisionprofile")

CLAIM_PATTERNS = [
    re.compile(r"JIT\s+is\s+enabled", re.IGNORECASE),
    re.compile(r"JIT\s+enabled\b(?!\s*:?\s*(NO|never|not))", re.IGNORECASE),
    re.compile(r"entitlement\s+(is\s+)?granted\b(?!\s*[:=]?\s*(NO|never|not))", re.IGNORECASE),
    re.compile(r"entitlement\s+(is\s+)?applied\b", re.IGNORECASE),
    re.compile(r"MAP_JIT\s+(is\s+)?(available|granted|works)\b", re.IGNORECASE),
]

# A line is only a claim when it is not also carrying a negation. The project is allowed to
# *name* the forbidden phrasings while denying them (the harness note does exactly that, and
# this audit quotes them in its own policy text), so the cue is required on the same line.
NEGATION_CUES = ("not ", "never", "no ", "cannot", "without", "deliberately",
                 "denied", "false", "fake", "claim", "reported as", "instead of",
                 "would be", "is a defect", "policy", "re.compile", "forbidden")
POLICY_FILES = {"tools/audit_entitlement_config.py", "tools/pass03_negative_controls.py"}


def parse_pbxproj(root):
    """Return the list of target build configurations with their settings."""
    sys.path.insert(0, str(root / "tools"))
    from openstep_plist import parse  # noqa: E402
    text = (root / PBXPROJ).read_text(encoding="utf-8")
    objects = parse(text).get("objects", {})
    configs = []
    for obj in objects.values():
        if not isinstance(obj, dict) or obj.get("isa") != "XCBuildConfiguration":
            continue
        settings = obj.get("buildSettings", {})
        if not isinstance(settings, dict) or "INFOPLIST_FILE" not in settings:
            continue                      # project-level configuration, not the app target
        configs.append((obj.get("name", "?"), settings))
    return configs


def rule_l1_file(root, out):
    violations = []
    path = root / ENTITLEMENTS
    if not path.is_file():
        violations.append(f"E1: {ENTITLEMENTS} is missing (level 1 unreadable)")
        return violations
    text = path.read_text(encoding="utf-8")
    if JIT_KEY not in text:
        violations.append(f"E1: {ENTITLEMENTS} does not request {JIT_KEY}")
    if "<key>" not in text:
        violations.append("E1: the entitlements file contains no plist keys")
    for marker in SECRET_MARKERS:
        if marker in text:
            violations.append("E1: the entitlements file contains credentials material")
    out.append("ok   E1 L1_FILE: requested-entitlement file present, key named, no credentials")
    return violations


def rule_wiring(root, out):
    violations = []
    configs = parse_pbxproj(root)
    if not configs:
        return ["E2: no target build configuration found in the project"]
    wired = [(name, settings.get("CODE_SIGN_ENTITLEMENTS")) for name, settings in configs
             if "CODE_SIGN_ENTITLEMENTS" in settings]
    if wired:
        names = ", ".join(name for name, _ in wired)
        if len(wired) != len(configs):
            violations.append(
                f"E2: CODE_SIGN_ENTITLEMENTS is set for a subset of the target configurations "
                f"({names}) - a project that requests an entitlement in one configuration and "
                f"not in another produces two different products")
        for name, value in wired:
            target = str(value).strip('"')
            if not target:
                violations.append(f"E2: {name} sets CODE_SIGN_ENTITLEMENTS to an empty value")
            elif not (root / target).is_file():
                violations.append(f"E2: {name} points CODE_SIGN_ENTITLEMENTS at '{target}', "
                                  f"which does not exist")
        out.append(f"ok   E2 WIRING_COHERENCE: wired for all target configurations ({names}) "
                   f"and the file exists")
        return violations

    # Not wired: the deliberate decision and the opt-in must be recorded where a reader looks.
    entitlements = (root / ENTITLEMENTS).read_text(encoding="utf-8")
    build = (root / BUILD_SCRIPT).read_text(encoding="utf-8")
    harness = (root / HARNESS).read_text(encoding="utf-8")
    if "WIRING DECISION" not in entitlements:
        violations.append("E2: the entitlements file is not wired and does not record the "
                          "wiring decision: an inert entitlement file with no decision is a "
                          "configuration defect")
    if "CODE_SIGN_ENTITLEMENTS" not in build:
        violations.append("E2: the build script does not document the CODE_SIGN_ENTITLEMENTS "
                          "opt-in for a signing context that can carry the entitlement")
    if "allow-jit" not in harness or "DELIBERATELY NOT attached" not in harness:
        violations.append("E2: the harness does not state the wiring decision at runtime "
                          "(level 1 must name the state, not leave it implicit)")
    if not violations:
        out.append("ok   E2 WIRING_COHERENCE: not wired for any configuration, and the "
                   "decision + opt-in are recorded (entitlements file, build script, harness)")
    return violations


def rule_no_grant_claims(root, out):
    violations = []
    sources = list((root / "RuntimeCore").rglob("*")) + list((root / "Diagnostics").rglob("*"))
    sources += list((root / "Documentation").glob("*.md")) + list((root / "tools").glob("*.py"))
    sources += list((root / "tools").glob("*.sh")) + [root / ENTITLEMENTS]
    for path in sources:
        if not path.is_file() or path.suffix in (".o", ".a"):
            continue
        rel = path.relative_to(root).as_posix()
        if rel in POLICY_FILES:
            continue          # the audit itself quotes the phrasings it forbids
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for number, line in enumerate(text.splitlines(), start=1):
            if any(cue in line for cue in NEGATION_CUES):
                continue
            for pattern in CLAIM_PATTERNS:
                match = pattern.search(line)
                if match:
                    violations.append(f"E3: {rel}:{number} claims a granted capability: "
                                      f"{match.group(0)!r}")
    harness = (root / HARNESS).read_text(encoding="utf-8")
    for level in ("L1_requested_in_repo", "L2_embedded_at_build", "L3_granted_to_signature",
                  "L4_observed_at_runtime"):
        if level not in harness:
            violations.append(f"E3: the harness note does not report {level}")
    if "never claimed to" not in harness:
        violations.append("E3: the harness does not state that the wiring is never claimed to "
                          "grant the capability")
    if not violations:
        out.append("ok   E3 NO_GRANT_CLAIMS: no granted-capability claim; the four levels are "
                   "reported separately and the wiring is explicitly not a grant")
    return violations


def rule_unsigned_fact(root, out):
    violations = []
    build = (root / BUILD_SCRIPT).read_text(encoding="utf-8")
    workflow = (root / WORKFLOW).read_text(encoding="utf-8")
    if "CODE_SIGNING_ALLOWED=NO" not in build:
        violations.append("E4: the device build is no longer unsigned - the product would "
                          "embed a signature whose entitlements nobody inspected")
    for label in ("UNSIGNED_IPA",):
        if label not in workflow:
            violations.append(f"E4: the CI no longer labels the product {label}")
    if "SIGNING=none" not in workflow:
        violations.append("E4: the CI does not state that the packaged IPA carries no signature")
    if not violations:
        out.append("ok   E4 UNSIGNED_FACT: unsigned build and its labelling intact "
                   "(the product embeds nothing; level 2 stays a signing-step fact)")
    return violations


def rule_inspect_scope(root, out):
    violations = []
    text = (root / INSPECT).read_text(encoding="utf-8")
    if "ENTITLEMENT_SIGNATURE_INSPECTION=UNTESTED REASON=NO_APPLE_TOOLCHAIN" not in text:
        violations.append("E5: the inspection script does not report the honest "
                          "UNTESTED/NO_APPLE_TOOLCHAIN label when it cannot read a signature")
    for forbidden in ("codesign -dvv", "codesign -dvvv", "codesign -d -vvv",
                      "security find-identity", "cat \"$WORK/profile.plist\""):
        if forbidden in text:
            violations.append(f"E5: the inspection script can print credential material "
                              f"({forbidden!r})")
    if "key names" not in text:
        violations.append("E5: the inspection script does not document the key-names-only rule")
    if not violations:
        out.append("ok   E5 INSPECT_SCOPE: key names and booleans only, honest UNTESTED label "
                   "without an Apple toolchain, no certificate/profile dumps")
    return violations


def rule_no_secrets(root, out):
    violations = []
    for path in (root).rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if "/build/" in f"/{rel}" or rel.startswith("build/"):
            continue
        if path.suffix in SECRET_SUFFIXES:
            violations.append(f"E6: {rel} looks like signing material and must not be in the "
                              f"repository")
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        for marker in SECRET_MARKERS:
            if any(line.strip().startswith(marker) for line in text.splitlines()):
                violations.append(f"E6: {rel} contains a private key or certificate block")
                break
    if not violations:
        out.append("ok   E6 NO_SECRETS: no profile, certificate, private key or committed "
                   "secret in the tree")
    return violations


def rule_physical_baseline(root, out):
    violations = []
    for name in (RUN1, RUN2):
        if not (root / name).is_file():
            violations.append(f"E7: {name} is missing")
    if (root / RUN2).is_file():
        text = (root / RUN2).read_text(encoding="utf-8")
        if RUN2_COUNTS not in text:
            violations.append("E7: the run #2 document no longer carries its verbatim counts")
        if "fail=0" not in text:
            violations.append("E7: the run #2 document lost the frozen fail=0 result")
    if not violations:
        out.append("ok   E7 PHYSICAL_BASELINE: run #1 present and run #2 carries its verbatim "
                   "counts (immutable history)")
    return violations


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent.parent))
    args = parser.parse_args()
    root = pathlib.Path(args.root).resolve()

    out = []
    violations = []
    for rule in (rule_l1_file, rule_wiring, rule_no_grant_claims, rule_unsigned_fact,
                 rule_inspect_scope, rule_no_secrets, rule_physical_baseline):
        violations.extend(rule(root, out))

    for line in out:
        print(line)
    for violation in violations:
        print(f"FAIL {violation}")
    print(f"ENTITLEMENT_CONFIG_AUDIT={len(violations)}")
    return 1 if violations else 0


if __name__ == "__main__":
    raise SystemExit(main())
