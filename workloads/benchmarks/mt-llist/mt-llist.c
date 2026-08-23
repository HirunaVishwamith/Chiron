//**************************************************************************
// mt-llist: does cmpxchg racing amoswap on ONE word ever lose an entry?
//--------------------------------------------------------------------------
// This is the primitive Linux's cross-call queue is built from, and the one
// combination none of the existing SMP regressions covers.
//
// Forensics on the quad-core /init hang (linux_csd_probe on
// ckpt_001100000000.bin) found:
//
//   two csds locked (u_flags = 0x11 = CSD_FLAG_LOCK|CSD_TYPE_SYNC)
//   every call_single_queue head = 0  -- all four queues EMPTY
//   the spinning hart reads 0x11, and DRAM holds 0x11 (so no stale line)
//   nothing wedged: all four harts keep committing
//
// An entry that is locked but on nobody's queue was either never published or
// was erased. Both queue operations touch the SAME word with DIFFERENT atomics:
//
//   llist_add()     -> cmpxchg(&head->first, old, new)   -> lr.d / sc.d
//   llist_del_all() -> xchg(&head->first, NULL)          -> amoswap.d
//
// NOTE: the original version of this comment claimed no existing test mixes
// amoswap with lr/sc on one address. That is wrong -- mt-lrsc phase 2 runs this
// exact pattern ("llist push (cmpxchg) vs del_all (amoswap) -- csd pattern").
// mt-lrsc is therefore the control for this test, and the fact that it passes
// while this one hung is what localised the bug to this file. What mt-llist
// adds over mt-lrsc is the conservation ledger: per-node seen[] accounting that
// names a lost or duplicated entry, where mt-lrsc only checks a total.
//
// mt-lrsc covers contended lr/sc, mt-ipi* cover IPI delivery, mt-csdwait covers
// the spin-and-release, mt-fencei covers fence.i before a release store -- and
// all of them pass. If amoswap
// can land between a peer's lr.d and sc.d without killing the reservation (or
// if sc.d reports success while its data is dropped), a push is silently lost:
// the pusher believes it queued the entry, the drainer never sees it, and a
// synchronous cross-call waits forever. That is exactly the observed state.
//
// The test is self-checking on a conservation law: every node pushed must be
// popped exactly once. Producers push a fixed number of distinct nodes; the
// consumer drains with xchg and counts. Losing even one node fails.
//
//   -DPUSH_CAS=1 (default) producers publish with cmpxchg  (the Linux shape)
//   -DPUSH_CAS=0           producers publish with amoswap  (control: same-atomic
//                          racing, which mt-lrsc-style tests already cover)
//
//--------------------------------------------------------------------------
// LIVENESS (2026-08-23). This test hung on CORRECT RTL and was very nearly
// diagnosed as a processor bug. It is a testbench bug. Read this before
// "fixing" a hang in the RTL on the strength of this test.
//
// ROOT CAUSE: the push loop used __sync_bool_compare_and_swap plus a redundant
// __sync_synchronize(). GCC pairs lr.d.aq with sc.d.AQ and emits its own
// leading release fence, so two full pipeline-draining fences sat between the
// load of head.first and the lr.d that has to observe the same value. Under
// three-way contention that window was essentially always invalidated: the
// compare failed before sc.d was ever reached, and the producers retried
// forever. Measured: 4 pushes in 4M cycles, none after cycle 1M, out of 1536
// required. A PC histogram put the producers at 51%/46% on the two retry
// branches and only 2.4% on sc.d. Fixed by using the same hand-written
// lr.d.aq/sc.d.rl cmpxchg64 that mt-lrsc uses -- see the note on it below.
//
// Two further defects, each of which also presented as an anonymous timeout:
//
// 1. The consumer drained in an unthrottled loop, issuing amoswap.d on
//    head.first every ~7 instructions. That is not what it claimed to model --
//    Linux calls llist_del_all() once per IPI and never spins on it -- so the
//    consumer now peeks with a plain load. NOTE this was NOT the hang: mt-lrsc
//    phase 2 spins its drain completely unthrottled and passes. It is fixed
//    because an idle consumer should not manufacture contention, not because
//    it was the bug. Beware: an earlier analysis blamed this, reasoning that
//    RISC-V only guarantees LR/SC forward progress when no peer writes the
//    reservation set. True, but not what was happening here.
//
// 2. WAIT_CAP exceeded the harness cycle budget, so the fail_hart escape was
//    dead code and a lost entry could only ever surface as a hang rather than
//    the "lost=N" this test exists to report.
//
// METHOD NOTE, because it cost hours: build/profile_quad_fast.out links
// Vsystem__ALL.a statically. Running it directly after rebuilding the RTL
// silently executes the PREVIOUS model, with entirely plausible output. Two
// "passes" of this test were exactly that. Drive it through make.
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#ifndef PUSH_CAS
#define PUSH_CAS 1
#endif

#define NODES_PER_PRODUCER 512

