# evaluated entirely at compile time
let MAX: int    = comptime(1024 * 8)
let DOUBLE: int = comptime(MAX * 2)
let HALF: int   = comptime(MAX / 2)

print(MAX)
print(DOUBLE)
print(HALF)

# use in array size — array of MAX/1024 = 8 elements
let buf: int[8] = {1, 2, 3, 4, 5, 6, 7, 8}
print(buf[0])
print(buf[7])

# comptime in expression
let x: int = comptime(10 * 10 + 5 * 2)
print(x)
