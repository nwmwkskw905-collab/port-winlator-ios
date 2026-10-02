/* FASE 04 / register-strategy evidence probe - host side.
 * Prints the raw memory produced by pair_test.S so the pairing/order questions
 * are answered by observed bytes, not by reading the ARM ARM.
 */
#include <stdio.h>
#include <string.h>

extern void pair_test(unsigned long *buf);

int main(void)
{
    unsigned long buf[16];
    memset(buf, 0, sizeof buf);

    pair_test(buf);

    printf("stp x9,x19,[x0,#32]  -> [+32]=%#lx  [+40]=%#lx   (expect 0x1111 / 0x2222)\n",
           buf[4], buf[5]);
    printf("ldp x9,x19,[x0,#32]  -> stored back [+48]=%#lx [+56]=%#lx (expect 0x1111 / 0x2222)\n",
           buf[6], buf[7]);
    printf("stp x9,x27,[x0,#64]  -> [+64]=%#lx  [+72]=%#lx   (expect 0x1111 / 0x3333)\n",
           buf[8], buf[9]);
    printf("x27 after the stp    -> [+80]=%#lx               (expect 0x3333)\n",
           buf[10]);

    int ok = (buf[4] == 0x1111) && (buf[5] == 0x2222)
          && (buf[6] == 0x1111) && (buf[7] == 0x2222)
          && (buf[8] == 0x1111) && (buf[9] == 0x3333)
          && (buf[10] == 0x3333);
    printf("VERDICT: non-consecutive STP/LDP %s\n", ok ? "ACCEPTED" : "REJECTED");
    return ok ? 0 : 1;
}
