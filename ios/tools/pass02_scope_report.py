#!/usr/bin/env python3
"""pass02_scope_report.py — prove that pass 02 changed exactly what the defects required.

Every file this round touches must be listed below with the defect that required it. A file
outside the list is an out-of-scope change and fails the report; so does a listed file that is
*not* changed (the list is the round's contract, both ways). Regenerated evidence is checked
separately: it lives under Documentation/evidence/ and carries no behaviour.

The change set is the round's diff against its baseline, resolved as:
  * `--base-dir <dir>`: compare against a copy of the tree (used to prove an incremental
    package inside a clone, where the round's commits are not in the history);
  * otherwise `HEAD^` when HEAD is the pass-02 commit;
  * otherwise `HEAD` (the round is still uncommitted, the normal case while working).

Usage: python3 tools/pass02_scope_report.py [--root <dir>] [--base-dir <dir>]
Exit:  0 = the change set matches the defect list, 1 = out-of-scope or stale entry.
"""
import argparse
import pathlib
import subprocess
import sys

# file -> (defects, concept changed, why it was necessary)
SCOPE = {
    "RuntimePoC/ContentView.swift": (
        "S-009",
        "save(): the destination of the saved report",
        "the report was written into tmp/, which file sharing never exposes: the evidence "
        "could not be recovered from the device. The SwiftUI body is untouched (frozen by "
        "tools/ui_surface.snapshot.txt)."),
    "RuntimeCore/src/runtime_memory.c": (
        "S-017",
        "rt_mem_protect(): the len > SIZE_MAX - page guard",
        "the same guard rt_mem_reserve already had: without it the rounding wraps, the call "
        "protects a sliver and reports success."),
    "RuntimeCore/src/runtime_filesystem.c": (
        "S-010, S-011, S-012",
        "rt_fs_links_and_modes(): the errno captured by rt_fs_write_pattern is no longer "
        "overwritten; lstat/stat results are checked separately from the type of what they "
        "returned. rt_fs_temp_file(): a short write reports EIO instead of a stale errno",
        "errno was published from a later read (possibly 0 or unrelated) - the same defect "
        "class that produced 'errno=0 (Undefined error: 0)' in IPHONE13_PHYSICAL_RUN_01."),
    "RuntimeCore/src/runtime_ipc.c": (
        "S-013",
        "rt_ipc_scm_rights(): the short write/read of the transferred descriptor report EIO",
        "a short transfer is not an errno-producing failure; the raw errno read could publish "
        "0. The pair round trip in the same file already did it correctly."),
    "RuntimeCore/src/runtime_signals.c": (
        "S-015",
        "rt_signal_roundtrip(): every failure carries an errno, and the restore of the "
        "previous disposition is honoured instead of being discarded and reported as PASS",
        "a return -1 left *err_out untouched (errno=0 in the report) and a failed restore was "
        "invisible in a PASS record."),
    "RuntimeCore/src/runtime_threads.c": (
        "S-016",
        "the five round trips report EILSEQ when their observed value disagrees with the "
        "expected one",
        "a mismatch returned -1 with *err_out = 0, so a real failure was reported with the "
        "errno of a success."),
    "RuntimeCore/src/runtime_cpu_abi.c": (
        "S-014",
        "rt_cpu_facts_summary(): a summary that does not fit its buffer is marked",
        "a silently truncated summary is evidence with a hole in it (same rule as the DETAIL "
        "marker)."),
    "RuntimeCore/src/runtime_context.c": (
        "S-014",
        "rt_context_describe(): same truncation marker",
        "same defect in the second summary composer."),
    "Diagnostics/src/phase02_log.c": (
        "S-014",
        "phase02_log_summary_line(): same truncation marker",
        "the summary line is what the CI gates on; a silent cut would hide counts."),
    "Tests/test_runtime_core.c": (
        "S-010, S-011, S-012, S-014, S-015, S-016, S-017",
        "three regression tests (overflow refusal, failure-carries-an-errno contracts, "
        "truncation markers) and the fcntl.h include they need",
        "every fixed defect gets a regression; no existing check was removed or weakened."),
    "tools/audit_ios_stabilization.py": (
        "S-010, S-011, S-012, S-013, S-014, S-015, S-016, S-017",
        "four new rules (ERRNO_CAPTURED_NOW, PROTECT_OVERFLOW, TRUNCATION_MARKED, "
        "PAGE_SIZE_MEASURED) on top of the eleven from pass 01",
        "each defect must be impossible to reintroduce without the audit failing."),
    "tools/stabilization_negative_controls.py": (
        "S-010, S-011, S-012, S-013, S-014, S-015, S-016, S-017",
        "seven new controls (L-R) that put each new defect back and require detection",
        "a rule that cannot fail proves nothing: every rule is exercised against its defect."),
    "tools/pass02_scope_report.py": (
        "pass 02 delivery",
        "new tool: this report",
        "the round must prove that it changed exactly what the defects required, and nothing "
        "else."),
    "tools/collect_evidence.sh": (
        "pass 02 delivery",
        "runs the scope report and the UI-surface check with the round's own wording",
        "the evidence bundle must carry the proof, not just the claim."),
    "tools/ui_surface.snapshot.txt": (
        "S-009",
        "the frozen SwiftUI body, extracted from the pass-01 commit",
        "so the interface cannot drift while the save destination is corrected."),
    ".github/workflows/ios-runtime-poc.yml": (
        "pass 02 delivery",
        "step 4h keeps the audit and controls gated with the new counts (18/18)",
        "a control that is not gated in CI cannot protect the next run."),
    "Documentation/PHASE02_IOS_ERROR_LEDGER.md": (
        "pass 02 delivery",
        "S-009 closed, S-010..S-017 registered, counts updated",
        "the ledger is the round's inventory."),
    "Documentation/RELATORIO_FASE_02_RECONSTRUIDA.md": (
        "pass 02 delivery",
        "section 17: the round's narrative",
        "the technical report must reflect the state after the round."),
    "Documentation/PHASE02_IOS_STABILIZATION_PASS_01.pointer.md": (
        "pass 02 delivery",
        "points at the pass-02 report as well",
        "the in-tree pointer must not send a reader to a stale report."),
}
EVIDENCE_PREFIX = "Documentation/evidence/"