// No-progress watchdog: consecutive idle peeks with the queue empty, reset on
// every drain. It exists so a lost entry reports itself as "lost=N" and exits
// instead of dying as an anonymous harness timeout.
//
// Sized from the run, not from guesswork: a healthy run is ~1.9M cycles end to
// end and the idle poll below cannot retire in under ~4 cycles on this 1-wide
// machine, so consecutive idle cannot exceed ~475K. 4M leaves an order of
// magnitude of headroom while still bounding a genuine wedge.
//
// Resist the temptation to make this smarter by watching a producer-side
// counter. Two attempts did exactly that -- one polling pushed[], one polling
// producers_done -- and BOTH hung the test outright, because the consumer then
// holds a line producers need Exclusive, and .bss placement is not declaration
// order so even a rarely-written counter can share a line with a hot one. The
// idle path must read head.first and nothing else.
#define WAIT_CAP           4000000UL

// A node published by the amoswap control is visible at the head *before* its
// next pointer is written, so the consumer must not read a next field that its
// producer has not filled in yet. Producers stamp this sentinel before
// publishing and overwrite it immediately after; the consumer waits for it to
// clear. Unused when PUSH_CAS=1, where the CAS lets next be written first.
#define NEXT_BUSY          ((struct node *)~0UL)

// cmpxchg exactly as mt-lrsc writes it, and as the kernel does: lr.d.aq paired
// with sc.d.rl, so the release ordering rides on the store-conditional itself.
//
// Do NOT go back to __sync_bool_compare_and_swap here. GCC emits lr.d.aq with
// sc.d.AQ plus a separate leading release fence, and this test additionally had
// a __sync_synchronize() before it. That put two full pipeline-draining fences
// between reading head.first and the lr.d that has to observe the same value:
//
//    ld a5,0(s4)      first = head.first
//    sd a5,0(s1)      n->next = first
//    fence            <- __sync_synchronize(), redundant
//    fence iorw,ow    <- the builtin's own release fence
//    lr.d.aq a3,(s4)
//    bne a3,a5,...    <- head.first has moved; retry forever
//
// Under three-way contention that window is essentially always invalidated, so
// the compare failed before sc.d was ever reached and the test livelocked with
// no output. A PC histogram showed the producers at 51%/46% on the two retry
// branches and only 2.4% on sc.d. mt-lrsc runs the identical algorithm with
// these asm primitives and passes, which is what localised it here.
static inline unsigned long cmpxchg64(volatile unsigned long *p,
                                      unsigned long old, unsigned long nw)
{
  unsigned long v, fail;
  __asm__ volatile(
      "1: lr.d.aq %0, (%2)\n\t"
      "   bne %0, %3, 2f\n\t"
      "   sc.d.rl %1, %4, (%2)\n\t"
      "   bnez %1, 1b\n\t"
      "2:"
      : "=&r"(v), "=&r"(fail)
      : "r"(p), "r"(old), "r"(nw)
      : "memory");
  return v;
}

static inline unsigned long amoswap64(volatile unsigned long *p, unsigned long nw)
{
  unsigned long old;
  __asm__ volatile("amoswap.d.aqrl %0, %2, (%1)"
                   : "=r"(old) : "r"(p), "r"(nw) : "memory");
  return old;
}

// A node is a whole cache line so this measures the atomics on the shared head,
// not false sharing between the nodes themselves.
//
// `next` is a VOLATILE POINTER (struct node *volatile), not a pointer to
// volatile. Writing `volatile struct node *next` qualifies the pointee and
// leaves the member an ordinary object, so a spin on n->next compiles to one
// load and an infinite `j .` -- which is exactly what it did here before.
typedef struct node {
  struct node *volatile next;
  volatile unsigned long owner;
  volatile unsigned long seq;
  unsigned char pad[40];
} node_t;

static node_t pool[4][NODES_PER_PRODUCER] __attribute__((aligned(64)));

// The contended word: call_single_queue's head->first.
static struct {
  volatile unsigned long first;
  unsigned char pad[56];
} head __attribute__((aligned(64)));

static volatile unsigned long pushed[4];
static volatile unsigned long popped_total;
static volatile unsigned long seen[4][NODES_PER_PRODUCER];
static volatile int producers_done;
static volatile int fail_hart[4];

// llist_add(): publish new at the head, chaining the old head behind it.
// Returns 1 if the list had been empty (Linux uses that to decide whether to
// send the IPI, so a wrong answer here is itself a lost wakeup).
static int llist_add(node_t *n)
{
#if PUSH_CAS
  unsigned long first = head.first;
  for (;;) {
    n->next = (struct node *)first;
    // No fence here: sc.d.rl inside cmpxchg64 orders the store above against
    // the publish. Retry with the value the CAS actually saw rather than
    // re-loading head.first -- one less load in the window that has to stay
    // valid, and it is what makes this loop converge under contention.
    unsigned long prev = cmpxchg64(&head.first, first, (unsigned long)n);
    if (prev == first)
      return first == 0UL;
    first = prev;
  }
#else
  // A single xchg cannot publish a Treiber node safely: the old head is not
  // known until after the swap has already made n reachable, so there is no
  // point at which next can be filled in first. Publishing it raw truncates
  // the chain whenever the consumer drains inside that window, losing every
  // node behind it -- which this test would then report as an RTL lost-update.
  // Stamp the sentinel before publishing and clear it right after; the
  // consumer waits it out.
  n->next = NEXT_BUSY;
  unsigned long first = amoswap64(&head.first, (unsigned long)n);
  n->next = (struct node *)first;
  return first == 0UL;
#endif
}

