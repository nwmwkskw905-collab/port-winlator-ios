/* ---------------------------------------------------------------------------
   FASE 04 / harness: driver for the *frame* tests (t_frame.S).

   These tests execute the real arm64_prolog.S / arm64_epilog.S / arm64_next.S
   of the tree under test - not a re-implementation of them.

   Build-time knobs
     -DR8REG=x9            patched tree (guest R8 in host x9, Strategy A)
     -DR8REG=x18           upstream v0.4.4 (guest R8 in host x18)
   Note on the negative control: the value of guest R8 is lost across an
   unspilled helper call only if the callee actually touches R8's home.  The
   ordinary compiled helper below happens NOT to use x9 (measured: x0-x8 only),
   which is exactly why host testing cannot settle the question - so the control
   uses hf_c_poison_abi, an otherwise ordinary compiled C function whose inline
   asm forcibly clobbers BOTH candidate homes x9 and x18.  That is the clobber
   set a C callee is allowed to use on Darwin (x18 is caller-saved there).
   --------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "emu_layout.h"

typedef void (*dynablock_fn)(void);   /* entered by arm64_prolog's `br x1` */

extern void arm64_prolog(void* emu, dynablock_fn block);
extern void arm64_epilog(void);
extern void arm64_next(void);

extern void hf_block_basic(void);
extern void hf_block_helper(void);
extern void hf_block_helper_nospill(void);
extern void hf_block_next(void);
extern void hf_block_next_end(void);
extern void hf_block_plain(void);
extern void hf_block_damage(void);
extern void hf_run_canaries(void* emu, dynablock_fn block);

volatile uint64_t g_frame_expect[2];
volatile uint64_t g_frame_result[2];
volatile uint64_t g_linknext_calls;
volatile uint64_t g_linknext_addr;
volatile uint64_t g_next_block;
volatile int      g_canary_fails;
volatile int      g_canary_idx[16];

static int checks = 0, failed = 0;

static void check(int ok, const char* what)
{
    ++checks;
    if(ok) {
        printf("  PASS  %s\n", what);
    } else {
        ++failed;
        printf("  FAIL  %s\n", what);
    }
}

void hf_canary_fail(int idx)
{
    int n = g_canary_fails;
    if(n < 16)
        g_canary_idx[n] = idx;
    g_canary_fails = n + 1;
}

static void clear_canary_idx(void)   /* plain loop: g_canary_idx is volatile */
{
    int k;
    for(k = 0; k < 16; ++k)
        g_canary_idx[k] = 0;
}

/* A plain C callee.  It must be a *real* compiled function: it is the host
   compiler, not hand-written assembly, that clobbers caller-saved registers
   here.  Several independent chains keep many registers live. */
volatile uint64_t g_poison_sink;

__attribute__((noinline)) uint64_t hf_c_poison(uint64_t x)
{
    uint64_t a = x ^ 0x0123456789abcdefULL;
    uint64_t b = (x * 6364136223846793005ULL) + 1442695040888963407ULL;
    uint64_t c = (a >> 7) | (b << 11);
    uint64_t d = a + c * 3;
    uint64_t e = (b ^ d) + (a << 5);
    uint64_t f = (e * 5) + (c ^ 0x5555555555555555ULL);
    uint64_t g = ((d + f) >> 13) ^ (e + b);
    uint64_t h = (g * 1103515245ULL) + (f >> 3);
    g_poison_sink = a ^ b ^ c ^ d ^ e ^ f ^ g ^ h;
    return h + 1;
}

/* Same, plus a forced clobber of x9 and of x18 - i.e. the freedom a C callee
   has on Darwin for both candidate homes of guest R8.  The function is still
   compiled C: only the clobber is stated explicitly. */
__attribute__((noinline)) uint64_t hf_c_poison_abi(uint64_t x)
{
    uint64_t r;
    __asm__ volatile("movz x9, #0xbeef\n\t"
                     "movk x9, #0xdead, lsl 16\n\t"
                     "movz x18, #0xbeef\n\t"
                     "movk x18, #0xdead, lsl 16\n\t"
                     "add  %0, %1, #1"
                     : "=r"(r) : "r"(x) : "x9", "x18");
    g_poison_sink = x ^ r;
    return r;
}

/* deterministic pseudo-random values (same generator style as the codegen fuzz) */
static uint64_t g_seed = 0x5eedf04ULL;
static uint64_t next_rand(void)
{
    g_seed = g_seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return g_seed ^ (g_seed >> 33);
}