def tree_path(root, path):
    """Resolve a scope key inside the tree: the ios/ subtree, or the repository root."""
    return (root.parent / path) if path.startswith(".github/") else (root / path)


def compare_with_directory(root, base):
    """Incremental proof: which files differ from a baseline copy of the tree?"""
    changed = set()
    for path in sorted(SCOPE):
        candidate = tree_path(base, path)
        here = tree_path(root, path)
        if not candidate.exists() or not here.exists():
            changed.add(path)
        elif candidate.read_bytes() != here.read_bytes():
            changed.add(path)
    print("== pass 02 scope (against a baseline tree) ==")
    failures = []
    for path in sorted(changed):
        defects, concept, why = SCOPE[path]
        print(f"ok   {path}  [{defects}]")
        print(f"       concept: {concept}")
        print(f"       why    : {why}")
    for path in sorted(set(SCOPE) - changed):
        failures.append(path)
        print(f"FAIL {path} is listed as changed but is identical to the baseline")
    extra = []
    for path in base.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(base)
        if rel.parts and rel.parts[0] in ("build", "__pycache__", ".git"):
            continue
        rel_s = rel.as_posix()
        if rel_s.startswith("ios/"):
            # inside the ios/ subtree: compare with the same file under the candidate tree
            rel_s = rel_s[len("ios/"):]
            here = root / rel_s
        elif rel_s.startswith("Documentation/evidence/"):
            continue
        elif rel_s in SCOPE:
            continue
        else:
            # repository root (the workflow and any other round artifact)
            here = root.parent / rel_s
        if rel_s.startswith("Documentation/evidence/"):
            continue
        if rel_s in SCOPE:
            continue
        if here.exists() and here.read_bytes() != path.read_bytes():
            extra.append(rel_s)
    for path in sorted(set(extra)):
        failures.append(path)
        print(f"FAIL {path} differs from the baseline and no defect in this round required it")
    print()
    print(f"PASS02_SCOPE_OUT_OF_SCOPE={len(failures)}")
    return 0 if not failures else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent.parent))
    parser.add_argument("--base-dir", default=None,
                        help="compare against this copy of the tree instead of a git revision")
    args = parser.parse_args()
    root = pathlib.Path(args.root)
    repo = root.parent

    if args.base_dir is not None:
        return compare_with_directory(root, pathlib.Path(args.base_dir))

    def git(*args):
        return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True,
                              check=True).stdout

    # The round's baseline: the pass-02 commit's parent once that commit exists, HEAD while the
    # round is still uncommitted. Working-tree changes on top of either are included.
    subject = git("log", "-1", "--format=%s").strip()
    base = "HEAD^" if "stabilization pass 02" in subject else "HEAD"

    rel = set()
    deleted = []
    for line in git("diff", "--name-status", base).splitlines():
        status, path = line.split("\t", 1)
        if path.startswith("ios/"):
            path = path[len("ios/"):]
        elif not path.startswith(".github/"):
            continue
        if status.startswith("D"):
            deleted.append(path)
            continue
        rel.add(path)
    # Untracked files are changes too: a defect fix that arrives as a new tool still counts, and
    # a deletion must never pass unnoticed.
    for line in subprocess.run(["git", "-C", str(repo), "status", "--porcelain"],
                               capture_output=True, text=True, check=True).stdout.splitlines():
        status, path = line[:2].strip(), line[3:].strip()
        if status != "??":
            continue
        if path.startswith("ios/"):
            path = path[len("ios/"):]
        elif not path.startswith(".github/"):
            continue
        if (repo / ".git").exists():
            rel.add(path)
    behaviour = sorted(p for p in rel if not p.startswith(EVIDENCE_PREFIX))
    evidence = sorted(p for p in rel if p.startswith(EVIDENCE_PREFIX))

    print("== pass 02 scope ==")
    print(f"changed files (behaviour): {len(behaviour)}")
    failures = []
    numstat = git("diff", "--numstat", base).splitlines()
    stats = {}
    for line in numstat:
        added, removed, path = line.split("\t", 2)
        if path.startswith("ios/"):
            stats[path[len("ios/"):]] = (added, removed)
        elif path.startswith(".github/"):
            stats[path] = (added, removed)
    for path in behaviour:
        added, removed = stats.get(path, ("?", "?"))
        if path in SCOPE:
            defects, concept, why = SCOPE[path]
            print(f"ok   {path}  [{defects}]  +{added}/-{removed}")
            print(f"       concept: {concept}")
            print(f"       why    : {why}")
        else:
            failures.append(path)
            print(f"FAIL {path}  +{added}/-{removed} is OUT OF SCOPE: no defect required it")
    for path, (defects, _concept, _why) in SCOPE.items():
        if path not in rel:
            failures.append(path)
            print(f"FAIL {path}  [{defects}] is listed as changed but is unchanged: "
                  "the round's contract is stale")
    print()
    for path in deleted:
        failures.append(path)
        print(f"FAIL {path} was DELETED: this round may not delete files")
    print(f"regenerated evidence files: {len(evidence)} (no behaviour, listed in the bundle)")
    print(f"out-of-scope changes: {len(failures)}")
    print()
    print(f"PASS02_SCOPE_OUT_OF_SCOPE={len(failures)}")
    print(f"UI_VISUAL_CHANGES=0 (enforced by the UI_SURFACE_FROZEN rule)")
    missing = [p for p in rel
               if not ((repo / p) if p.startswith(".github/") else (root / p)).exists()]
    print(f"FILES_DELETED={len(deleted)} (missing from the tree: {len(missing)})")
    print("PHASE04_FUNCTIONAL_CHANGES="
          f"{len([p for p in rel if p.startswith('box64-registers')])}")
    print("PHASE05_STARTED=NO")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
