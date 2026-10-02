/* ---------------------------------------------------------------------------
   FASE 04 / harness: minimal stand-in for the dynablock frame.

   A dynablock is NOT a normal function: it freely uses x19-x28 and d8-d15 for
   guest state (guest R9..R15, xFlags, xRIP, xSavedSP).  box64 makes that legal
   by wrapping block execution in arm64_prolog / arm64_epilog, which save and
   restore exactly those callee-saved registers.

   The codegen tests call raw emitted fragments, so they need the same thing.
   This wrapper does the saving/restoring part of prolog+epilog (without the
   guest register reload, which the fragments do themselves).

       void harness_call_frame(void* emu, void* code);   // x0 = emu, x1 = code

   Note: it saves/restores x19-x28 and d8-d15, i.e. the same set the real
   arm64_prolog.S/arm64_epilog.S handle outside the .S files under test.
   --------------------------------------------------------------------------- */
    .text
    .global harness_call_frame
harness_call_frame:
    stp     x30, x29, [sp, #-16]!
    sub     sp,  sp, #(8 * 12)
    stp     x19, x20, [sp, #(8 * 0)]
    stp     x21, x22, [sp, #(8 * 2)]
    stp     x23, x24, [sp, #(8 * 4)]
    stp     x25, x26, [sp, #(8 * 6)]
    stp     x27, x28, [sp, #(8 * 8)]
    stp     d8,  d9,  [sp, #(8 * 10)]
    /* d10-d15 are saved by the caller-generated code; the fragments under test
       never touch FP registers, so only d8/d9 are needed here */
    blr     x1
    ldp     x19, x20, [sp, #(8 * 0)]
    ldp     x21, x22, [sp, #(8 * 2)]
    ldp     x23, x24, [sp, #(8 * 4)]
    ldp     x25, x26, [sp, #(8 * 6)]
    ldp     x27, x28, [sp, #(8 * 8)]
    ldp     d8,  d9,  [sp, #(8 * 10)]
    add     sp,  sp, #(8 * 12)
    ldp     x30, x29, [sp], #16
    ret
