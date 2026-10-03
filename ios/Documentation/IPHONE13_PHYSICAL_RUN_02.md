# IPHONE13_PHYSICAL_RUN_02 — physical iOS run after Correction 06

* **Run id:** `IPHONE13_PHYSICAL_RUN_02`
* **Date:** as submitted with the PASS 03 brief (campaign of 2026-10-02/03; the run carried no
  separate stamp of its own).
* **Source of this document:** the physical run report supplied with the PASS 03 brief. The
  counts, the non-PASS records and the capability details below are transcribed from it; the
  item-level detail of the passing records is summarised from the same report (see §3). Nothing
  here is generated, simulated or inferred from a host run: `AARCH64_QEMU` and
  `MACOS_RUNNER_ARM64` results are never reused as iOS evidence.
* **Evidence class:** `IPHONE_DEVICE` (physical iPhone 13, `ios-device`).
* **Public from the run, verbatim:**

```
platform=darwin page_size=16384 isa=aarch64 apple_target=ios-device
records=52 assertions=0 pass=43 fail=0 blocked=5 unsupported=0 untested=1 not_applicable=3 summary=BLOCKED
```

* **Immutable verdict of this run:**

```
PHASE_02_PHYSICAL_VALIDATION=BLOCKED
```

This document is an immutable physical record. It is never deleted, never rewritten and never
reinterpreted: a later run adds a new document, it does not edit this one. Pass 03 changes the
code that will produce the *next* lines; it does not change these.

## 1. Comparison with IPHONE13_PHYSICAL_RUN_01

| Quantity | RUN_01 | RUN_02 | Meaning |
|---|---|---|---|
| `pass` | 42 | **43** | one more record really ran and passed |
| `fail` | 4 | **0** | every code defect of RUN_01 is gone; this number is preserved |
| `blocked` | 1 | 5 | four more records are now *correctly* attributed to capabilities instead of being printed as failures or as successes |
| `unsupported` | 1 | 0 | `memory.dual_mapping_rw_rx` is no longer reported as “no mechanism at all”: the failing **stage** (`shm_open`) is known |
| `untested` | 1 | 1 | unchanged: `jit.execution_allowed` |
| `not_applicable` | 3 | 3 | unchanged, including `jit.write_protect_np` (iOS API property) |
| `records` | 52 | 52 | the same suite, no test removed and no assertion dropped |
| `summary` | `FAIL` | `BLOCKED` | no code failure remains; the remaining items are capabilities this signing context was not granted |

The four RUN_01 failures — `jit.alloc` (`arena allocation failed errno=1`), `fs.deep_paths`
(`unexpected errno=0 … depth=103 (deepest ok=102, 1007 chars)`), `ipc.posix_shm` (an errno with no
operation) and `loader.run_valid_module` (`rejected: OK`) — are all absent from RUN_02, and the
code paths that produced them are the ones Correction 06 rewrote.

## 2. The records that are not PASS (verbatim detail)

```
memory.dual_mapping_rw_rx  BLOCKED         stage=shm_open refused errno=1 (Operation not permitted)
jit.map_jit_probe          BLOCKED         MAP_JIT refused errno=1 (Operation not permitted) (target=ios-device)
jit.alloc                  BLOCKED         arena allocation refused errno=1 (Operation not permitted); depends on jit.map_jit_probe
ipc.posix_shm              BLOCKED         stage=shm_open refused errno=1 (Operation not permitted)
loader.run_valid_module    BLOCKED         image validated and accepted; entry point not executable (JIT unavailable)
jit.write_protect_np       NOT_APPLICABLE  the iPhoneOS SDK marks the call unavailable for iOS
jit.execution_allowed      UNTESTED        cannot be established while the executable arena is refused
```

