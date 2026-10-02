/* ---------------------------------------------------------------------------
   FASE 04 / harness : codegen + execution tests for Strategy A.

   What is real here:
     * the register mapping under test is box64's own arm64_mapping.h
       (selected with -I: include/new = patched, include/orig = upstream v0.4.4);
     * every instruction is produced by box64's own emitter macros
       (include/upstream/arm64_emitter.h, unmodified upstream file);
     * the emitted instructions are really executed (W^X pages: RW -> RX -> call),
       under qemu-aarch64 on this machine => SIMULATOR ONLY, never
       "validated on iPhone".

   What is NOT real here: the dynarec driver (passes, caches, dynablock bookkeeping).
   Those are covered by the per-TU compile checks and by source review, not by this
   program.
   --------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "arm64_mapping.h"          /* -I include/new | include/orig */

static uint32_t g_code[4096];
static int      g_n = 0;
#define EMIT(A) (g_code[g_n++] = (uint32_t)(A))
#include "arm64_emitter.h"          /* -I include/upstream */

#include "emu_layout.h"

extern void harness_clobber(void);            /* clobber.S   */
extern void harness_call_frame(void *, void *); /* frame_call.S */
#define CALL_JIT(emu, code) harness_call_frame((emu), (code))

static int g_pass = 0, g_fail = 0;
static const char *g_art = "artifacts";

#define CHECK(cond, ...)                                            \
    do {                                                            \
        if (cond) { ++g_pass; printf("  PASS  "); }                  \
        else      { ++g_fail; printf("  FAIL  "); }                  \
        printf(__VA_ARGS__); printf("\n");                           \
    } while (0)

/* instruction field decoders (enough for the instruction classes used below) */
#define F_RD(w)  ((int)((w) & 0x1f))
#define F_RN(w)  ((int)(((w) >> 5) & 0x1f))
#define F_RT2(w) ((int)(((w) >> 10) & 0x1f))
#define F_RM(w)  ((int)(((w) >> 16) & 0x1f))
#define F_RT(w)  F_RD(w)

static void dump_words(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.bin", g_art, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    if (fwrite(g_code, 4, (size_t)g_n, f) != (size_t)g_n) perror("fwrite");
    fclose(f);
    printf("  ....  dumped %-24s %3d instructions -> %s\n", name, g_n, path);
}

/* one reusable page, toggled RW -> RX (no RWX anywhere) */
static void *jit_page(void)
{
    static void *p = NULL;
    if (!p) {
        p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) { perror("mmap"); exit(2); }
    }
    return p;
}

static void *jit_seal(void)
{
    void *p = jit_page();
    if (mprotect(p, 4096, PROT_READ | PROT_WRITE)) { perror("mprotect rw"); exit(2); }
    memcpy(p, g_code, (size_t)g_n * 4);
    if (mprotect(p, 4096, PROT_READ | PROT_EXEC)) { perror("mprotect rx"); exit(2); }
    __builtin___clear_cache((char *)p, (char *)p + 4096);
    return p;
}

static void *jit_seal_new(void)
{
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(2); }
    memcpy(p, g_code, (size_t)g_n * 4);
    if (mprotect(p, 4096, PROT_READ | PROT_EXEC)) { perror("mprotect rx"); exit(2); }
    __builtin___clear_cache((char *)p, (char *)p + 4096);
    return p;
}

