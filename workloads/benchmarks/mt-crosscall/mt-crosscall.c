//**************************************************************************
// mt-crosscall: the FULL Linux cross-call protocol, end to end.
//--------------------------------------------------------------------------
// Forensics on the /init hang (linux_csd_probe on ckpt_001100000000.bin):
//
//   func = 802054e8 = ipi_remote_fence_i   (i.e. flush_icache_all())
//   per-cpu csd slots: 0 free, 1 LOCKED 0x11, 2 LOCKED 0x11, 3 free
//   every call_single_queue head = 0
//
// on_each_cpu() locks a csd for every OTHER online cpu, so with 4 harts the
// sender locks 3. One of the two free slots is the sender's own; the other is
// a target that COMPLETED. So of three cross-calls, one worked and TWO were
// lost. A 2-in-3 failure rate is not a rare race — it is near-systematic once
// it triggers, and it triggers on the first flush_icache_all() after userspace
// starts. That says the earlier repros were missing a structural ingredient,
// not merely unlucky.
//
// What each earlier test left out (all of them PASS):
//   mt-csdwait   spin + release store, no fence.i, no IPI, no queue
//   mt-fencei    fence.i before the release store, but no IPI and no queue
//   mt-llist     cmpxchg vs amoswap, but ONE shared head and no interrupts
//   mt-ipi*      IPI delivery, but no queue and no callback
//
// Linux does all of it at once, and — the ingredient none of the above has —
// **drains the queue inside the interrupt handler**, so the amoswap races
// whatever the target was executing when the IPI landed. This test reproduces
// the whole protocol:
//
//   sender  : for t in targets:  csd_lock(csd[t])            (flag = 0x11)
//                                llist_add(&csd[t], &queue[t])  (cmpxchg)
//                                send_ipi(t)
//             then spin on every csd[t].flag with the div-based cpu_relax
//   target  : IN THE ISR:        chain = xchg(&queue[self], 0)   (amoswap)
//                                for each node: fence.i ; csd_unlock(node)
//
// Targets run a dirty-the-D-cache loop between IPIs so the interrupt lands
// mid-work with dirty lines outstanding, as it does under Linux.
//
//   -DCC_FENCEI=1 (default) callback executes fence.i, as ipi_remote_fence_i does
//   -DCC_FENCEI=0           callback does nothing — isolates the fence.i
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

#ifndef CC_FENCEI
#define CC_FENCEI 1
#endif

#define ROUNDS       64
#define DIRTY_LINES  32
#define SPIN_TIMEOUT 150000UL

// The csd IS the llist node, exactly as in Linux:
//   struct __call_single_node { struct llist_node llist; unsigned int u_flags; }
typedef struct csd {
  volatile struct csd *next;
  volatile unsigned int flag;
  unsigned char pad[52];
} csd_t;

typedef struct {
  volatile unsigned long first;
  unsigned char pad[56];
} llist_head_t;

static csd_t       csd[4]   __attribute__((aligned(64)));
static llist_head_t queue[4] __attribute__((aligned(64)));
static volatile unsigned long dirty[4][DIRTY_LINES][8] __attribute__((aligned(64)));

static volatile unsigned long served[4];
static volatile unsigned long rounds_done[4];
static volatile int fail_hart[4];
static volatile int stop;

static inline int hartid(void)
{
  unsigned long h;
  __asm__ volatile("csrr %0, mhartid" : "=r"(h));
  return (int)h;
}

// Runs in interrupt context: llist_del_all(), then the callback + csd_unlock
// for every entry taken. This is __flush_smp_call_function_queue.
void crosscall_isr(void)
{
  const int self = hartid();
  csd_t *n = (csd_t *)__sync_lock_test_and_set(&queue[self].first, 0UL);
  while (n) {
    csd_t *next = (csd_t *)n->next;
#if CC_FENCEI
    __asm__ volatile("fence.i" ::: "memory");   // ipi_remote_fence_i()
#endif
    __sync_synchronize();                       // smp_store_release()
    n->flag = 0u;                               // csd_unlock()
    served[self]++;
    n = next;
  }
}

