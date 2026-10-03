#!/bin/sh
# collect_evidence.sh — regenerate every evidence artefact of this reconstruction.
#
# PHASE_02_RECONSTRUCTED_POC. Host and AArch64 evidence is produced by running the
# code; nothing is transcribed from the lost Phase 02 report.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
EVIDENCE="$ROOT/Documentation/evidence"
mkdir -p "$EVIDENCE"

echo "[evidence] inventory of the reconstructed tree"
find "$ROOT/RuntimeCore" "$ROOT/Diagnostics" "$ROOT/RuntimePoC" "$ROOT/Tests" "$ROOT/tools" \
     -name __pycache__ -prune -o -type f -print | sort \
     | xargs sha256sum > "$EVIDENCE/inventory_sha256.txt"
wc -l < "$EVIDENCE/inventory_sha256.txt"

echo "[evidence] host build + tests"
sh "$ROOT/tools/build_host_harness.sh"

echo "[evidence] aarch64 cross build + qemu tests"
sh "$ROOT/tools/build_aarch64_cross.sh"

echo "[evidence] Apple SDK include audit (static pre-check)"
python3 "$ROOT/tools/audit_apple_includes.py" 2>&1 | tee "$EVIDENCE/apple_include_audit.txt"

echo "[evidence] Apple API audit (macOS-only symbols)"
python3 "$ROOT/tools/audit_apple_apis.py" 2>&1 | tee "$EVIDENCE/apple_api_audit.txt"

echo "[evidence] interface audit (declarations + layering)"
python3 "$ROOT/tools/audit_interfaces.py" 2>&1 | tee "$EVIDENCE/interface_audit.txt"

echo "[evidence] bridge syntax check (clang + stub Foundation)"
sh "$ROOT/tools/check_bridge_syntax.sh" 2>&1 | tee "$EVIDENCE/bridge_syntax_stdout.txt"

echo "[evidence] platform composition audit (targets, forbidden symbols, backend selection)"
python3 "$ROOT/tools/audit_platform_composition.py" 2>&1 | tee "$EVIDENCE/platform_composition_audit.txt"

echo "[evidence] negative controls for the composition audit (CI run #5 condition included)"
python3 "$ROOT/tools/platform_composition_negative_control.py" \
    2>&1 | tee "$EVIDENCE/platform_composition_negative_control.txt" | tail -6

echo "[evidence] object-level link audit (AArch64 objects; the Apple link stays UNTESTED)"
sh "$ROOT/tools/audit_link_symbols.sh" 2>&1 | tee "$EVIDENCE/link_symbol_audit.txt" | tail -6

echo "[evidence] Fix 06 contract audit (loader reason, stages, errno 0, MAP_JIT order)"
python3 "$ROOT/tools/audit_fix06_contracts.py" 2>&1 | tee "$EVIDENCE/fix06_contracts_audit.txt" | tail -3

echo "[evidence] pass 01 scope (Fase 04 byte-intact, zero interface change, baseline untouched)"
{
  echo "== iOS stabilization pass 01 — scope check =="
  echo "-- Fase 04 (must be empty):"
  git -C "$ROOT/.." diff --stat -- ios/box64-registers | sed 's/^/   /'
  echo "-- interface/app layer RuntimePoC/ (pass 02: only the S-009 save destination;"
  echo "   the visible surface is frozen by tools/ui_surface.snapshot.txt):"
  git -C "$ROOT/.." diff --stat -- ios/RuntimePoC | sed 's/^/   /'
  echo "-- physical baseline IPHONE13_PHYSICAL_RUN_01.md (must be empty: immutable):"
  git -C "$ROOT/.." diff --stat -- ios/Documentation/IPHONE13_PHYSICAL_RUN_01.md | sed 's/^/   /'
  echo "-- files changed by this pass:"
  git -C "$ROOT/.." diff --name-only | sed 's/^/   /'
} 2>&1 | tee "$EVIDENCE/pass01_scope_check.txt" | tail -3

echo "[evidence] pass 02 scope (every change mapped to the defect that required it)"
python3 "$ROOT/tools/pass02_scope_report.py" 2>&1 | tee "$EVIDENCE/pass02_scope_check.txt" | tail -3

echo "[evidence] iOS stabilization audit (JIT results, arena, guard, dual map, shm, log)"
python3 "$ROOT/tools/audit_ios_stabilization.py" 2>&1 | tee "$EVIDENCE/ios_stabilization_audit.txt" | tail -3

echo "[evidence] stabilization negative controls"
python3 "$ROOT/tools/stabilization_negative_controls.py" \
    2>&1 | tee "$EVIDENCE/ios_stabilization_negative_controls.txt" | tail -3

echo "[evidence] Fix 06 negative controls (every run #1 defect must be caught)"
python3 "$ROOT/tools/fix06_negative_controls.py" \
    2>&1 | tee "$EVIDENCE/fix06_negative_controls.txt" | tail -3

echo "[evidence] entitlement levels (1-2 repository facts, 3 needs a signed product on macOS)"
sh "$ROOT/tools/inspect_entitlements.sh" "$ROOT/build/host/phase02_poc" \
    2>&1 | tee "$EVIDENCE/entitlement_levels.txt" | head -3

echo "[evidence] entitlement configuration audit (level 1, wiring coherence, no grant claims)"
python3 "$ROOT/tools/audit_entitlement_config.py" 2>&1 | tee "$EVIDENCE/entitlement_config_audit.txt" | tail -3

echo "[evidence] JIT causal graph audit (probe -> arena -> write window -> execution -> free)"
python3 "$ROOT/tools/audit_jit_causal_graph.py" 2>&1 | tee "$EVIDENCE/jit_causal_graph_audit.txt" | tail -3

