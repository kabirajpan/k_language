# K Language Code Style Guide

---

## Naming

```
# variables - snake_case
let my_var = 10
let total_count = 0

# functions - snake_case
fn add_two(a, b)
    return a + b
end

# constants - UPPER_SNAKE_CASE
let MAX_SIZE = 1024
let PI = 3
```

---

## Indentation

- 4 spaces — no tabs
- indent inside `fn`, `if`, `while`, `for`

```
# good
fn add(a, b)
    let result = a + b
    return result
end

# bad
fn add(a, b)
  let result = a + b
return result
end
```

---

## Spacing

```
# operators — always space around them
let x = 5 + 3
let y = x * 2

# function calls — no space before paren
print(x)
add(a, b)

# commas — space after
fn add(a, b, c)
```

---

## Functions

- one job per function
- keep functions short
- return early when possible

```
# good
fn is_positive(x)
    if x > 0
        return 1
    end
    return 0
end

# bad
fn do_everything(x)
    let a = x + 1
    let b = a * 2
    let c = b - 3
    print(a)
    print(b)
    print(c)
    return c
end
```

---

## Comments

```
# single line comment

# describe WHY not WHAT
# bad:  add 1 to x
# good: offset by 1 to account for zero-indexing

# every function gets a comment above it
# adds two numbers and returns the result
fn add(a, b)
    return a + b
end
```

---

## File Layout

```
# 1. constants at top
let MAX = 100

# 2. helper functions

# 3. main logic at bottom
```

---

## Blocks

- `end` always on its own line
- never leave an empty block

```
# good
if x > 0
    print(x)
end

# bad
if x > 0
end
```

---

## Numbers

- no magic numbers — assign to a named variable first

```
# good
let max_retries = 3
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