/* ---------------------------------------------------------------- T1 mapping */
static void t_mapping(void)
{
    printf("[T1] register-map invariants\n");
    int seen[32];
    memset(seen, 0, sizeof seen);
    int dup = 0, has18 = 0;
    for (int i = 0; i < 16; i++) {
        int h = TO_NAT(i);
        if (h < 0 || h > 31 || seen[h]) dup = 1;
        seen[h] = 1;
        if (h == 18) has18 = 1;
    }
    CHECK(!dup, "TO_NAT() maps the 16 guest GPRs to 16 DISTINCT host registers");
    CHECK(!has18, "x18 is not the home of ANY guest GPR");
    CHECK(xR8 == 9, "xR8 == %d (expected 9)", xR8);
    CHECK(TO_NAT(8) == xR8, "TO_NAT(8) == xR8 (%d)", TO_NAT(8));
#ifdef xPLATFORM
    CHECK(xPLATFORM == 18, "xPLATFORM == %d (expected 18)", xPLATFORM);
#endif
    CHECK(IS_GPR(xR8) && IS_GPR(xRAX) && IS_GPR(xRIP),
          "IS_GPR(): the R8 slot, RAX and RIP are guest registers");
    CHECK(!IS_GPR(18), "IS_GPR(18): xPLATFORM is NOT a guest register");
    CHECK(!IS_GPR(8) && !IS_GPR(28), "IS_GPR(): x8 / xSavedSP are NOT guest registers");
    CHECK(seen[xRAX] && seen[xR15] && !seen[8] && !seen[18] && !seen[26] &&
          !seen[27] && !seen[28],
          "TO_NAT's image is exactly the 16 guest GPR homes: x8/x18/xFlags/xRIP/xSavedSP excluded");
    CHECK(!seen[0] && !seen[1] && !seen[2] && !seen[3] && !seen[4] && !seen[5] &&
          !seen[6] && !seen[7] && !seen[29] && !seen[30] && !seen[31],
          "no guest GPR home aliases xEmu, the scratch set, xLP/xLR or SP/ZR");
}

/* -------------------------------------------------- T2 static codegen audit */
static void t_static_codegen(void)
{
    printf("[T2] static codegen audit: every R8 access must encode host reg 9\n");
    g_n = 0;
    MOVx_REG(xR8, xEmu);                 uint32_t w_mov  = g_code[0];
    STRx_U12(xR8, xEmu, 64);             uint32_t w_str  = g_code[1];
    LDRx_U12(xR8, xEmu, 64);             uint32_t w_ldr  = g_code[2];
    STPx_S7_offset(xR8, xR9, xEmu, 64);  uint32_t w_stp  = g_code[3];
    LDPx_S7_offset(xR8, xR9, xEmu, 64);  uint32_t w_ldp  = g_code[4];
    MOVw_REG(wR8, xEmu);                 uint32_t w_movw = g_code[5];
    ADDx_REG(xR8, xR8, x1);              uint32_t w_add  = g_code[6];

    CHECK(F_RD(w_mov) == 9 && F_RM(w_mov) == 0,
          "MOVx_REG(xR8,xEmu)         -> x%d <- x%d (expect 9 <- 0)", F_RD(w_mov), F_RM(w_mov));
    CHECK(F_RT(w_str) == 9 && F_RN(w_str) == 0,
          "STRx_U12(xR8,xEmu,64)      -> str x%d,[x%d,#64]", F_RT(w_str), F_RN(w_str));
    CHECK(F_RT(w_ldr) == 9 && F_RN(w_ldr) == 0,
          "LDRx_U12(xR8,xEmu,64)      -> ldr x%d,[x%d,#64]", F_RT(w_ldr), F_RN(w_ldr));
    CHECK(F_RT(w_stp) == 9 && F_RT2(w_stp) == 19 && F_RN(w_stp) == 0,
          "STPx_S7_offset(xR8,xR9,..) -> stp x%d,x%d,[x%d,#64] (pair stays ONE instruction)",
          F_RT(w_stp), F_RT2(w_stp), F_RN(w_stp));
    CHECK(F_RT(w_ldp) == 9 && F_RT2(w_ldp) == 19,
          "LDPx_S7_offset(xR8,xR9,..) -> ldp x%d,x%d (pair stays ONE instruction)",
          F_RT(w_ldp), F_RT2(w_ldp));
    CHECK(F_RD(w_movw) == 9, "MOVw_REG(wR8,xEmu)         -> w%d", F_RD(w_movw));
    CHECK(F_RD(w_add) == 9 && F_RN(w_add) == 9,
          "ADDx_REG(xR8,xR8,x1)       -> add x%d,x%d,x%d", F_RD(w_add), F_RN(w_add), F_RM(w_add));

    int any18 = 0;
    for (int i = 0; i < g_n; i++) {
        uint32_t w = g_code[i];
        if (F_RD(w) == 18 || F_RN(w) == 18 || F_RT2(w) == 18 || F_RM(w) == 18) any18 = 1;
    }
    CHECK(!any18, "no operand of the %d emitted R8 instructions encodes register 18", g_n);
    dump_words("t2_r8_ops");
}

