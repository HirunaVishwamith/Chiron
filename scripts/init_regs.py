# get all the lines containing register initializations
modified_lines = []

with open('core.v', 'r') as file:
  for line_no, line in enumerate(file.read().split('\n')):
    terms = line.split()
    # is register assignment
    try:
      if (terms[0] == 'reg') and ('_RAND_' not in line) and ('];' not in line):
        wire_len = (0 if (terms[1][0] != '[') else int(terms[1][1:terms[1].index(':')])) + 1
        terms[1 if (terms[1][0] != '[') else 2] = terms[1 if (terms[1][0] != '[') else 2][:-1] + " = " + str(wire_len) + "'h0;"
        modified_lines.append("  " + ' '.join(terms))
      else:
        if ('];' in line) and (terms[0] == 'reg'):
          print ("Found vectored reg in line ", line_no)
        modified_lines.append(line)
    except:
      print(line_no)

with open("modified_core.v", 'w') as file:
  file.write('\n'.join(modified_lines))