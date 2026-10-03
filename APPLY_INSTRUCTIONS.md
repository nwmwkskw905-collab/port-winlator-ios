# phase02-ios-blocker-resolution-pass-03 — how to apply

Base: `f81dda2` ("Stabilize Phase 02 iOS runtime after physical validation"), branch used here:
`ios-phase-02-blocker-pass-03`. Nothing was pushed and no workflow was started.

## Option A — apply the patch to a clean checkout of f81dda2

    git fetch --depth=100 https://github.com/nwmwkskw905-collab/port-winlator-ios.git main
    git checkout -b ios-phase-02-blocker-pass-03 f81dda2
    git apply --check ../phase02-ios-blocker-resolution-pass-03.diff
    git apply          ../phase02-ios-blocker-resolution-pass-03.diff

## Option B — copy the packaged files over a checkout of f81dda2

    tar -xzf phase02-ios-blocker-resolution-pass-03.tar.gz -C <checkout of f81dda2>

The archive contains only the files this pass changed or added, with their repository paths,
plus this file. No file is deleted by the patch or by the archive.

## Verification after applying (Linux host, no Apple toolchain needed)

    sh ios/tools/collect_evidence.sh          # host + aarch64/qemu + every audit + every control
    python3 ios/tools/audit_entitlement_config.py
    python3 ios/tools/audit_jit_causal_graph.py
    python3 ios/tools/audit_shm_backend.py
    python3 ios/tools/pass03_negative_controls.py

Expected: host 298 checks / 0 failures, ctest 2/2, AArch64+qemu 289 checks / 0 failures,
STABILIZATION_NEGATIVE_CONTROLS=18/18, FIX06_NEGATIVE_CONTROLS=6/6,
PASS03_NEGATIVE_CONTROLS=19/19, all three new audits = 0 violations,
IOS_BUILD=UNTESTED REASON=NO_APPLE_TOOLCHAIN and
ENTITLEMENT_SIGNATURE_INSPECTION=UNTESTED REASON=NO_APPLE_TOOLCHAIN without a macOS host.

## Not done here (and not claimed)

No Apple CI run, no signing, no provisioning, no push, no remote workflow, no Fase 05.
The next real observations must come from an Apple CI run (APPLE_CI) and from a new physical
run (IPHONE13_PHYSICAL_RUN_03).
