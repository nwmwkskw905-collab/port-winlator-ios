/* ---------------------------------------------------------------------------
   FASE 04 / harness: tests that execute the REAL arm64_prolog.S / arm64_epilog.S
   / arm64_next.S of the tree under test.

   Blocks here are entered by arm64_prolog's `br x1` and leave through
   `b arm64_epilog`, i.e. exactly like emitted dynablocks.  They are written
   with the cpp macro R8REG so the SAME source runs against both register
   models:
        -DR8REG=x9    (Strategy A / patched tree)
        -DR8REG=x18   (upstream v0.4.4)

   Only host scratch registers (x1-x8, x10-x17) are used besides R8REG.

   Globals (defined in t_frame_main.c):
        volatile uint64_t g_frame_expect[]   expected guest R8 at that point
        volatile uint64_t g_frame_result[]   1 = mismatch detected by this code
        volatile uint64_t g_linknext_calls   how often the C stub ran
        volatile uint64_t g_linknext_addr    addr the C stub was given
        volatile uint64_t g_next_block       address the C stub returns
        volatile int      g_canary_fails     count from hf_canary_fail()
   --------------------------------------------------------------------------- */
#ifndef R8REG
#define R8REG x9
#endif

    .text

/* fail(idx): set g_frame_result[idx] = 1 */
    .macro HF_MARK reg, idx
        adrp    x2, g_frame_result
        add     x2, x2, :lo12:g_frame_result
        mov     x4, #1
        str     x4, [x2, #(8 * \idx)]
    .endm

/* check R8REG == g_frame_expect[idx] */
    .macro HF_EXPECT reg, idx
        adrp    x2, g_frame_expect
        add     x2, x2, :lo12:g_frame_expect
        ldr     x3, [x2, #(8 * \idx)]
        cmp     \reg, x3
        b.eq    91f
        HF_MARK \reg, \idx
91:
    .endm

/* ------------------------------------------------------------------ blocks */

/* void hf_block_basic(emu, ...) : entered by the prolog, adds 0x11 to guest R8 */
    .global hf_block_basic
hf_block_basic:
    HF_EXPECT R8REG, 0
    add     R8REG, R8REG, #0x11
    b       arm64_epilog

/* Same, but through a C helper call: shows the emitted spill/reload pattern
   (guest R8 <-> emu->regs[8] = offset 64) that the emitter must generate
   around a `blr` to a C helper, while the prolog/epilog frame is live. */
    .global hf_block_helper
hf_block_helper:
    HF_EXPECT R8REG, 0
    str     R8REG, [x0, #64]        /* the emitter's spill of guest R8 */
    stp     x0, x5, [sp, #-16]!     /* as box64's call_c does: save xEmu for the BLR */
    movz    x6, #0x1111              /* argument for the C helper */
    bl      hf_c_poison              /* BLR(reg): a compiled C callee, clobbers caller-saved */
    ldp     x0, x5, [sp], #16        /* as box64's call_c does: restore xEmu */
    ldr     R8REG, [x0, #64]         /* the emitter's reload of guest R8 */
    HF_EXPECT R8REG, 0               /* R8 must be intact after the C call */
    add     R8REG, R8REG, #0x11
    b       arm64_epilog

/* A block that changes nothing and leaves through the epilog.
   Used for the full-register-file and canary round trips. */
    .global hf_block_plain
hf_block_plain:
    b       arm64_epilog

/* hf_block_helper WITHOUT the spill/reload: negative control for the "
   R8 must be preserved across a helper call" requirement. */
    .global hf_block_helper_nospill
hf_block_helper_nospill:
    stp     x0, x5, [sp, #-16]!
    movz    x6, #0x1111
    bl      hf_c_poison_abi
    ldp     x0, x5, [sp], #16
    add     R8REG, R8REG, #0x11
    b       arm64_epilog

/* Dispatcher test: guest R8 must survive arm64_next's call to the C LinkNext */
    .global hf_block_next
hf_block_next:
    HF_EXPECT R8REG, 1
    movz    x27, #0x4242             /* guest rip (xRIP home), read by arm64_next */
    b       arm64_next

    .global hf_block_next_end
hf_block_next_end:                   /* reached via arm64_next's `br x3` */
    HF_EXPECT R8REG, 1               /* R8 must have survived LinkNext */
    add     R8REG, R8REG, #0x22
    b       arm64_epilog

/* Negative control for the canary checker: corrupt a *saved frame slot* so the
   epilog restores a wrong value.  Frame layout produced by arm64_prolog:
     [x28 + 0] x19,x20   [x28 +16] x21,x22   [x28 +32] x23,x24
     [x28 +48] x25,x26   [x28 +64] x27,x28   [x28 +80] d8..d15
   so [x28 +16] holds the saved x21 and the checker must report canary index 2. */
    .global hf_block_damage
hf_block_damage:
    movz    x5, #0xdead
    str     x5, [x28, #16]
    b       arm64_epilog

/* ------------------------------------------- canary-checking frame run */
/* void hf_run_canaries(void* emu, void* block)
   Saves the C caller's callee-saved registers, poisons them, runs
   prolog -> block -> epilog, and checks that everything came back. */
    .macro HF_CANARY reg, imm, idx
        movz    x5, \imm
        cmp     \reg, x5
        b.eq    92f
        mov     w0, #\idx
        bl      hf_canary_fail
92:
    .endm

    .global hf_run_canaries
hf_run_canaries:
    stp     x29, x30, [sp, #-16]!
    sub     sp,  sp, #160

    /* stash C caller's callee-saved registers */
    stp     x19, x20, [sp, #(8 * 0)]
    stp     x21, x22, [sp, #(8 * 2)]
    stp     x23, x24, [sp, #(8 * 4)]
    stp     x25, x26, [sp, #(8 * 6)]
    stp     x27, x28, [sp, #(8 * 8)]
    stp     d8,  d9,  [sp, #(8 *10)]
    stp     d10, d11, [sp, #(8 *12)]
    stp     d12, d13, [sp, #(8 *14)]
    stp     d14, d15, [sp, #(8 *16)]

    /* poison them: the dynarec frame must hand them back unchanged */
    movz    x19, #0x1919
    movz    x20, #0x2020
    movz    x21, #0x2121
    movz    x22, #0x2222
    movz    x23, #0x2323
    movz    x24, #0x2424
    movz    x25, #0x2525
    movz    x26, #0x2626
    movz    x27, #0x2727
    movz    x28, #0x2828
    movz    x5,  #0x0808
    fmov    d8,  x5
    movz    x5,  #0x1515
    fmov    d15, x5

    bl      arm64_prolog             /* x0 = emu, x1 = block */

    HF_CANARY x19, #0x1919, 0
    HF_CANARY x20, #0x2020, 1
    HF_CANARY x21, #0x2121, 2
    HF_CANARY x22, #0x2222, 3
    HF_CANARY x23, #0x2323, 4
    HF_CANARY x24, #0x2424, 5
    HF_CANARY x25, #0x2525, 6
    HF_CANARY x26, #0x2626, 7
    HF_CANARY x27, #0x2727, 8
    HF_CANARY x28, #0x2828, 9

    movz    x5, #0x0808
    fmov    x6, d8
    cmp     x6, x5
    b.eq    93f
    mov     w0, #10
    bl      hf_canary_fail
93:
    movz    x5, #0x1515
    fmov    x6, d15
    cmp     x6, x5
    b.eq    94f
    mov     w0, #11
    bl      hf_canary_fail
94:
    /* restore the C caller's registers */
    ldp     x19, x20, [sp, #(8 * 0)]
    ldp     x21, x22, [sp, #(8 * 2)]
    ldp     x23, x24, [sp, #(8 * 4)]
    ldp     x25, x26, [sp, #(8 * 6)]
    ldp     x27, x28, [sp, #(8 * 8)]
    ldp     d8,  d9,  [sp, #(8 *10)]
    ldp     d10, d11, [sp, #(8 *12)]
    ldp     d12, d13, [sp, #(8 *14)]
    ldp     d14, d15, [sp, #(8 *16)]
    add     sp,  sp, #160
    ldp     x29, x30, [sp], #16
    ret

/* --------------------------------------------------------- LinkNext stubs */
/* Same signature as box64's src/dynarec/dynarec.c:32:
       void* LinkNext(x64emu_t* emu, uintptr_t addr, void* x2, uintptr_t* x3)
   x3 = slot where the (possibly changed) guest rip is written back.

   Like the real C implementation it POISONS every caller-saved register before
   returning - including x9 and x18, the two candidate homes of guest R8.  That
   is what makes the arm64_next test meaningful: R8 can only survive because
   arm64_next.S saves/restores its register across this call. */
    .global LinkNext
LinkNext:
    adrp    x4, g_linknext_calls
    add     x4, x4, :lo12:g_linknext_calls
    ldr     x5, [x4]
    add     x5, x5, #1
    str     x5, [x4]

    adrp    x4, g_linknext_addr
    add     x4, x4, :lo12:g_linknext_addr
    str     x1, [x4]

    cbz     x3, 95f
    add     x4, x1, #0x1000
    str     x4, [x3]                 /* change the guest rip */
95:
    movz    x1,  #0xbeef             /* poison x1-x8 */
    movz    x2,  #0xbeef
    movz    x3,  #0xbeef
    movz    x4,  #0xbeef
    movz    x5,  #0xbeef
    movz    x6,  #0xbeef
    movz    x7,  #0xbeef
    movz    x8,  #0xbeef
    movz    x9,  #0xbeef             /* poison x9  (Strategy A home of R8) */
    movk    x9,  #0xdead, lsl 16
    movz    x10, #0xbeef
    movz    x11, #0xbeef
    movz    x12, #0xbeef
    movz    x13, #0xbeef
    movz    x14, #0xbeef
    movz    x15, #0xbeef
    movz    x16, #0xbeef
    movz    x17, #0xbeef
    movz    x18, #0xbeef             /* poison x18 (upstream home of R8) */
    movk    x18, #0xdead, lsl 16

    adrp    x4, g_next_block         /* return value, set last */
    add     x4, x4, :lo12:g_next_block
    ldr     x0, [x4]
    ret

    .global LinkNextInvalid
LinkNextInvalid:
    b       LinkNext

/* C helpers used by the blocks above; both implemented in t_frame_main.c:
     hf_c_poison      ordinary compiled C callee (positive tests)
     hf_c_poison_abi  compiled C callee that also clobbers x9 and x18, the two
                      candidate homes of guest R8 (negative control)          */
    .extern hf_c_poison
    .extern hf_c_poison_abi