/* ------------------------------------------------- T3 values / T4 arithmetic */
enum { OP_MOV, OP_ADD, OP_SUB, OP_XOR, OP_AND, OP_OR, OP_SHL, OP_SHR,
       OP_MOV32, OP_ADD32, OP_SUB32, OP_XOR32, OP_AND32, OP_OR32, OP_SHL32, OP_SHR32,
       OP__COUNT };

static const char *op_name(int op)
{
    static const char *n[] = { "MOV","ADD","SUB","XOR","AND","OR","SHL","SHR",
                               "MOV32","ADD32","SUB32","XOR32","AND32","OR32","SHL32","SHR32" };
    return (op >= 0 && op < OP__COUNT) ? n[op] : "?";
}

/* emit:  R8 = <op>(R8, operand);  operand <- emu->h[0];  result -> emu->regs[9] */
static void emit_op(int op)
{
    LDRx_U12(x1, xEmu, H_SCRATCH);          /* operand */
    LDRx_U12(xR8, xEmu, H_REGS(8));         /* R8 */
    switch (op) {
        case OP_MOV:    MOVx_REG(xR8, x1);        break;
        case OP_ADD:    ADDx_REG(xR8, xR8, x1);   break;
        case OP_SUB:    SUBx_REG(xR8, xR8, x1);   break;
        case OP_XOR:    EORx_REG(xR8, xR8, x1);   break;
        case OP_AND:    ANDx_REG(xR8, xR8, x1);   break;
        case OP_OR:     ORRx_REG(xR8, xR8, x1);   break;
        case OP_SHL:    LSLx_IMM(xR8, xR8, 3);    break;
        case OP_SHR:    LSRx_IMM(xR8, xR8, 3);    break;
        case OP_MOV32:  MOVw_REG(wR8, x1);        break;
        case OP_ADD32:  ADDw_REG(wR8, wR8, w1);   break;
        case OP_SUB32:  SUBw_REG(wR8, wR8, w1);   break;
        case OP_XOR32:  EORw_REG(wR8, wR8, w1);   break;
        case OP_AND32:  ANDw_REG(wR8, wR8, w1);   break;
        case OP_OR32:   ORRw_REG(wR8, wR8, w1);   break;
        case OP_SHL32:  LSLw_IMM(wR8, wR8, 3);    break;
        case OP_SHR32:  ORRw_REG_LSR(wR8, xZR, wR8, 3); break;  /* = LSRw_IMM */
        default:        break;
    }
    STRx_U12(xR8, xEmu, H_SCRATCH + 16);    /* result (harness scratch, NOT a guest slot) */
    RET(xLR);
}

static uint64_t ref_op(int op, uint64_t a, uint64_t b)
{
    switch (op) {
        case OP_MOV:   return b;
        case OP_ADD:   return a + b;
        case OP_SUB:   return a - b;
        case OP_XOR:   return a ^ b;
        case OP_AND:   return a & b;
        case OP_OR:    return a | b;
        case OP_SHL:   return a << 3;
        case OP_SHR:   return a >> 3;
        case OP_MOV32: return (uint32_t)b;
        case OP_ADD32: return (uint32_t)((uint32_t)a + (uint32_t)b);
        case OP_SUB32: return (uint32_t)((uint32_t)a - (uint32_t)b);
        case OP_XOR32: return (uint32_t)((uint32_t)a ^ (uint32_t)b);
        case OP_AND32: return (uint32_t)((uint32_t)a & (uint32_t)b);
        case OP_OR32:  return (uint32_t)((uint32_t)a | (uint32_t)b);
        case OP_SHL32: return (uint32_t)((uint32_t)a << 3);
        case OP_SHR32: return (uint32_t)((uint32_t)a >> 3);
    }
    return 0;
}

