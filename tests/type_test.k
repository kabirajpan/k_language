# int type — explicit
let x: int = 10
let y: int = 20
print(x + y)

# float type
let pi: float = 3
let radius: float = 5
print(pi)

# str type
let name: str = "kabir"
print(name)

# no type — inferred as int (backward compat)
let z = 99
print(z)

# typed function
fn add(a: int, b: int) -> int
    return a + b
end

print(add(3, 7))
