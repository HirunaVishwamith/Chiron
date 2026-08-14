//**************************************************************************
// mt-icoh: is freshly-written CODE visible to another hart's instruction fetch?
//--------------------------------------------------------------------------
// The untested gap behind the quad-core Linux freeze at ~1.23e9 cycles.
//
// Measured there: hart0 fetched 16 consecutive words of 0x00000000 at
// 0x81b03840 and jammed (see linux_robjam_probe). That address is in the
// initramfs/userspace region -- memory the kernel WRITES as data during unpack
// and then EXECUTES. Crossing that data->text boundary is exactly what fence.i
// exists for, and on SMP Linux does it with ipi_remote_fence_i: one hart writes
// the code, every hart runs fence.i.
//
// The suspected race: a remote hart invalidates its I-cache and REFILLS from
// L2/DRAM before the writer's D-cache clean-on-fence walker has written the
// dirty lines back. The refill then returns the pre-write contents -- zeros.
//
// mt-fencei already covers fence.i + a DATA release store, and it passes. What
// nothing covered until now is INSTRUCTION visibility, which is a different
// path entirely (I-cache invalidate + refill vs D-cache walker writeback).
//
// Shape, per round:
//   writer hart  : store a tiny function into a code buffer, fence.i, publish
//   reader harts : wait for publish, fence.i, CALL the buffer, check result
//
// The generated function is `li a0, MAGIC; ret`, so a reader that fetched stale
// bytes either returns the WRONG magic (silent staleness) or traps on an
// illegal instruction (zeros) -- the illegal-instruction trap added 2026-08-12
// turns the latter into a clean failure instead of a hang, which is what makes
// this test reportable at all.
//
// A/B knobs:
//   -DWRITER_FENCEI=1 (default) writer executes fence.i before publishing
//   -DWRITER_FENCEI=0 control: writer omits it (readers alone cannot be enough)
//   -DROUNDS=n        how many publish/execute rounds
//   -DSTRIDE_LINES=n  spread successive rounds over n cache lines, so the
//                     refill path is exercised on cold lines rather than one
//                     line that stays resident
//
// exit(0) pass; exit(1) a reader saw a stale/wrong instruction stream.
//**************************************************************************

#include "util.h"

#include <stdint.h>

extern void exit(int status);

#ifndef ROUNDS
#define ROUNDS 64
#endif
#ifndef WRITER_FENCEI
#define WRITER_FENCEI 1
#endif
#ifndef STRIDE_LINES
#define STRIDE_LINES 16
#endif
// SELF_EXEC: the writer also EXECUTES the code it just wrote, on its own hart.
//
// The cross-hart case above passes. The Linux failure is the same-hart one: on
// nommu there is no VDSO, so the kernel writes an rt_sigreturn trampoline
// (`li a7,139; ecall`) onto the user stack as DATA and then returns to it as
// TEXT -- one hart, store then fetch, with only a local fence.i in between.
// Observed at 0x81b03840: the kernel's own oops dump (a DATA read) showed the
// trampoline present, while DRAM held zeros and the I-fetch got zeros. Three
// views of one address disagreeing is a writeback/refill ordering failure, not
// corruption. Nothing in the suite covered store->fence.i->fetch on ONE hart.
#ifndef SELF_EXEC
#define SELF_EXEC 0
#endif

#define LINE_BYTES 64
#define NHARTS     4

// Executable scratch. Aligned and padded so each round lands on its own cache
// line, and so the whole buffer spans many lines rather than one hot one.
__attribute__((aligned(64)))
volatile unsigned int code_buf[STRIDE_LINES * (LINE_BYTES / 4)];

volatile unsigned long publish_seq = 0;   // writer -> readers: round number
volatile unsigned long reader_seq[NHARTS];
volatile unsigned long reader_bad[NHARTS];
volatile unsigned long reader_got[NHARTS];

// `li a0, imm` (addi a0,x0,imm) followed by `ret` (jalr x0,0(x1)).
static inline unsigned int enc_li_a0(unsigned int imm12)
{
  return 0x00000513u | ((imm12 & 0xfffu) << 20);
}
#define ENC_RET 0x00008067u

void thread_entry(int cid, int nc)
{
  if (cid >= nc) {
    while (1)
      ;
  }

  if (cid == 0) {
    for (int i = 0; i < NHARTS; i++) {
      reader_seq[i] = 0;
      reader_bad[i] = 0;
      reader_got[i] = 0;
    }
    for (unsigned i = 0; i < sizeof(code_buf) / sizeof(code_buf[0]); i++)
      code_buf[i] = 0;
    publish_seq = 0;
  }
  barrier(nc);

  for (unsigned long r = 1; r <= (unsigned long)ROUNDS; r++) {
    // Successive rounds use a different cache line, so each publish exercises a
    // line the readers do not already have resident.
    const unsigned slot = (unsigned)((r % STRIDE_LINES) * (LINE_BYTES / 4));
    const unsigned magic = (unsigned)(r & 0x7ffu) | 0x100u;

    if (cid == 0) {
      // --- writer -------------------------------------------------------
      code_buf[slot + 0] = enc_li_a0(magic);
      code_buf[slot + 1] = ENC_RET;
#if WRITER_FENCEI
      // Push the stores out of the D-cache and drop stale I-cache lines here.
      __asm__ volatile("fence.i" ::: "memory");
#endif
#if SELF_EXEC
      // Same-hart data->instruction transition, before anyone else is told the
      // code exists. A stale I-cache line here can only have come from THIS
      // hart's own earlier use of the slot (STRIDE_LINES makes rounds revisit
      // each line with different contents), which is exactly the signal-frame
      // pattern: the same stack address holding a fresh trampoline each time.
      {
        unsigned long (*sfn)(void) =
            (unsigned long (*)(void))(void *)&code_buf[slot];
        const unsigned long sgot = sfn();
        reader_got[0] = sgot;
        if (sgot != (unsigned long)magic) {
          reader_bad[0]++;
        }
      }
#endif
      __sync_synchronize();
      publish_seq = r;               // release: readers may now execute it
    } else {
      // --- readers ------------------------------------------------------
      while (publish_seq != r)
        ;
      __sync_synchronize();
      // Every hart must fence.i for its own I-cache, exactly as
      // ipi_remote_fence_i makes every hart do.
      __asm__ volatile("fence.i" ::: "memory");

      unsigned long (*fn)(void) =
          (unsigned long (*)(void))(void *)&code_buf[slot];
      const unsigned long got = fn();

      reader_got[cid] = got;
      if (got != (unsigned long)magic) {
        reader_bad[cid]++;
      }
      reader_seq[cid] = r;
    }

    // Writer waits for every reader to finish this round before overwriting a
    // slot, so a mismatch can only mean stale fetch, never a benign data race.
    if (cid == 0) {
      for (int t = 1; t < nc; t++)
        while (reader_seq[t] != r)
          ;
    }
    barrier(nc);
  }

  barrier(nc);

  if (cid == 0) {
    unsigned long bad = 0;
    // Under SELF_EXEC hart0 is a participant, not just the writer.
    for (int t = (SELF_EXEC ? 0 : 1); t < nc; t++)
      bad += reader_bad[t];
    if (bad)
      exit(1);
    exit(0);
  }
  while (1)
    ;
}
