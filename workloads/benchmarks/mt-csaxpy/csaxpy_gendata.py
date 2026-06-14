import random

data_size = 1000

def select(cond, a, b):
  if cond:
    return a
  else:
    return b

a = int(random.random()*1000.00)
X = [int(random.random()*1000.00) for x in range(data_size)]
Y = [int(random.random()*1000.00) for x in range(data_size)]
cond = [select((random.random() > 0.5),  "true", "false") for x in range(data_size)]
result = []

for i in range(data_size):
  if cond[i] == "true":
    result.append(a*X[i] + Y[i])
  else:
    result.append(Y[i])

def print_arr(array_type, array_name, array_sz, pyarr):
    print "{} {}[{}] = ".format(array_type, array_name, array_sz)
    print "{"
    print ", ".join(map(str, pyarr))
    print "};"


def print_scalar(scalar_type, scalar_name, pyscalar):
    print "{} {} = {};".format(scalar_type, scalar_name, pyscalar)

print "#define DATA_SIZE {}".format(data_size)

print_scalar("int", "input_data_a", a)

print_arr("int", "input_data_X", "DATA_SIZE", X)

print_arr("int", "input_data_Y", "DATA_SIZE", Y)

print_arr("int", "verify_data", "DATA_SIZE", result)

print_arr("unsigned char", "cond_data", "DATA_SIZE", cond)