static void run_op(int op, uint64_t *a_inout, uint64_t b, h_emu_t *emu)
{
    g_n = 0;
    emit_op(op);
    void *fn = jit_seal();   /* a JIT code address: the call happens in frame_call.S */
    emu->regs[8] = *a_inout;
    emu->h[0] = b;
    emu->h[2] = 0;
    CALL_JIT(emu, fn);
    *a_inout = emu->h[2];
}

static const uint64_t g_vals[] = {
    0x0000000000000000UL, 0x0000000000000001UL, 0xffffffffffffffffUL,
    0x0123456789abcdefUL, 0xdeadbeefcafebabeUL, 0x8000000080000000UL,
    0x00000000ffffffffUL, 0xffffffff00000000UL,
};
#define NVALS ((int)(sizeof g_vals / sizeof g_vals[0]))

static void t_values_and_alu(h_emu_t *emu)
{
    printf("[T3] guest R8 values (0, 1, -1, 0x0123456789ABCDEF, mixed) round-trip\n");
    for (int i = 0; i < NVALS; i++) {
        for (int w32 = 0; w32 < 2; w32++) {
            g_n = 0;
            LDRx_U12(x1, xEmu, H_SCRATCH);
            if (w32) MOVw_REG(wR8, x1); else MOVx_REG(xR8, x1);
            STRx_U12(xR8, xEmu, H_SCRATCH + 16);
            RET(xLR);
            emu->h[0] = g_vals[i];
            emu->h[2] = 0;
            CALL_JIT(emu, jit_seal());
            uint64_t want = w32 ? (uint32_t)g_vals[i] : g_vals[i];
            CHECK(emu->h[2] == want, "MOV%s R8 = %#lx -> read back %#lx",
                  w32 ? "32" : "", (unsigned long)g_vals[i],
                  (unsigned long)emu->h[2]);
        }
    }

    printf("[T4] arithmetic/logic on R8 (64- and 32-bit) vs reference model\n");
    int mismatches = 0, cases = 0;
    for (int op = 0; op < OP__COUNT; op++) {
        for (int i = 0; i < NVALS; i++) {
            uint64_t a = g_vals[i], b = g_vals[(i + 3) % NVALS];
            uint64_t got = a;
            run_op(op, &got, b, emu);
            uint64_t want = ref_op(op, a, b);
            ++cases;
            if (got != want) {
                ++mismatches;
                if (mismatches < 6)
                    printf("  FAIL  %-6s a=%#018lx b=%#018lx got=%#018lx want=%#018lx\n",
                           op_name(op), (unsigned long)a, (unsigned long)b,
                           (unsigned long)got, (unsigned long)want);
            }
        }
    }
    CHECK(mismatches == 0, "%d ALU cases executed, %d mismatches vs reference model",
          cases, mismatches);

    printf("[T5] the other 15 guest registers are not disturbed by R8 traffic\n");
    g_n = 0;
    emit_op(OP_ADD);
    void *fn = (void *)jit_seal();
    for (int i = 0; i < 16; i++) emu->regs[i] = 0x1000 + (uint64_t)i;
    emu->regs[8] = 5;
    emu->h[0] = 1;
    CALL_JIT(emu, fn);
    int disturbed = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 8) continue;
        if (emu->regs[i] != (uint64_t)(0x1000 + i)) disturbed++;
    }
    CHECK(disturbed == 0, "regs[0..7,9..15] unchanged while regs[8] was modified (%d disturbed)",
          disturbed);
}

/* ------------------------------------------------------------- T6 pressure */
static void t_pressure(h_emu_t *emu)
{
    printf("[T6] register pressure: all 16 guest GPRs live at once, each in its own home\n");
    for (int rep = 0; rep < 3; rep++) {
        g_n = 0;
        MOVZx(x1, 1);
        for (int i = 0; i < 16; i++) LDRx_U12(TO_NAT(i), xEmu, H_REGS(i));
        for (int i = 0; i < 16; i++) ADDx_REG(TO_NAT(i), TO_NAT(i), x1);
        for (int i = 0; i < 16; i++) STRx_U12(TO_NAT(i), xEmu, H_REGS(i));
        RET(xLR);
        void *fn = (void *)jit_seal();
        for (int i = 0; i < 16; i++) emu->regs[i] = 0xabc0000000000000UL + (uint64_t)i;
        CALL_JIT(emu, fn);
        int bad = 0;
        for (int i = 0; i < 16; i++)
            if (emu->regs[i] != 0xabc0000000000001UL + (uint64_t)i) bad++;
        CHECK(bad == 0, "round %d: 16 GPRs loaded, incremented, stored back (%d wrong)", rep, bad);
    }
}

