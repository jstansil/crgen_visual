net = open("output.txt", 'r')
lines = net.readlines()
net.close()

net = open("output.txt", 'w')
for line in lines:
    items = line.split()
    if items[0] == 'bc':
        net.write(line)

net.close()
print ('done!')
