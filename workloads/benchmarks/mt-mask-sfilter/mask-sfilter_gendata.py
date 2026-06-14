import random

data_size = 64
input_sz = data_size 

X = [int(random.random()*10) for x in range(data_size*data_size)]

mask = [int(random.random()*2) for x in range(data_size*data_size)]

result = [0 for x in range(data_size*data_size)]




for i in range(1, data_size-1):
    for j in range(1, data_size-1):
        if mask[i+j*data_size] != 0:
            i0 = X[(i-1)+(j-1)*input_sz];
            i1 = X[(i)  +(j-1)*input_sz];
            i2 = X[(i+1)+(j-1)*input_sz];
            i3 = X[(i-1)+(j)  *input_sz];
            i4 = X[(i)  + j  * input_sz];
            i5 = X[(i+1)+(j)  *input_sz];
            i6 = X[(i-1)+(j+1)*input_sz];
            i7 = X[(i)  +(j+1)*input_sz];
            i8 = X[(i+1)+(j+1)*input_sz];
            result[i+j*data_size] = i0 + i1 + i2 + i3 + i4 + i5 + i6 + i7 + i8
        else:
            result[i+j*data_size] = X[i+j*data_size]


def print_arr(array_type, array_name, array_sz, pyarr):
    print "{} __attribute__((aligned(16))) {}[{}] = ".format(array_type, array_name, array_sz)
    print "{"
    print ", ".join(map(str, pyarr))
    print "};"


def print_scalar(scalar_type, scalar_name, pyscalar):
    print "{} {} = {};".format(scalar_type, scalar_name, pyscalar)

print "#define DATA_SIZE {}".format(data_size)
print "#define data_t int"

print_arr("int", "input", "DATA_SIZE*DATA_SIZE", X)

print_arr("short", "mask", "DATA_SIZE*DATA_SIZE", mask)

print_arr("int", "verify_data", "DATA_SIZE*DATA_SIZE", result)
