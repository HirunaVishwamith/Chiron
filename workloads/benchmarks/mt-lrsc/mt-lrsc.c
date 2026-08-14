//**************************************************************************
// mt-lrsc: cross-hart LR/SC (cmpxchg) contention — lost-update detector
//--------------------------------------------------------------------------
// Linux SMP on the RTL reaches userspace then (a) console writes start
// vanishing, (b) a MUTEX_WARN_ON(owner & MUTEX_FLAG_PICKUP) fires at ttyUL0
// registration, and (c) CPU0 eventually spins forever in csd_lock_wait on a
// call_single_data whose llist enqueue/completion was lost. All three are
// cmpxchg-family failures: mutex fastpath, llist_add, csd_unlock are LR/SC
// loops on RISC-V. The AMO paths are proven good (mt-ipimux, amoadd/amoor
// benches) — but NOTHING stresses cross-hart LR/SC: the ISA lrsc test is
// single-core and every benchmark's atomic is an amoadd. chiron's docs also
// flag an SC hole (isSCWriteWire writes BRAM unconditionally on reservation
// match even if the line was evicted mid-sequence).
//
// Three phases, all self-checking:
//  1. cmpxchg counter: every hart performs N SUCCESSFUL lr.d/sc.d increments
//     of one shared u64 (retry loop, like atomic_long_cmpxchg). Final value
//     must be exactly 4N — any lost update = an SC that "succeeded" without
//     its store becoming globally visible.
//  2. llist push/pop (the exact csd pattern): harts 1-3 cmpxchg-push nodes
//     onto a shared head; hart0 amoswap-pops the whole list (llist_del_all)
//     and counts. Every pushed node must be seen exactly once.
//  3. mutex-style owner handoff: harts cmpxchg(0 -> self) to acquire,
//     store-release 0 to release, count acquisitions; total must match and
//     the owner field must never be observed with a foreign value while
//     held (checked inside the critical section).
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#define N_INC 3000
#define N_NODES 800        // per pusher hart, phase 2
#define N_LOCK 1500
#define SPIN_TIMEOUT 400000000UL

static volatile unsigned long counter __attribute__((aligned(64)));
static volatile unsigned long llhead __attribute__((aligned(64)));
static volatile unsigned long owner __attribute__((aligned(64)));
static volatile unsigned long lock_total __attribute__((aligned(64)));
static volatile unsigned long crit __attribute__((aligned(64)));
volatile unsigned long popped_count;
volatile unsigned long owner_corrupt;
volatile unsigned long sc_fail_zero;   // diagnostic: sc never succeeding

// Phase-2 nodes: next-pointer per node, embedded in a static pool.
// 3 pusher harts * N_NODES nodes. Node id encodes (hart, idx).
static unsigned long nodes[3 * N_NODES] __attribute__((aligned(64)));
static volatile unsigned char seen[3 * N_NODES];

static inline unsigned long lr64(volatile unsigned long *p)
{
  unsigned long v;
  __asm__ volatile("lr.d %0, (%1)" : "=r"(v) : "r"(p) : "memory");
  return v;
}
static inline int sc64(volatile unsigned long *p, unsigned long v)
{
  unsigned long fail;
  __asm__ volatile("sc.d %0, %2, (%1)" : "=r"(fail) : "r"(p), "r"(v) : "memory");
  return fail == 0;
}
// atomic_long_cmpxchg acquire-release flavor, like the kernel's
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

static void fail(const char *what, unsigned long a, unsigned long b)
{
  uart_send_string("mt-lrsc: FAIL ");
  uart_send_string(what);
  uart_send_string(" a=");
  uart_send_integer((int)a);
  uart_send_string(" b=");
  uart_send_integer((int)b);
  uart_send_string("\n");
  exit(1);
}

