import random

data_size = 100000
bin_size = 1000

input_data = [int(random.random() * bin_size) for x in range(data_size)]

result = [0] * bin_size

for num in input_data:
    result[num] += 1

def print_arr(array_type, array_name, array_sz, pyarr):
    print "{} {}[{}] = ".format(array_type, array_name, array_sz)
    print "{"
    print ", ".join(map(str, pyarr))
    print "};"

print "#define NUM_BINS {}".format(bin_size)

print "int output_bins[NUM_BINS];"

print "#define DATA_SIZE {}".format(data_size)

print_arr("int", "input_data", "DATA_SIZE", input_data)

print_arr("int", "verify_data", "NUM_BINS", result)