Three of the five `BLOCKED` records are one cause each, and two of them are the *same* cause:
`jit.map_jit_probe`, `jit.alloc` and `loader.run_valid_module` are the MAP_JIT/JIT-permission
chain (probe → arena → loader execution). The other two (`memory.dual_mapping_rw_rx`,
`ipc.posix_shm`) share the second cause: the **named POSIX shared-memory namespace** refused to
this app in the sandbox at `shm_open`.

## 3. The measurements that passed and must not be disturbed

43 records passed. The groups RUN_02 reports as passing include, and RUN_01 already listed:
`memory.reserve_rw`, `memory.read_rw`, `memory.rw_to_r_transition`, `memory.write_to_r_faults`,
`memory.r_to_rw_transition`, `memory.rw_to_rx_transition`, `memory.rx_to_rw_transition`,
`memory.wx_policy` (strict W^X available), `memory.release`, `jit.emit_payload` (ISA=aarch64,
8 bytes for `return 42`), the CPU facts (page size 16384, endianness, register notes, x18),
the thread suite (pthread/TLS/mutex/cond/atomics), the signal suite, the filesystem suite
including `fs.deep_paths` (`depth=102`, 1007 characters, `depth=103` → errno 63/`ENAMETOOLONG`,
`PATH_MAX=1024`), the IPC suite (`socketpair`, `AF_UNIX`, `SCM_RIGHTS`, `pipe`, `sun_path_limit`,
`kqueue`), the loader image construction and every invalid-module rejection.

Two facts from that list matter for pass 03 and are used as evidence, not as assumptions:

* `memory.rw_to_rx_transition = PASS` and `memory.wx_policy = PASS` — on **this device**, a single
  view can be flipped RW → R-X and combined RWX is refused. Single-view W^X is therefore the
  mechanism the iOS backend may rely on, and it needs no MAP_JIT.
* `jit.emit_payload = PASS` — the aarch64 emitter works on the device. What has never happened on
  the device is the *execution* of an emitted payload: the arena was refused before that step, in
  both RUN_01 and RUN_02. Pass 03 does not claim otherwise.

## 4. What RUN_02 does and does not prove

Proves: no code defect survives in the RUN_01 set; the classification vocabulary (capability vs
defect vs undetermined) now describes the physical reality; the failing *stage* is known for both
shared-memory records.

Does **not** prove: that JIT is impossible on iOS in general, that shared memory is impossible, or
that dual mapping is impossible. It proves that *this* signing context was not granted MAP_JIT,
and that *this* sandbox refuses the *named* POSIX shared-memory namespace. The final MAP_JIT
answer is a property of signing + installation + physical execution, and only a new physical run
can decide it.

## 5. What pass 03 does about it

1. The MAP_JIT capability stays `BLOCKED` — it is not converted into anything else, and no
   entitlement is claimed to obtain it.
2. `jit.alloc` no longer dies with the capability: the MAP_JIT refusal is recorded and the arena
   is obtained through the platform's supported single-view W^X mechanism, whose executable step
   is verified by **real execution** — so the next run either executes the payload or reports
   exactly where it was refused.
3. The loader uses the arena's own write protocol per kind, so a valid module can be mapped and
   executed when the platform allows it, and reports the real errno when it does not.
4. The dual-mapping experiment keeps its measurement and gains an iOS-side backing (a
   file-backed `MAP_SHARED` object inside the app container) that the sandbox can grant, so the
   next run separates “the named namespace is refused” from “this device refuses an executable
   alias”.
5. `ipc.posix_shm` remains `BLOCKED` as measured — with a note stating what it does *not* mean.

## 6. Next physical objective

`IPHONE13_PHYSICAL_RUN_03`, built from pass 03, is the only way to turn any of the records above
into a new result. The expected honest outcomes: `jit.map_jit_probe` still `BLOCKED` (unless the
signing context changes), `jit.alloc`/`jit.execution_allowed`/`loader.run_valid_module` decided by
real execution, `memory.dual_mapping_rw_rx` decided by the iOS backend, and `fail = 0` preserved.