void thread_entry(int cid, int nc)
{
  if (cid >= nc) {
    while (1)
      ;
  }

  initialize_count_asm(0);
  barrier(nc);

  // ── Phase 1: N successful LR/SC increments per hart ──────────────────────
  for (unsigned long i = 0; i < N_INC; i++) {
    unsigned long spins = 0;
    for (;;) {
      unsigned long v = lr64(&counter);
      if (sc64(&counter, v + 1))
        break;
      if (++spins > SPIN_TIMEOUT)
        fail("p1-sc-livelock", (unsigned long)cid, i);
    }
  }
  barrier(nc);
  if (cid == 0 && counter != (unsigned long)nc * N_INC)
    fail("p1-lost-updates", counter, (unsigned long)nc * N_INC);
  barrier(nc);

  // ── Phase 2: llist push (cmpxchg) vs del_all (amoswap) — csd pattern ────
  if (cid == 0) {
    // popper: drain until all pushers signal done AND list empty
    unsigned long done_target = (unsigned long)(nc - 1) * N_NODES;
    unsigned long spins = 0;
    while (popped_count < done_target) {
      unsigned long head = amoswap64(&llhead, 0);   // llist_del_all
      while (head) {
        unsigned long id = head - (unsigned long)&nodes[0];
        id /= sizeof(unsigned long);
        if (id >= 3 * N_NODES)
          fail("p2-bad-node", head, id);
        if (seen[id])
          fail("p2-double-pop", id, 0);
        seen[id] = 1;
        popped_count++;
        head = nodes[id];                            // ->next
      }
      if (++spins > SPIN_TIMEOUT)
        fail("p2-pop-timeout", popped_count, done_target);
    }
  } else {
    for (unsigned long i = 0; i < N_NODES; i++) {
      unsigned long idx = (unsigned long)(cid - 1) * N_NODES + i;
      volatile unsigned long *node = &nodes[idx];
      unsigned long spins = 0;
      for (;;) {                                     // llist_add
        unsigned long first = llhead;
        *node = first;                               // node->next = first
        if (cmpxchg64(&llhead, first, (unsigned long)node) == first)
          break;
        if (++spins > SPIN_TIMEOUT)
          fail("p2-push-livelock", (unsigned long)cid, i);
      }
    }
  }
  barrier(nc);
  if (cid == 0) {
    for (unsigned long id = 0; id < 3 * N_NODES; id++)
      if (!seen[id])
        fail("p2-lost-node", id, popped_count);
  }
  barrier(nc);

  // ── Phase 3: mutex-owner style cmpxchg handoff ───────────────────────────
  for (unsigned long i = 0; i < N_LOCK; i++) {
    unsigned long spins = 0;
    unsigned long self = (unsigned long)(cid + 1) << 8;  // nonzero id
    while (cmpxchg64(&owner, 0, self) != 0)
      if (++spins > SPIN_TIMEOUT)
        fail("p3-acquire-timeout", (unsigned long)cid, i);
    // critical section: owner must read back as exactly self
    if (owner != self)
      owner_corrupt++;
    crit++;                                            // plain shared inc
    lock_total++;
    __asm__ volatile("fence rw, w" ::: "memory");
    owner = 0;                                         // release
  }
  barrier(nc);

  if (cid != 0)
    exit(2);

  uart_send_string("mt-lrsc: counter=");
  uart_send_integer((int)counter);
  uart_send_string(" popped=");
  uart_send_integer((int)popped_count);
  uart_send_string(" lock_total=");
  uart_send_integer((int)lock_total);
  uart_send_string(" corrupt=");
  uart_send_integer((int)owner_corrupt);
  uart_send_string("\n");

  if (counter == (unsigned long)nc * N_INC &&
      popped_count == (unsigned long)(nc - 1) * N_NODES &&
      lock_total == (unsigned long)nc * N_LOCK &&
      crit == (unsigned long)nc * N_LOCK && owner_corrupt == 0) {
    uart_send_string("mt-lrsc: PASS\n");
    exit(0);
  }
  uart_send_string("mt-lrsc: FAIL totals\n");
  exit(1);
}
