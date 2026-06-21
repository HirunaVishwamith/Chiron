// args.h — tiny argv scanner shared by every harness.
//
// No getopt, no dependencies: harnesses take a handful of `--flag value` pairs
// and these three helpers cover every case. All run from the repo root.
#pragma once

#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <vector>

namespace harness {

// Value following the first occurrence of `flag`, or `def` if absent.
inline const char *find_arg(int argc, char **argv, const char *flag,
                            const char *def = nullptr) {
  for (int i = 1; i < argc - 1; ++i)
    if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
  return def;
}

// True if `flag` appears anywhere (presence-only switch, e.g. --show-state).
inline bool has_flag(int argc, char **argv, const char *flag) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], flag) == 0) return true;
  return false;
}

// Every value following a repeated `flag`, parsed as hex/decimal (strtoull base 0).
// Used for --done-pc, which may be given more than once.
inline std::vector<uint64_t> collect_hex_args(int argc, char **argv,
                                              const char *flag) {
  std::vector<uint64_t> out;
  for (int i = 1; i < argc - 1; ++i)
    if (std::strcmp(argv[i], flag) == 0)
      out.push_back(std::strtoull(argv[i + 1], nullptr, 0));
  return out;
}

}  // namespace harness
