let x: int = 42
let p: ptr = addr(x)
print(deref(p))
deref(p) = 99
print(deref(p))
print(x)