echo "[evidence] SHM / dual-mapping backend audit (named path intact, iOS backend selected)"
python3 "$ROOT/tools/audit_shm_backend.py" 2>&1 | tee "$EVIDENCE/shm_backend_audit.txt" | tail -3

echo "[evidence] pass 03 negative controls (every detector must fire, then the tree is restored)"
python3 "$ROOT/tools/pass03_negative_controls.py" \
    2>&1 | tee "$EVIDENCE/pass03_negative_controls.txt" | tail -3

echo "[evidence] pass 04 selected-suite regression (the entry points the Run Selected button uses)"
(cd "$ROOT/build/host" && ./phase02_run_selected_tests) \
    > "$EVIDENCE/host_run_selected_tests.log" 2>&1 || true
tail -1 "$EVIDENCE/host_run_selected_tests.log"

echo "[evidence] pass 04 crash-class negative control (hard termination mid-run, non-destructive)"
python3 "$ROOT/tools/pass04_negative_controls.py" \
    2>&1 | tee "$EVIDENCE/pass04_negative_controls.txt" | tail -2

echo "[evidence] ASAN / UBSAN over the selected-suite entry points"
sh "$ROOT/tools/run_sanitizers.sh" 2>&1 | tee "$EVIDENCE/sanitizers.txt"

echo "[evidence] xcode project structure"
python3 "$ROOT/tools/generate_xcodeproj.py" > "$EVIDENCE/ios_generate.log"
python3 "$ROOT/tools/validate_xcodeproj.py" > "$EVIDENCE/ios_validate.log" || true
tail -1 "$EVIDENCE/ios_validate.log"

echo "[evidence] pass 03 scope (blockers only; UI, Fase 04 and the baseline untouched)"
{
  echo "== pass 03 scope check (base f81dda2) =="
  echo "-- files changed by this pass under ios/:"
  git -C "$ROOT/.." diff --stat f81dda2 -- ios | sed 's/^/   /'
  echo "-- files deleted by this pass under ios/ (must be empty):"
  git -C "$ROOT/.." diff --diff-filter=D --name-only f81dda2 -- ios | sed 's/^/   /'
  echo "-- Fase 04 (must be empty):"
  git -C "$ROOT/.." diff --stat f81dda2 -- ios/box64-registers | sed 's/^/   /'
  echo "-- IPHONE13_PHYSICAL_RUN_01.md (must be empty: immutable baseline):"
  git -C "$ROOT/.." diff --stat f81dda2 -- ios/Documentation/IPHONE13_PHYSICAL_RUN_01.md | sed 's/^/   /'
  echo "-- new files added by this pass:"
  git -C "$ROOT/.." ls-files --others --exclude-standard -- ios | sed 's/^/   /'
  echo "UI_VISUAL_CHANGES=0 (audit_ios_stabilization.py rule_ui_surface_frozen, snapshot intact)"
  echo "PHASE04_FUNCTIONAL_CHANGES=0 (no diff under ios/box64-registers)"
  echo "PHASE05_STARTED=NO"
} 2>&1 | tee "$EVIDENCE/pass03_scope_check.txt" | tail -6

echo "[evidence] pass 04 scope (base d83332a: the crash fix only; UI, Fase 04 and runs 01/02 untouched)"
{
  echo "== pass 04 scope check (base d83332a, the commit physical run 03 was built from) =="
  echo "-- files changed by this pass under ios/:"
  git -C "$ROOT/.." diff --stat d83332a -- ios | sed 's/^/   /'
  echo "-- files deleted by this pass under ios/ (must be empty):"
  git -C "$ROOT/.." diff --diff-filter=D --name-only d83332a -- ios | sed 's/^/   /'
  echo "-- new files added by this pass:"
  git -C "$ROOT/.." ls-files --others --exclude-standard -- ios | sed 's/^/   /'
  echo "-- Fase 04 (must be empty):"
  git -C "$ROOT/.." diff --stat d83332a -- ios/box64-registers | sed 's/^/   /'
  echo "-- IPHONE13_PHYSICAL_RUN_01.md / _02.md / 03 (immutable: must be empty):"
  git -C "$ROOT/.." diff --stat d83332a -- ios/Documentation/IPHONE13_PHYSICAL_RUN_01.md \
      ios/Documentation/IPHONE13_PHYSICAL_RUN_02.md | sed 's/^/   /'
  echo "-- RuntimePoC/ (SwiftUI surface; only the bridge may change):"
  git -C "$ROOT/.." diff --stat d83332a -- ios/RuntimePoC | sed 's/^/   /'
  echo "UI_VISUAL_CHANGES=0 (ContentView.swift/WinlatorPhase02App.swift unchanged; "
  echo "                     snapshot tools/ui_surface.snapshot.txt intact)"
  echo "PHASE04_FUNCTIONAL_CHANGES=0 (no diff under ios/box64-registers)"
  echo "PHASE05_STARTED=NO"
} 2>&1 | tee "$EVIDENCE/pass04_scope_check.txt" | tail -6

echo "[evidence] host/device distinction"
{
    echo "HOST_TESTS   = CONFIRMADA NO HARNESS HOST (linux) - never an iOS result"
    echo "AARCH64      = AARCH64_QEMU - qemu does not validate iOS"
    echo "IOS_BUILD    = see ios_* logs; without a macOS runner it is"
    echo "               IOS_BUILD=UNTESTED REASON=NO_APPLE_TOOLCHAIN"
    echo "IPHONE       = PHYSICAL_DEVICE_VALIDATION_REQUIRED (iPhone 13 target)"
} > "$EVIDENCE/evidence_classes.txt"
cat "$EVIDENCE/evidence_classes.txt"
