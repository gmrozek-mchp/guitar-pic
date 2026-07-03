/* Strong overrides for the ARM926 CPU-exception vectors.
 *
 * The MCC-generated handlers (config/default/fault_handlers.c) are `weak` and
 * just `while(true){}` — a data/prefetch abort silently freezes the board
 * (CPU spins in Abort mode with IRQ masked; the XLCDC keeps scanning DDR by DMA,
 * so the panel stays lit while every task, tick, and the console die, and
 * nothing is printed). These strong definitions win at link time and, before
 * spinning, dump the fault class + faulting address straight to DBGU so a hang
 * names its cause instead of being a silent dead board.
 *
 * The vectors (cstartup.S) enter these directly in Abort/Undef mode with a valid
 * mode stack (resetHandler sets _abtstack/_undstack) and the banked LR holding
 * the faulting PC (+8 data abort, +4 prefetch/undef). Output uses the polled
 * DBGU plib only — no FreeRTOS, no heap, no interrupts — so it is safe here.
 * Resolve the printed pc/far with: xc32-addr2line -e <elf> <pc>. */

#include <stdint.h>
#include "definitions.h"   /* DBGU_WriteByte (polled) */

static void dbgu_str(const char *s)
{
    while (*s != '\0') { DBGU_WriteByte((uint8_t)*s++); }
}

static void dbgu_hex32(uint32_t v)
{
    dbgu_str("0x");
    for (int i = 28; i >= 0; i -= 4)
    {
        uint32_t nib = (v >> (unsigned)i) & 0xFu;
        DBGU_WriteByte((uint8_t)(nib < 10u ? ('0' + nib) : ('a' + nib - 10u)));
    }
}

/* C reporters — reached by a bare branch from the naked handlers, so args come
 * in r0.. per the AAPCS and they must never return. `used` keeps them past
 * --gc-sections (only referenced from inline asm). */

void __attribute__((used, noreturn))
fault_report_dabt(uint32_t pc, uint32_t cpsr, uint32_t far, uint32_t dfsr)
{
    dbgu_str("\r\n*** DATA ABORT  pc=");  dbgu_hex32(pc);
    dbgu_str(" far=");                    dbgu_hex32(far);
    dbgu_str(" dfsr=");                   dbgu_hex32(dfsr);
    dbgu_str(" cpsr=");                   dbgu_hex32(cpsr);
    dbgu_str(" ***\r\n");
    for (;;) { /* spin — leave the board put for inspection */ }
}

void __attribute__((used, noreturn))
fault_report_pabt(uint32_t pc, uint32_t cpsr)
{
    dbgu_str("\r\n*** PREFETCH ABORT  pc="); dbgu_hex32(pc);
    dbgu_str(" cpsr=");                       dbgu_hex32(cpsr);
    dbgu_str(" ***\r\n");
    for (;;) { }
}

void __attribute__((used, noreturn))
fault_report_undef(uint32_t pc, uint32_t cpsr)
{
    dbgu_str("\r\n*** UNDEFINED INSTR  pc="); dbgu_hex32(pc);
    dbgu_str(" cpsr=");                        dbgu_hex32(cpsr);
    dbgu_str(" ***\r\n");
    for (;;) { }
}

/* Naked vector entries — capture the banked LR (faulting PC) + SPSR + CP15
 * fault registers before any prologue clobbers them, then branch to the C
 * reporter (which is noreturn, so a plain `b` is correct). */

void __attribute__((naked)) data_abort_irq_handler(void)
{
    __asm volatile(
        "sub  r0, lr, #8              \n"  /* data abort: faulting PC = LR - 8 */
        "mrs  r1, spsr                \n"  /* CPSR at fault (mode/flags)       */
        "mrc  p15, 0, r2, c6, c0, 0   \n"  /* FAR  — faulting data address     */
        "mrc  p15, 0, r3, c5, c0, 0   \n"  /* DFSR — data fault status         */
        "b    fault_report_dabt       \n"
    );
}

void __attribute__((naked)) prefetch_abort_irq_handler(void)
{
    __asm volatile(
        "sub  r0, lr, #4              \n"  /* prefetch abort: faulting PC = LR - 4 */
        "mrs  r1, spsr                \n"
        "b    fault_report_pabt       \n"
    );
}

void __attribute__((naked)) undefined_instruction_irq_handler(void)
{
    __asm volatile(
        "sub  r0, lr, #4              \n"  /* ARM undef: faulting PC = LR - 4 */
        "mrs  r1, spsr                \n"
        "b    fault_report_undef      \n"
    );
}
