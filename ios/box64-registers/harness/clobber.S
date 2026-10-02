/* ---------------------------------------------------------------------------
   FASE 04 / harness: a "host function with a platform clobber contract".

   It writes recognisable garbage into every caller-saved register that the
   platform ABI (AAPCS64) allows a callee to clobber, with two deliberate
   exceptions that mirror how box64 actually calls helpers:

     * x0 (xEmu) is left alone: every box64 call site preserves the emu pointer
       by construction (call_c/call_n push it on the stack; checkCRC keeps it in
       x6 and restores it; the flags block uses it as its base).
     * x6 is left alone: arm64_crc / arm64_x31_hash (arm64_lock.S) only use
       x0-x4, and checkCRC keeps xEmu in x6 across the call.

   x19-x28 and d8-d15 are callee-saved and are deliberately NOT touched, exactly
   like a real host function.

   x18 IS clobbered on purpose: on arm64-linux x18 is caller-saved, so a host
   function is entitled to destroy it.  This is the property that makes the old
   "guest R8 lives in x18" model unsafe and that Strategy A removes.
   --------------------------------------------------------------------------- */
    .text
    .global harness_clobber
harness_clobber:
    movz    x1,  #0xbeef
    movz    x2,  #0xbeef
    movz    x3,  #0xbeef
    movz    x4,  #0xbeef
    movz    x5,  #0xbeef
    movz    x7,  #0xbeef
    movz    x8,  #0xbeef
    movz    x9,  #0xbeef
    movz    x10, #0xbeef
    movz    x11, #0xbeef
    movz    x12, #0xbeef
    movz    x13, #0xbeef
    movz    x14, #0xbeef
    movz    x15, #0xbeef
    movz    x16, #0xbeef
    movz    x17, #0xbeef
    movz    x18, #0xbeef          /* the interesting one */
    movk    x18, #0xdead, lsl 16
    ret