/* --------------------------------------------- T7 helper-call preservation */
static int run_preserve(int with_spill, uint64_t start, h_emu_t *emu)
{
    g_n = 0;
    /* A dynablock is not entered with an LR it must keep: box64's arm64_prolog/
       arm64_epilog own the return path, so helper calls may BLR freely.  This
       fragment therefore stashes LR in harness scratch instead of assuming the
       LR survives the call below. */
    STRx_U12(xLR, xEmu, H_SCRATCH + 24);
    LDRx_U12(xR8, xEmu, H_REGS(8));
    if (with_spill) STRx_U12(xR8, xEmu, H_REGS(8));      /* the Strategy A pattern */
    LDRx_U12(x3, xEmu, H_SCRATCH);                        /* callee address */
    BLR(x3);                                              /* host call may clobber x9/x18 */
    if (with_spill) LDRx_U12(xR8, xEmu, H_REGS(8));
    STRx_U12(xR8, xEmu, H_SCRATCH + 8);                   /* observed value */
    LDRx_U12(xLR, xEmu, H_SCRATCH + 24);
    RET(xLR);
    void *fn = (void *)jit_seal();
    emu->regs[8] = start;
    emu->h[0] = (uint64_t)(uintptr_t)&harness_clobber;
    emu->h[1] = 0;
    CALL_JIT(emu, fn);
    return emu->h[1] == start;
}

static void t_helper_preserve(h_emu_t *emu)
{
    printf("[T7] helper-call preservation across a platform-ABI host call\n");
    const uint64_t v = 0x0123456789abcdefUL;

    g_n = 0;                                   /* dumps for the objdump audit */
    STRx_U12(xLR, xEmu, H_SCRATCH + 24);
    LDRx_U12(xR8, xEmu, H_REGS(8));
    STRx_U12(xR8, xEmu, H_REGS(8));
    LDRx_U12(x3, xEmu, H_SCRATCH);
    BLR(x3);
    LDRx_U12(xR8, xEmu, H_REGS(8));
    STRx_U12(xR8, xEmu, H_SCRATCH + 8);
    LDRx_U12(xLR, xEmu, H_SCRATCH + 24);
    RET(xLR);
    dump_words("t7_boundary_spilled");

    g_n = 0;
    STRx_U12(xLR, xEmu, H_SCRATCH + 24);
    LDRx_U12(xR8, xEmu, H_REGS(8));
    LDRx_U12(x3, xEmu, H_SCRATCH);
    BLR(x3);
    STRx_U12(xR8, xEmu, H_SCRATCH + 8);
    LDRx_U12(xLR, xEmu, H_SCRATCH + 24);
    RET(xLR);
    dump_words("t7_boundary_nospill");

    CHECK(run_preserve(1, v, emu) == 1,
          "WITH the spill (Strategy A): R8 survives a host call that clobbers its home register");
    CHECK(run_preserve(1, 0, emu) == 1, "WITH the spill: R8 = 0 survives too");
    CHECK(run_preserve(1, ~0ULL, emu) == 1, "WITH the spill: R8 = 0xffffffffffffffff survives too");
    CHECK(run_preserve(0, v, emu) == 0,
          "NEGATIVE CONTROL, no spill: R8 is lost (so the test can detect a missing spill)");
}

