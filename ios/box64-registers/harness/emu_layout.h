/* ---------------------------------------------------------------------------
   FASE 04 - harness: layout of the emulated x64emu_t register area.

   This mirrors the part of box64's x64emu_t that the dynarec addresses by
   offset:
       regs[16]  at offset 0   (RAX,RCX,RDX,RBX,RSP,RBP,RSI,RDI,R8,R9,...,R15)
       eflags    at offset 128
       ip        at offset 136
   Verified against the source under test:
     arm64_prolog.S : "ldp x26, x27, [x0, (8 * 16)]"  -> x26 = flags, x27 = ip
     dynarec_arm64_helper.h : STORE_XEMU_CALL(xRIP) -> STR to offsetof(x64emu_t, ip)
     arm64_mapping.h : xRAX..xR15 = regs[0..15], guest R8 = regs[8] = byte 64
   The harness only relies on these offsets, never on the full struct.
   --------------------------------------------------------------------------- */
#ifndef HARNESS_EMU_LAYOUT_H
#define HARNESS_EMU_LAYOUT_H

#include <stdint.h>

typedef struct {
    uint64_t regs[16];   /*   0..127 : guest GPRs (regs[8] = guest R8, byte 64) */
    uint64_t flags;      /*     128 : guest eflags / xFlags                    */
    uint64_t ip;         /*     136 : guest RIP / xRIP                         */
    uint64_t h[16];      /* 144..   : harness scratch (callee ptr, in/out)      */
} h_emu_t;

#define H_REGS(i)   ((int)((i) * 8))
#define H_FLAGS     128
#define H_IP        136
#define H_SCRATCH   144

#endif /* HARNESS_EMU_LAYOUT_H */
