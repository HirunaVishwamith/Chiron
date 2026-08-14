//**************************************************************************
// mt-uartrx: does the uartlite RX path actually deliver host keystrokes?
//--------------------------------------------------------------------------
// The quad-core Linux boot reached "buildroot login: " and could never be
// logged into, because the console was output-only: system.scala tied
// `hostInput` off, and uartPort's compiled-in ROM answered the LEGACY register
// map (status at +0x00, data at +0x04) which Linux's xilinx-uartlite driver
// never reads. It polls +0x08 for RX_VALID and pops +0x00.
//
// The fix adds a real uartlite RX: STATUS at +0x08 reports RX_VALID, the byte
// pops at +0x00, and a top-level hostInput port lets the C++ harness forward
// the host's stdin. That fix is worth exactly nothing if it does not actually
// work, and the only way it was going to be discovered otherwise is 14 hours
// into a Linux boot, at the login prompt, with no way to type. Hence this test:
// it exercises the identical registers the Linux driver uses, in seconds.
//
// hart0 reads RX_CHARS bytes off the uartlite RX FIFO and echoes each one back
// over TX. The harness (sim/harness/uartrx_test.cpp) drives a known string in
// and compares what comes out, so a pass means the bytes survived the round
// trip host -> hostInput -> AXI read -> core -> TX -> host intact and in order.
//
// Deliberately NOT using the ROM path: the ROM only unlocks once terminalReady
// has seen a login prompt, which never happens in a bare-metal run. This drives
// hostInput directly, which is the path `make linux-sim` uses for your
// keystrokes.
//
//   RX_CHARS=n   how many bytes to read back (must match the harness)
//**************************************************************************

#include "util.h"

extern void uart_send_string(const char *s);

// Xilinx uartlite register map — the one the Linux driver speaks.
#define UART_RX           0x40600000
#define UART_STS          0x40600008
#define UART_STS_RX_VALID (1u << 0)

#ifndef RX_CHARS
#define RX_CHARS 12
#endif

// Bound the poll so a broken RX fails loudly instead of hanging the run out to
// the harness's cycle cap with no clue why.
#ifndef RX_SPIN_LIMIT
#define RX_SPIN_LIMIT 20000000u
#endif

void thread_entry(int cid, int nc)
{
  // Only hart0 touches the console; the others must not race it for the FIFO.
  if (cid != 0) {
    while (1)
      ;
  }
  (void)nc;

  volatile unsigned int *sts = (volatile unsigned int *)UART_STS;
  volatile unsigned int *rx  = (volatile unsigned int *)UART_RX;

  char got[RX_CHARS + 1];

  for (int i = 0; i < RX_CHARS; i++) {
    unsigned int spins = 0;
    while (!((*sts) & UART_STS_RX_VALID)) {
      if (++spins >= RX_SPIN_LIMIT) {
        uart_send_string("\nRXFAIL: STATUS never asserted RX_VALID\n");
        exit(1);
      }
    }
    got[i] = (char)((*rx) & 0xffu);
  }
  got[RX_CHARS] = '\0';

  // Echo between markers so the harness can locate the payload unambiguously
  // even if anything else ever prints on this port.
  uart_send_string("<");
  uart_send_string(got);
  uart_send_string(">");
  exit(0);
}
