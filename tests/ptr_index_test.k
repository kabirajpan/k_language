let buf: ptr = alloc(64)
buf[0] = 10
buf[1] = 20
buf[2] = 30
print(buf[0])
print(buf[1])
print(buf[2])
free(buf, 64)
