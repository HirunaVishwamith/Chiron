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
// mt-lrsc covers contended lr/sc, mt-ipi* cover IPI delivery, mt-csdwait covers
// the spin-and-release, mt-fencei covers fence.i before a release store -- and
// all of them pass. None mixes amoswap with lr/sc on one address. If amoswap
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
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#ifndef PUSH_CAS
#define PUSH_CAS 1
#endif

#define NODES_PER_PRODUCER 512
#define WAIT_CAP           40000000UL

// A node is a whole cache line so this measures the atomics on the shared head,
// not false sharing between the nodes themselves.
typedef struct node {
  volatile struct node *next;
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
  for (;;) {
    unsigned long first = head.first;
    n->next = (volatile struct node *)first;
    __sync_synchronize();
    if (__sync_bool_compare_and_swap(&head.first, first, (unsigned long)n))
      return first == 0UL;
  }
#else
  unsigned long first = __sync_lock_test_and_set(&head.first, (unsigned long)n);
  n->next = (volatile struct node *)first;
  return first == 0UL;
#endif
}

// llist_del_all(): take the whole chain in one atomic swap.
static node_t *llist_del_all(void)
{
  return (node_t *)__sync_lock_test_and_set(&head.first, 0UL);
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
    unsigned long spin = 0;
    while (popped_total < expected) {
      node_t *n = llist_del_all();
      while (n) {
        node_t *next = (node_t *)n->next;
        if (n->owner < 4UL && n->seq < (unsigned long)NODES_PER_PRODUCER)
          seen[n->owner][n->seq]++;
        popped_total++;
        n = next;
      }
      if (++spin > WAIT_CAP) {
        fail_hart[0] = 1;
        break;
      }
    }
  } else {
    for (int i = 0; i < NODES_PER_PRODUCER; i++) {
      llist_add(&pool[cid][i]);
      pushed[cid]++;
    }
    __sync_fetch_and_add(&producers_done, 1);
  }

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
  uart_send_string("\n");

  if (lost == 0UL && dup == 0UL && popped_total == expected && !fail_hart[0]) {
    uart_send_string("mt-llist: PASS\n");
    exit(0);
  }
  uart_send_string("mt-llist: FAIL (queue lost or duplicated an entry)\n");
  exit(1);
}
