// terminal.h — raw-mode TTY helpers used by the golden model's UART path.
//
// The emulator reads keystrokes from stdin and echoes program output, so it
// flips the controlling terminal into raw mode (no line buffering, no echo)
// while running and restores it on exit.
#pragma once

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// Non-zero if a byte is waiting on stdin (non-blocking key poll).
inline int kbhit() {
  int byteswaiting;
  ioctl(0, FIONREAD, &byteswaiting);
  return byteswaiting > 0;
}

inline void enable_raw_mode() {
  termios term;
  tcgetattr(0, &term);
  term.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(0, TCSANOW, &term);
}

inline void disable_raw_mode() {
  termios term;
  tcgetattr(0, &term);
  term.c_lflag |= ICANON | ECHO;
  tcsetattr(0, TCSANOW, &term);
}
