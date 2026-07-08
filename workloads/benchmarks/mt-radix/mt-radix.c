//**************************************************************************
// mt-radix: SPLASH-3 Radix (integer radix sort) port
//--------------------------------------------------------------------------
// Transliterated from SakalisC/Splash-3's codes/kernels/radix/radix.c.in
// (original Stanford SPLASH-2/3) into chiron's bare-metal SPMD benchmark
// idiom, not compiled as-is: the original assumes argc/argv (getopt),
// dynamic thread spawning (CREATE(slave_sort, P)), and a >1024-processor
// binary-tree prefix-sum merge -- none of which apply here (every hart
// already starts at thread_entry() via crt.S; NUM_CORES is fixed at compile
// time; 4 cores don't need a tree merge, a serial prefix-sum by core0 over
// the other cores' histograms is simpler and just as correct).
//
// What's preserved from the original: the real three-phase radix-sort
// structure -- per-core histogram of the current digit, a global prefix-sum
// over all cores' histograms, then a parallel permute that scatters every
// core's keys across the whole shared destination array by rank. That
// permute phase is the interesting part for cross-core memory/coherence
// traffic (each core writes essentially randomly across the other cores'
// cache lines), which is what makes Radix a useful stress test independent
// of whether the actual sorted values are "real" data.
//
// What's NOT preserved (per the chosen structure/concurrency-only fidelity,
// not numerical fidelity -- chiron's toolchain has no F/D anyway): the
// original's key-generation RNG (ran_num_init/product_mod_46) uses double
// arithmetic for exact 46-bit modular multiplication. That's replaced with
// splitmix64 -- an ordinary integer PRNG. The point of this benchmark is the
// sort's memory-access pattern, not reproducing SPLASH's reference keys.
//--------------------------------------------------------------------------
#include "util.h"

#define MAX_CORES   4
#define NUM_KEYS    2048          // must be divisible by MAX_CORES
#define RADIX_BITS  8
#define RADIX       (1 << RADIX_BITS)
#define MAX_KEY     65536         // 16-bit keys -> 2 passes of 8 bits each
#define NUM_PASSES  2

extern void __attribute__((noinline)) barrier(int ncores);
extern void uart_send_string(const char *s);
extern void uart_send_integer(int n);
extern void exit(int status);

static long key[2][NUM_KEYS];
static long hist[MAX_CORES][RADIX];
static long prefix[MAX_CORES][RADIX];

static inline unsigned long splitmix64(unsigned long *state)
{
  unsigned long z = (*state += 0x9E3779B97F4A7C15UL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9UL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBUL;
  return z ^ (z >> 31);
}

// Returns 0 if key[arr][0..n) is sorted ascending, else 1-based index of the
// first out-of-order element.
static int verify_sorted(int n, const volatile long *arr)
{
  for (int i = 1; i < n; i++)
    if (arr[i - 1] > arr[i]) return i;
  return 0;
}

void thread_entry(int cid, int nc)
{
  initialize_count_asm(0);

  const int chunk = NUM_KEYS / nc;
  const int start = cid * chunk;
  const int end   = (cid == nc - 1) ? NUM_KEYS : start + chunk;

  // Each core generates its own key partition -- no shared RNG state, no
  // locking needed for this phase.
  unsigned long seed = 0x9E3779B9UL + (unsigned long)cid;
  for (int i = start; i < end; i++)
    key[0][i] = (long)(splitmix64(&seed) & (MAX_KEY - 1));

  barrier(nc);

  int from = 0, to = 1;
  for (int pass = 0; pass < NUM_PASSES; pass++) {
    const int shift = pass * RADIX_BITS;

    // Phase 1: per-core histogram of this pass's digit.
    for (int d = 0; d < RADIX; d++) hist[cid][d] = 0;
    for (int i = start; i < end; i++) {
      int digit = (key[from][i] >> shift) & (RADIX - 1);
      hist[cid][digit]++;
    }
    barrier(nc);

    // Phase 2: global exclusive prefix sum, digit-major then core-minor (so
    // a stable partition results: for each digit, core 0's matching keys
    // land before core 1's, etc). Serial by core0 -- simpler than SPLASH's
    // tree merge and just as correct at MAX_CORES=4.
    if (cid == 0) {
      long sum = 0;
      for (int d = 0; d < RADIX; d++) {
        for (int p = 0; p < nc; p++) {
          long c = hist[p][d];
          prefix[p][d] = sum;
          sum += c;
        }
      }
    }
    barrier(nc);

    // Phase 3: parallel permute -- every core scatters its keys to their
    // final rank across the WHOLE shared destination array (real cross-core
    // memory/coherence traffic, not just core-local writes).
    for (int i = start; i < end; i++) {
      int digit = (key[from][i] >> shift) & (RADIX - 1);
      long pos = prefix[cid][digit]++;
      key[to][pos] = key[from][i];
    }
    barrier(nc);

    int t = from; from = to; to = t;
  }

  if (cid == 0) {
    int err = verify_sorted(NUM_KEYS, key[from]);
    if (err) {
      uart_send_string("mt-radix: FAIL at index ");
      uart_send_integer(err);
      uart_send_string("\n");
    } else {
      uart_send_string("mt-radix: ALL SORTED OK\n");
    }
    exit(err ? 1 : 0);
  } else {
    exit(2);
  }
}
