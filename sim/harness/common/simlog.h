// simlog.h — the debug sink shared by every harness.
//
// A harness has two kinds of output and they must not share a stream:
//
//   * what the SIMULATED PROGRAM transmits (its UART bytes) — this is the
//     result, and on stdout it is the whole point of the run;
//   * what the HARNESS has to say (image loading, progress heartbeats,
//     checkpoint bookkeeping) — useful when debugging, noise otherwise.
//
// The second kind used to be interleaved with the first, which made
// `make linux-sim | tee boot.log` produce a boot log that was half Linux and
// half heartbeat, and unusable as a record of what the kernel actually printed.
//
// Here the harness stream is OFF unless a run explicitly asks for it with
// --debug, and even then it goes to a FILE, never to the console. Nothing has
// to be conditionalised at the call site: SIMLOG() with no sink open is a
// cheap no-op.
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace simlog {

// The sink. nullptr (the default) means every SIMLOG() is discarded.
inline std::FILE *&sink() {
  static std::FILE *f = nullptr;
  return f;
}

inline bool enabled() { return sink() != nullptr; }

// Open `path` as the debug log. Failure is deliberately non-fatal and reported
// on stderr: a debug log is an aid, and it must never be able to kill the run
// it was meant to help. Returns true if the sink is live.
inline bool open(const char *path) {
  if (!path || !*path) return false;
  std::FILE *f = std::fopen(path, "w");
  if (!f) {
    std::fprintf(stderr, "[simlog] cannot open '%s' — debug logging disabled\n",
                 path);
    return false;
  }
  sink() = f;
  std::time_t now = std::time(nullptr);
  char stamp[64];
  std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S",
                std::localtime(&now));
  std::fprintf(f, "# chiron harness debug log — started %s\n", stamp);
  std::fflush(f);
  return true;
}

// Write one line to the sink. Flushed immediately: the runs this exists for are
// the ones that end in a wedge, a kill, or a power cut, and an unflushed buffer
// loses exactly the last lines that mattered.
inline void logf(const char *fmt, ...) {
  std::FILE *f = sink();
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(f, fmt, ap);
  va_end(ap);
  std::fflush(f);
}

inline void close() {
  if (sink()) {
    std::fclose(sink());
    sink() = nullptr;
  }
}

}  // namespace simlog

#define SIMLOG(...) ::simlog::logf(__VA_ARGS__)
