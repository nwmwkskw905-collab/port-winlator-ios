// FASE 04 / register-strategy evidence probe
//
// Question 1: do AArch64 STP/LDP accept NON-CONSECUTIVE register pairs?
//   Box64 ARM64 stores guest R8/R9 with  STPx_S7_offset(xR8, xR9, xEmu, regs[_R8])
//   i.e. one instruction covering two adjacent *memory* slots but, under the
//   current mapping, two adjacent *registers* (x18, x19).  Any remap of guest
//   R8 must keep the store legal or split it into two STR.
//
// Question 2: is the memory order "first register -> lowest address"?
//   Needed to know whether LDP xA,xB,[...] reads back the same pairing.
//
// Question 3: does a high register (x27, used by arm64_next.S in
//   `stp x18, x27, [sp, (8*10)]`) also form a legal non-consecutive pair?
//
// This probe only measures the *emitter/ISA* constraint.  It says nothing about
// whether x22/x23 are live at those points - that is a liveness question and is
// answered separately (see the report's liveness section).
    .text
    .global pair_test
pair_test:
    // x0 = buffer of 16 unsigned long
    mov  x9,  #0x1111
    mov  x19, #0x2222
    // non-consecutive pair, both halves written by one STP
    stp  x9, x19, [x0, #32]          // expects [x0+32]=0x1111, [x0+40]=0x2222
    mov  x9,  #0
    mov  x19, #0
    // read them back with a non-consecutive LDP
    ldp  x9, x19, [x0, #32]
    str  x9,  [x0, #48]              // expect 0x1111
    str  x19, [x0, #56]              // expect 0x2222

    // non-consecutive pair using a high register (x27), the arm64_next.S shape
    mov  x27, #0x3333
    stp  x9, x27, [x0, #64]          // expects [x0+64]=0x1111, [x0+72]=0x3333
    str  x27, [x0, #80]              // x27 must survive the stp untouched
    ret
