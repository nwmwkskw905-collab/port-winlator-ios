# IPHONE13_PHYSICAL_RUN_01 — physical iOS baseline

* **Run id:** `IPHONE13_PHYSICAL_RUN_01`
* **Date:** 2026-10-02
* **Evidence class:** `IPHONE_DEVICE` (physical iPhone 13). This is the **only** run that may be
  labelled `IPHONE_DEVICE` in this reconstruction; `MACOS_RUNNER_ARM64` and `AARCH64_QEMU`
  results are never reused as iOS evidence.
* **Public from the run, verbatim:**

```
platform=darwin page_size=16384 isa=aarch64 apple_target=ios-device
records=52 assertions=0 pass=42 fail=4 blocked=1 unsupported=1 untested=1 not_applicable=3 summary=FAIL
```

* **Immutable verdict of this run:**

```
PHASE_02_PHYSICAL_VALIDATION=FAIL
```

This document is the physical baseline. It is never deleted, never rewritten and never
reinterpreted: a later run adds a new document, it does not edit this one. Correction 06 changes
the code that produced these lines; it does not change these lines.

## 1. The four failures, verbatim

| Test | Status | Detail as printed by the device |
|---|---|---|
| `jit.alloc` | `FAIL` | `arena allocation failed errno=1 (Operation not permitted)` |
| `fs.deep_paths` | `FAIL` | `unexpected errno=0 (Undefined error: 0) at depth=103 (deepest ok=102, 1007 chars)` |
| `ipc.posix_shm` | `FAIL` | `shared memory object mapped twice and compared (errno=1)` |
| `loader.run_valid_module` | `FAIL` | `rejected: OK` |

## 2. The measurements that passed and must not be disturbed

```
memory.reserve_rw            = PASS
memory.read_rw               = PASS
memory.rw_to_r_transition    = PASS
memory.write_to_r_faults     = PASS
memory.r_to_rw_transition    = PASS
memory.rw_to_rx_transition   = PASS
memory.rx_to_rw_transition   = PASS
memory.wx_policy             = PASS    RWX refused errno=13 (Permission denied): strict W^X is available
memory.release               = PASS
jit.emit_payload             = PASS    ISA=aarch64 emitted 8 bytes for 'return 42'
```

## 3. The findings that are not failures (and must not be converted into PASS)

| Test | Status | Detail |
|---|---|---|
| `memory.dual_mapping_rw_rx` | `UNSUPPORTED` | `second (executable) view refused errno=1 (Operation not permitted); host=darwin` |
| `jit.map_jit_probe` | `BLOCKED` | `MAP_JIT refused errno=1 (Operation not permitted) (target=ios-device)` |
| `jit.write_protect_np` | `NOT_APPLICABLE` | the iPhoneOS SDK marks the call unavailable for iOS |
| `jit.execution_allowed` | `UNTESTED` | cannot be established while the executable mapping is refused |

`jit.map_jit_probe = BLOCKED` is a capability this signature did not grant. It is not proof that
JIT works (nothing was executed), and it is not a defect of the port.

## 4. What Correction 06 does about it

Each `FAIL` above received an individual root cause, established from the code that produced the
line (see `Documentation/RELATORIO_FASE_02_RECONSTRUIDA.md`, section 15):

1. `jit.alloc` failed with the same `errno=1` as the blocked probe, but the arena call recorded
   "MAP_JIT attempted" only on success, so the report could not connect the two. It can now, and
   the verdict becomes the dependency `BLOCKED` **only** when the attempt really happened, the
   probe really measured a refusal, and both errnos are equal and non-zero.
2. `fs.deep_paths` printed `errno=0` because the error path read an out-parameter it had never
   written: the real errno was discarded. The errno is now captured where it happens, the stage is
   named, and `errno=0` can no longer be reported as anything but a defect of the error path.
3. `ipc.posix_shm` printed an errno without the operation that produced it. The stage
   (`shm_open`, `ftruncate`, `mmap(second view)`, `compare`, …) is now part of the record, and the
   classification follows the measured stage instead of external knowledge.
4. `loader.run_valid_module` printed `rejected: OK` because the loader returned a failure status
   without writing any reason. The loader now returns `BLOCKED` with
   `RT_LOADER_ERR_JIT_UNAVAILABLE` and the errno for a valid module that cannot get an executable
   arena, and a non-`PASS` return can never carry the reason `OK` again.

## 5. Next physical objective

`IPHONE13_PHYSICAL_RUN_02`, built from Correction 06, is the only way to turn any of the four
lines above into a new result. `MAP_JIT` is not required to pass: without a genuinely granted JIT
entitlement the honest outcome stays `BLOCKED`.
