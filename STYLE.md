# K Language Code Style Guide

---

## Naming

```
# variables - snake_case
let my_var: int = 10
let total_count: int = 0

# mutable variables - snake_case with mut
let mut counter: int = 0

# functions - snake_case
fn add_two(a: int, b: int) -> int
    return a + b
end

# constants - UPPER_SNAKE_CASE
let MAX_SIZE: int = comptime(1024 * 8)
let BUFFER_SIZE: int = comptime(MAX_SIZE * 2)

# structs - PascalCase
struct PlayerState
    x: int
    y: int
    health: int
end

# struct instances - snake_case
let player = PlayerState(0, 0, 100)

# pointers - snake_case with _ptr suffix
let buf_ptr: ptr = alloc(1024)

# file descriptors - snake_case with _fd suffix
let input_fd: int = open("input.txt", 0)
```

---

## Types

Always declare types explicitly. Never rely on inference in serious code.

```
# good
let x: int   = 10
let f: float = 3
let s: str   = "hello"
let p: ptr   = alloc(1024)
let b: bool  = true

# bad — inference is only for quick throwaway code
let x = 10
```

Available types:
- `int`   — 64-bit integer
- `float` — 64-bit double
- `str`   — string literal
- `ptr`   — raw pointer
- `bool`  — true / false

---

## Indentation

- 4 spaces — no tabs
- indent inside `fn`, `if`, `elif`, `else`, `while`, `for`, `match`, `struct`

```
# good
fn add(a: int, b: int) -> int
    let result: int = a + b
    return result
end

# bad
fn add(a: int, b: int) -> int
  let result: int = a + b
return result
end
```

---

## Spacing

```
# operators — always space around them
let x: int = 5 + 3
let y: int = x * 2

# function calls — no space before paren
print(x)
add(a, b)

# commas — space after
fn add(a: int, b: int, c: int) -> int

# struct fields — space after colon
struct Point
    x: int
    y: int
end
```

---

## Functions

- one job per function
- keep functions short
- return early when possible
- always declare param types and return type

```
# good
fn is_positive(x: int) -> bool
    if x > 0
        return true
    end
    return false
end

# good — multiple return for error handling
fn safe_divide(a: int, b: int) -> int, int
    if b == 0
        return 0, 1    # value, err
    end
    return a / b, 0
end

# bad — too many jobs
fn do_everything(x: int) -> int
    let a: int = x + 1
    let b: int = a * 2
    let c: int = b - 3
    print(a)
    print(b)
    print(c)
    return c
end
```

---

## Multiple Return Values

Use two return values for error handling. First value is result, second is error code.

```
# 0 = no error, non-zero = error
let result, err = safe_divide(10, 0)
if err != 0
    print(err)
end
```

---

## Memory

- always pair `alloc` with `free` unless inside a function (ownership handles it)
- inside functions, ownership model auto-frees at scope end
- never use a pointer after `free`

```
# inside function — no manual free needed
fn process_data()
    let buf: ptr = alloc(1024)    # owned by this scope
    deref(buf) = 42
    print(deref(buf))
    # compiler emits free here automatically
end

# at top level — free manually
let buf: ptr = alloc(1024)
deref(buf) = 99
print(deref(buf))
free(buf, 1024)
```

---

## Pointers

Use `addr` and `deref` — never raw pointer arithmetic yet.

```
# good
let x: int = 42
let p: ptr = addr(x)
let v: int = deref(p)
deref(p) = 99

# document why you need a pointer
# p points to x so we can pass it to process()
let p: ptr = addr(x)
```

---

## Structs

- define structs at top of file
- use constructor syntax for initialisation
- one field per line

```
# good
struct Point
    x: int
    y: int
end

let p = Point(10, 20)
print(p.x)
p.y = 99

# bad — don't cram fields
struct Point
    x: int y: int    # wrong — one per line
end
```

---

## File I/O

Always close file descriptors. Always check with a comment what flags mean.

```
# flags: 0 = read only, 1 = write only, 2 = read+write
let fd: int = open("data.txt", 0)
let buf: ptr = alloc(1024)
read(fd, buf, 1024)
write(1, buf, 64)    # 1 = stdout
close(fd)
free(buf, 1024)
```

---

## Comptime

Use `comptime` for all constants that involve math. Never hardcode computed values.

```
# good
let MAX: int    = comptime(1024 * 8)
let DOUBLE: int = comptime(MAX * 2)
let BUF: int[8]

# bad
let MAX: int    = 8192     # where did 8192 come from?
let DOUBLE: int = 16384
```

---

## Match

Prefer `match` over long `if/elif` chains for discrete values.

```
# good
match status
    0 -> print("ok")
    1 -> print("error")
    2 -> print("timeout")
    else -> print("unknown")
end

# bad — use match instead
if status == 0
    print("ok")
elif status == 1
    print("error")
elif status == 2
    print("timeout")
end
```

---

## Comments

```
# single line comment only
# describe WHY not WHAT

# bad:  add 1 to x
# good: offset by 1 to account for zero-indexing

# every function gets a comment above it
# divides a by b, returns result and error code
# err = 1 if b is zero
fn safe_divide(a: int, b: int) -> int, int
    if b == 0
        return 0, 1
    end
    return a / b, 0
end
```

---

## File Layout

```
# 1. comptime constants at top
let MAX: int = comptime(1024 * 8)

# 2. struct definitions
struct Point
    x: int
    y: int
end

# 3. helper functions

# 4. main logic at bottom
```

---

## Blocks

- `end` always on its own line
- never leave an empty block
- never nest more than 3 levels deep — extract to function instead

```
# good
if x > 0
    print(x)
end

# bad
if x > 0
end

# bad — too deep, extract inner logic to a function
for i = 0 to 10
    if i > 5
        while running
            if check
                print(i)
            end
        end
    end
end
```

---

## Numbers

No magic numbers — assign to a named comptime variable first.

```
# good
let max_retries: int = 3
while retries < max_retries
    retries = retries + 1
end

# bad
while retries < 3
    retries = retries + 1
end
```

---

## General Rules

- one statement per line
- no deeply nested blocks — extract to functions instead
- keep files under 300 lines
- test every function
- every ptr variable name should make ownership clear
- every fd variable name should end in _fd
