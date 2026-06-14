# Mt-Benchmark

Mt-Benchmark is a RISC-V multithreaded benchmark suite designed to evaluate the performance of RISC-V processors under multithreaded workloads. It provides tools to compile and generate both binary (`.bin`) and dump (`.dump`) files for analysis.

## Features

- **Multithreaded Benchmarks**: Assess the performance of RISC-V processors with concurrent execution scenarios.
- **Customizable Compilation**: Utilize the provided `Makefile` to compile benchmarks tailored to specific RISC-V architectures.
- **Output Artifacts**: Generate `.bin` binary files and `.dump` files containing detailed assembly and memory information.

## Repository Structure

The repository is organized as follows:


## Prerequisites

- **RISC-V Toolchain**: Ensure that the RISC-V GCC compiler (`riscv64-unknown-elf-gcc`) is installed and accessible in your system's `PATH`.

## Compilation Instructions

1. **Clone the Repository**:

   ```
   git clone https://github.com/HirunaVishwamith/Mt-Benchmark.git
   cd Mt-Benchmark
   ```
   
2. **Compile the Benchmarks**:

Simply run the following command:

```
  make all
```

This command will compile the benchmarks and generate the following files:
```
mt-matmul.bin: The final RISC-V binary executable.
mt-matmul.riscv.dump: The assembly dump of the compiled binary.
```
