let buf: ptr = alloc(1024)
deref(buf) = 42
print(deref(buf))
deref(buf) = 100
print(deref(buf))
free(buf, 1024)
print(99)