// llist_del_all(): take the whole chain in one atomic swap.
static node_t *llist_del_all(void)
{
  return (node_t *)amoswap64(&head.first, 0UL);
}

void thread_entry(int cid, int nc)
{
  if (cid >= nc) {
    while (1)
      ;
  }

  initialize_count_asm(0);
  if (cid == 0) {
    head.first    = 0UL;
    popped_total  = 0UL;
    producers_done = 0;
  }
  pushed[cid]   = 0UL;
  fail_hart[cid] = 0;
  for (int i = 0; i < NODES_PER_PRODUCER; i++) {
    pool[cid][i].next  = 0;
    pool[cid][i].owner = (unsigned long)cid;
    pool[cid][i].seq   = (unsigned long)i;
    seen[cid][i]       = 0UL;
  }
  barrier(nc);

  const unsigned long expected =
      (unsigned long)(nc - 1) * (unsigned long)NODES_PER_PRODUCER;

  if (cid == 0) {
    // Consumer: drain until every produced node has been accounted for.
    unsigned long idle = 0;
    while (popped_total < expected) {
      // Peek with a plain load. Swapping unconditionally would put an AMO on
      // head.first every few instructions and starve the producers' lr/sc --
      // see the LIVENESS note at the top. An idle consumer must be silent on
      // the contended line.
      // The idle path touches NOTHING a producer writes except head.first
      // itself. Two earlier watchdogs polled a second shared word here --
      // pushed[] (bumped on all 1536 pushes) and then producers_done -- and
      // both starved the producers outright: .bss placement is not
      // declaration order, so a "cheap" counter can share a 64-byte line with
      // a hot one, and either way the consumer holds that line Shared while
      // producers need it Exclusive. Both variants hung. Keep this loop
      // reading one word and counting in a register.
      if (head.first == 0UL) {
        if (++idle > WAIT_CAP) { fail_hart[0] = 1; break; }
        continue;
      }

      node_t *n = llist_del_all();
      idle = 0;                       // a drain is progress
      while (n) {
        // Wait out a producer that has published but not yet linked (the
        // amoswap control only; the CAS path never stores the sentinel).
        struct node *nx;
        while ((nx = n->next) == NEXT_BUSY)
          ;                           // n->next is volatile: reloads each pass
        node_t *next = (node_t *)nx;
        if (n->owner < 4UL && n->seq < (unsigned long)NODES_PER_PRODUCER)
          seen[n->owner][n->seq]++;
        popped_total++;
        n = next;
      }
    }
  } else {
    for (int i = 0; i < NODES_PER_PRODUCER; i++) {
      llist_add(&pool[cid][i]);
      pushed[cid]++;
    }
    __sync_fetch_and_add(&producers_done, 1);
  }

  // Do NOT join the barrier when the watchdog fired. It fires precisely when
  // producers are not making progress, which means they are still spinning in
  // llist_add and will never arrive here -- waiting for them would block hart 0
  // forever and turn the diagnosis we just computed back into the anonymous
  // hang this watchdog exists to prevent. Producers that did finish stay parked
  // at the barrier; hart 0 reports and exits, which is what the harness reads.
  if (!(cid == 0 && fail_hart[0]))
    barrier(nc);

  if (cid != 0)
    exit(2);

  // Conservation: every node pushed was popped exactly once.
  unsigned long lost = 0UL, dup = 0UL;
  for (int p = 1; p < nc; p++)
    for (int i = 0; i < NODES_PER_PRODUCER; i++) {
      if (seen[p][i] == 0UL) lost++;
      else if (seen[p][i] > 1UL) dup++;
    }

  uart_send_string("mt-llist: push=");
  uart_send_integer((int)((unsigned long)(nc - 1) * NODES_PER_PRODUCER));
  uart_send_string(" pop=");
  uart_send_integer((int)popped_total);
  uart_send_string(" lost=");
  uart_send_integer((int)lost);
  uart_send_string(" dup=");
  uart_send_integer((int)dup);
  uart_send_string(" cas=");
  uart_send_integer(PUSH_CAS);
  uart_send_string(" stalled=");
  uart_send_integer(fail_hart[0]);
  uart_send_string("\n");

  if (lost == 0UL && dup == 0UL && popped_total == expected && !fail_hart[0]) {
    uart_send_string("mt-llist: PASS\n");
    exit(0);
  }
  // Report rather than hang: the watchdog above guarantees we reach this even
  // when entries went missing, so the failure names itself instead of showing
  // up as a harness timeout.
  if (fail_hart[0])
    uart_send_string("mt-llist: FAIL (consumer starved: queue empty, "
                     "producers never published)\n");
  else
    uart_send_string("mt-llist: FAIL (queue lost or duplicated an entry)\n");
  exit(1);
}
