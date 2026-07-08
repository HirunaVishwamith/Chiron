import random

def generate_data_file(
    filename: str,
    data_size: int = 70000,
    bin_size: int = 1000
) -> None:
    """
    Generates random data, counts occurrences, and writes C-style
    array definitions to `filename` in the same format as your printout.
    """
    # 1. Generate the data and the histogram
    input_data = [int(random.random() * bin_size) for _ in range(data_size)]
    result = [0] * bin_size
    for num in input_data:
        result[num] += 1

    # 2. Helper to format an array
    def format_arr(array_type, array_name, array_sz, pyarr):
        lines = []
        lines.append(f"{array_type} {array_name}[{array_sz}] = ")
        lines.append("{")
        lines.append(", ".join(map(str, pyarr)))
        lines.append("};")
        return "\n".join(lines)

    # 3. Write everything to file
    with open(filename, "w") as f:
        f.write(f"#define NUM_BINS {bin_size}\n\n")
        f.write("int output_bins[NUM_BINS];\n\n")
        f.write(f"#define DATA_SIZE {data_size}\n\n")
        f.write(format_arr("int", "input_data", "DATA_SIZE", input_data))
        f.write("\n\n")
        f.write(format_arr("int", "verify_data", "NUM_BINS", result))
        f.write("\n")

if __name__ == "__main__":
    # Example usage:
    generate_data_file("s4.h")