// M-mode software-interrupt handler. Clears its own msip (level-triggered off
// msipShared: mret with it still high would re-trap), then calls the C drain.
__asm__(
    ".pushsection .text\n"
    ".align 2\n"
    ".global cc_handler\n"
    "cc_handler:\n"
    "  addi sp, sp, -160\n"
    "  sd ra,   0(sp)\n"
    "  sd t0,   8(sp)\n"
    "  sd t1,  16(sp)\n"
    "  sd t2,  24(sp)\n"
    "  sd t3,  32(sp)\n"
    "  sd t4,  40(sp)\n"
    "  sd t5,  48(sp)\n"
    "  sd t6,  56(sp)\n"
    "  sd a0,  64(sp)\n"
    "  sd a1,  72(sp)\n"
    "  sd a2,  80(sp)\n"
    "  sd a3,  88(sp)\n"
    "  sd a4,  96(sp)\n"
    "  sd a5, 104(sp)\n"
    "  sd a6, 112(sp)\n"
    "  sd a7, 120(sp)\n"
    "  csrr t0, mhartid\n"
    "  slli t1, t0, 2\n"
    "  li   t2, 0x02000000\n"
    "  add  t2, t2, t1\n"
    "  sw   zero, 0(t2)\n"
    "  lw   t1, 0(t2)\n"        // serialize: the clear reached the CLINT
    "  call crosscall_isr\n"
    "  ld ra,   0(sp)\n"
    "  ld t0,   8(sp)\n"
    "  ld t1,  16(sp)\n"
    "  ld t2,  24(sp)\n"
    "  ld t3,  32(sp)\n"
    "  ld t4,  40(sp)\n"
    "  ld t5,  48(sp)\n"
    "  ld t6,  56(sp)\n"
    "  ld a0,  64(sp)\n"
    "  ld a1,  72(sp)\n"
    "  ld a2,  80(sp)\n"
    "  ld a3,  88(sp)\n"
    "  ld a4,  96(sp)\n"
    "  ld a5, 104(sp)\n"
    "  ld a6, 112(sp)\n"
    "  ld a7, 120(sp)\n"
    "  addi sp, sp, 160\n"
    "  mret\n"
    ".popsection\n");

static inline void ipi_arm(void)
{
  __asm__ volatile(
      "la t0, cc_handler\n\t"
      "csrw mtvec, t0\n\t"
      "csrsi mie, 8\n\t"      // mie.MSIE
      "csrsi mstatus, 8\n\t"  // mstatus.MIE
      ::: "t0", "memory");
}

static inline void send_ipi(int hart)
{
  volatile unsigned int *msip =
      (volatile unsigned int *)(0x02000000UL + 4UL * (unsigned)hart);
  *msip = 1;
}

// cpu_relax() as the RISC-V kernel emits it.
static inline void cpu_relax_riscv(void)
{
  int dummy;
  __asm__ __volatile__("div %0, %0, zero" : "=r"(dummy));
  __asm__ __volatile__("" ::: "memory");
}

// llist_add(): publish at the head with cmpxchg. Returns 1 if it was empty.
static int llist_add(csd_t *n, llist_head_t *h)
{
  for (;;) {
    unsigned long first = h->first;
    n->next = (volatile struct csd *)first;
    __sync_synchronize();
    if (__sync_bool_compare_and_swap(&h->first, first, (unsigned long)n))
      return first == 0UL;
  }
}

void thread_entry(int cid, int nc)
{
  if (cid >= nc) {
    while (1)
      ;
  }

  initialize_count_asm(0);
  csd[cid].flag  = 0u;
  csd[cid].next  = 0;
  queue[cid].first = 0UL;
  served[cid]    = 0UL;
  rounds_done[cid] = 0UL;
  fail_hart[cid] = 0;
  if (cid == 0) stop = 0;
  barrier(nc);

  ipi_arm();
  barrier(nc);

  if (cid == 0) {
    for (unsigned long r = 1; r <= ROUNDS; r++) {
      // on_each_cpu(): lock, queue and ring every other cpu, then wait on all.
      for (int t = 1; t < nc; t++) {
        csd[t].flag = 0x11u;                 // csd_lock()
        __sync_synchronize();
        llist_add(&csd[t], &queue[t]);       // llist_add()
        send_ipi(t);                         // send_call_function_single_ipi()
      }
      for (int t = 1; t < nc; t++) {
        unsigned long spin = 0;
        while ((csd[t].flag & 1u) != 0u) {   // csd_lock_wait()
          cpu_relax_riscv();
          if (++spin > SPIN_TIMEOUT) { fail_hart[t] = 1; goto done; }
        }
      }
      rounds_done[0] = r;
    }
  } else {
    // Targets do real work with dirty lines outstanding, so the IPI lands
    // mid-stream rather than on an idle hart.
    while (!stop) {
      for (int i = 0; i < DIRTY_LINES; i++)
        dirty[cid][i][0] = dirty[cid][i][0] + 1UL;
    }
    rounds_done[cid] = served[cid];
  }

done:
  if (cid == 0) {
    stop = 1;
    __sync_synchronize();
  }
  barrier(nc);

  if (cid != 0)
    exit(2);

  int ok = (rounds_done[0] == ROUNDS);
  for (int t = 1; t < nc; t++)
    ok &= (fail_hart[t] == 0) && (served[t] == ROUNDS);

  uart_send_string("mt-crosscall: fencei=");
  uart_send_integer(CC_FENCEI);
  uart_send_string(" rounds=");
  uart_send_integer((int)rounds_done[0]);
  uart_send_string("/");
  uart_send_integer(ROUNDS);
  uart_send_string(" served=");
  for (int i = 0; i < 4; i++) {
    uart_send_integer((int)served[i]);
    uart_send_string(i == 3 ? " stuck=" : ",");
  }
  for (int i = 0; i < 4; i++) {
    uart_send_integer(fail_hart[i]);
    uart_send_string(i == 3 ? "\n" : ",");
  }
  if (ok) {
    uart_send_string("mt-crosscall: PASS\n");
    exit(0);
  }
  uart_send_string("mt-crosscall: FAIL (a cross-call was never completed)\n");
  exit(1);
}