/* ------------------------------------------------- T8 cross-block A->B->C->D */
static void t_chain(h_emu_t *emu)
{
    printf("[T8] cross-block persistence A->B->C->D through indirect jumps (dispatch shape)\n");
    void *blk[4];
    static const uint64_t add[4] = { 0x11, 0x2200, 0x330000, 0x44000000UL };
    for (int i = 0; i < 4; i++) {
        g_n = 0;
        if (i == 0) LDRx_U12(xR8, xEmu, H_REGS(8));    /* what arm64_prolog does on entry */
        LDRx_U12(x1, xEmu, H_SCRATCH + 8 * (1 + i));   /* per-block increment */
        ADDx_REG(xR8, xR8, x1);                        /* touch guest R8 */
        STRx_U12(xR8, xEmu, H_SCRATCH + 8 * (10 + i)); /* trace: R8 after this block */
        if (i < 3) {
            LDRx_U12(x3, xEmu, H_SCRATCH + 8 * (5 + i)); /* next block address */
            BR(x3);                                      /* dispatch: indirect jump, no spill */
        } else {
            RET(xLR);
        }
        blk[i] = jit_seal_new();
    }
    for (int i = 0; i < 4; i++) emu->h[1 + i] = add[i];
    for (int i = 0; i < 3; i++) emu->h[5 + i] = (uint64_t)(uintptr_t)blk[i + 1];

    uint64_t start = 0x100000000UL;
    emu->regs[8] = start;
    for (int i = 0; i < 4; i++) emu->h[10 + i] = 0;
    CALL_JIT(emu, blk[0]);

    uint64_t want = start;
    int bad = 0;
    for (int i = 0; i < 4; i++) {
        want += add[i];
        if (emu->h[10 + i] != want) bad++;
        printf("  ....  after block %c: R8 = %#lx (expected %#lx)\n", 'A' + i,
               (unsigned long)emu->h[10 + i], (unsigned long)want);
    }
    CHECK(bad == 0, "R8 survives A->B->C->D through indirect jumps (%d wrong hops)", bad);
}

/* ------------------------------------------------------------ T9 fuzz (seed) */
static uint64_t g_rng;
static uint64_t rng(void)
{
    g_rng = g_rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return g_rng ^ (g_rng >> 29);
}

static void t_fuzz(h_emu_t *emu, uint64_t seed, int iters)
{
    printf("[T9] deterministic fuzz vs reference model (seed=%#lx, %d iterations)\n",
           (unsigned long)seed, iters);
    g_rng = seed;
    int bad = 0;
    uint64_t a = rng();
    for (int i = 0; i < iters; i++) {
        int op = (int)(rng() % OP__COUNT);
        uint64_t b = rng();
        if ((i % 97) == 0) b = 0;
        if ((i % 131) == 0) b = ~0ULL;
        uint64_t before = a;
        run_op(op, &a, b, emu);
        uint64_t want = ref_op(op, before, b);
        if (a != want) {
            if (bad < 5)
                printf("  FAIL  iter %d %s a=%#lx b=%#lx got=%#lx want=%#lx\n", i,
                       op_name(op), (unsigned long)before, (unsigned long)b,
                       (unsigned long)a, (unsigned long)want);
            ++bad;
        }
        a ^= (uint64_t)i * 0x9e3779b97f4a7c15UL;
    }
    CHECK(bad == 0, "%d fuzz iterations, %d divergences from the reference model", iters, bad);
}

int main(int argc, char **argv)
{
    if (argc > 1) g_art = argv[1];
    uint64_t seed = (argc > 2) ? strtoull(argv[2], NULL, 0) : 0x5eedf04UL;
    int iters = (argc > 3) ? atoi(argv[3]) : 2000;

    setvbuf(stdout, NULL, _IONBF, 0);
    h_emu_t *emu = aligned_alloc(64, sizeof *emu);
    memset(emu, 0, sizeof *emu);

    printf("=== FASE 04 harness: codegen tests ===\n");
    printf("mapping under test: %s (xR8=%d, TO_NAT(8)=%d)\n",
           (xR8 == 9) ? "Strategy A / patched" : "upstream v0.4.4", xR8, TO_NAT(8));

    t_mapping();
    t_static_codegen();
    t_values_and_alu(emu);
    t_pressure(emu);
    t_helper_preserve(emu);
    t_chain(emu);
    t_fuzz(emu, seed, iters);

    printf("=== %d checks passed, %d failed ===\n", g_pass, g_fail);
    free(emu);
    return g_fail ? 1 : 0;
}
