#ifndef MARVIN_GAMEPLAY_COV_H
#define MARVIN_GAMEPLAY_COV_H

/* Integer ink-coverage primitive shared by the digit readers (gameplay_score.c,
 * gameplay_amp2p.c) — the C side of the host's gameplay/covcore.py.
 *
 * Split [0, extent) into `n` spans; edge(i) = round(i*extent/n), half-up, in pure
 * integer (matches covcore.edge). The whole coverage pipeline is integer so it
 * reproduces the host prototype bit-for-bit — float would differ by rounding mode
 * and float32/64, and this core also runs on the FPU-less ARM926. One definition,
 * because two readers rounding grid edges differently would diverge silently. */

static inline int gp_edge(int i, int extent, int n)
{
    return (2 * i * extent + n) / (2 * n);
}

#endif