int main(void)
{
    static const uint64_t r8_values[] = {
        0x0000000000000000ULL,
        0x0000000000000001ULL,
        0xffffffffffffffffULL,
        0x0123456789abcdefULL,
    };
    h_emu_t* emu = calloc(1, 8192);
    if(!emu) {
        printf("cannot allocate emu\n");
        return 2;
    }

    printf("=== FASE 04 harness: dynarec frame tests (prolog/epilog/next) ===\n");
#ifdef MODEL_NAME
    printf("model under test: %s\n", MODEL_NAME);
#endif
    printf("negative-control clobber set: x9 and x18 (Darwin caller-saved freedom)\n");

    /* ---------------- T10: guest R8 round trip through prolog/epilog -------- */
    printf("[T10] guest R8 round trip through the real arm64_prolog/arm64_epilog\n");
    {
        uint64_t vals[16];
        int n = 0, i;
        for(i = 0; i < 4; ++i)
            vals[n++] = r8_values[i];
        for(i = 0; i < 12; ++i)
            vals[n++] = next_rand();
        int bad = 0;
        for(i = 0; i < n; ++i) {
            memset(emu, 0, 8192);
            emu->regs[8] = vals[i];
            g_frame_result[0] = 0;
            g_frame_expect[0] = vals[i];
            arm64_prolog(emu, hf_block_basic);
            int ok = (g_frame_result[0] == 0) && (emu->regs[8] == vals[i] + 0x11u);
            if(!ok) {
                ++bad;
                printf("  ....  R8 in 0x%016lx -> out 0x%016lx (expected 0x%016lx), in-block check %s\n",
                       (unsigned long)vals[i], (unsigned long)emu->regs[8],
                       (unsigned long)(vals[i] + 0x11u),
                       g_frame_result[0] ? "FAILED" : "ok");
            }
        }
        check(bad == 0, "guest R8 survives prolog -> block -> epilog for 16 values (0,1,-1,0x0123456789abcdef + 12 pseudo-random)");
    }

    /* ---------------- T11: C helper call inside a live dynarec frame -------- */
    printf("[T11] guest R8 across a real C helper call inside the frame\n");
    {
        uint64_t v = 0x0123456789abcdefULL;
        memset(emu, 0, 8192);
        emu->regs[8] = v;
        g_frame_result[0] = 0;
        g_frame_expect[0] = v;
        g_poison_sink = 0;
        arm64_prolog(emu, hf_block_helper);
        check(g_poison_sink != 0, "the C helper really ran (poison sink written)");
        check(g_frame_result[0] == 0 && emu->regs[8] == v + 0x11u,
              "spill/reload pattern keeps R8 across blr to compiled C code");
    }

    /* ---------------- T12: negative control for T11 ------------------------- */
    printf("[T12] negative control: same block, R8 spill removed\n");
    {
        uint64_t v = 0x0123456789abcdefULL;
        int lost;
        memset(emu, 0, 8192);
        emu->regs[8] = v;
        arm64_prolog(emu, hf_block_helper_nospill);
        lost = (emu->regs[8] != v + 0x11u);
        printf("  ....  R8 after unspilled C call: 0x%016lx (with the spill it would be 0x%016lx)\n",
               (unsigned long)emu->regs[8], (unsigned long)(v + 0x11u));
        check(lost, "R8 is really lost when the spill is missing (control is sensitive)");
    }

    /* ---------------- T13: all 16 guest GPRs + flags + ip round trip -------- */
    printf("[T13] full register file round trip (prolog -> plain block -> epilog)\n");
    {
        uint64_t want[16];
        int i, bad = 0;
        memset(emu, 0, 8192);
        for(i = 0; i < 16; ++i) {
            want[i] = 0x1000000000000000UL + (uint64_t)i * 0x0123456789UL;
            emu->regs[i] = want[i];
        }
        emu->flags = 0x20252ULL;
        emu->ip = 0x00400abcULL;
        arm64_prolog(emu, hf_block_plain);
        for(i = 0; i < 16; ++i)
            if(emu->regs[i] != want[i]) {
                ++bad;
                printf("  ....  regs[%d]: 0x%016lx -> 0x%016lx\n", i,
                       (unsigned long)want[i], (unsigned long)emu->regs[i]);
            }
        check(bad == 0, "all 16 guest GPRs come back unchanged (incl. R8 at regs[8])");
        check(emu->flags == 0x20252ULL, "guest eflags come back unchanged");
        check(emu->ip == 0x00400abcULL, "guest rip comes back unchanged");
    }

    /* ---------------- T14: dispatcher (arm64_next) -------------------------- */
    printf("[T14] arm64_next: R8 across the C call to LinkNext + rip write-back\n");
    {
        uint64_t v = 0x0123456789abcdefULL;
        uint64_t before;
        memset(emu, 0, 8192);
        emu->regs[8] = v;
        g_frame_result[1] = 0;
        g_frame_expect[1] = v;
        before = g_linknext_calls;
        g_linknext_addr = 0;
        g_next_block = (uint64_t)(uintptr_t)(void (*)(void))hf_block_next_end;
        arm64_prolog(emu, hf_block_next);
        check(g_linknext_calls == before + 1, "arm64_next called LinkNext exactly once");
        check(g_linknext_addr == 0x4242ULL, "LinkNext received the guest rip from x27");
        check(g_frame_result[1] == 0, "R8 was intact INSIDE the block reached through arm64_next");
        check(emu->regs[8] == v + 0x22u, "R8 survives the LinkNext C call and the following block");
        check(emu->ip == 0x5242ULL, "LinkNext's changed rip was written back (0x4242 -> 0x5242)");
    }

    /* ---------------- T15: frame canaries + negative control ---------------- */
    printf("[T15] callee-saved canaries around prolog/epilog (x19-x28, d8, d15)\n");
    {
        memset(emu, 0, 8192);
        emu->regs[8] = 0x1234;
        g_canary_fails = 0;
        clear_canary_idx();
        hf_run_canaries(emu, hf_block_plain);
        check(g_canary_fails == 0, "x19-x28, d8 and d15 all come back unchanged (11 canaries)");

        g_canary_fails = 0;
        clear_canary_idx();
        hf_run_canaries(emu, hf_block_damage);   /* corrupts the saved x20 */
        printf("  ....  negative control reported %d canary failure(s), first index %d\n",
               g_canary_fails, g_canary_idx[0]);
        check(g_canary_fails == 1, "negative control: corrupting saved x20 is detected");
        check(g_canary_idx[0] == 2, "negative control: exactly the x21 canary fails (index 2)");
    }

    printf("=== %d checks passed, %d failed ===\n", checks - failed, failed);
    return failed ? 1 : 0;
}
