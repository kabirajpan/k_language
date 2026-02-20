# K Language — Full Roadmap & Technical Details
# Based on research papers + all improvements discussed

---

## Vision (updated)

K is a compiled, low-level programming language that:
- Beats C in raw performance on most workloads
- Is safer than C by design (no undefined behaviour, ownership model)
- Is simpler than Rust (no borrow checker complexity)
- Is fully transparent — you always know exactly what asm your code produces
- Has zero runtime, zero GC, zero dependencies
- Eventually compiles itself

---

## Current State (what works today)

| Feature         | Status |
|-----------------|--------|
| Lexer           | ✅ done |
| Parser          | ✅ done |
| Codegen x86-64  | ✅ done |
| let / math      | ✅ done |
| if/elif/else    | ✅ done |
| while           | ✅ done |
| for / step      | ✅ done |
| fn / return     | ✅ done |
| print()         | ✅ done |
| != >= <=        | ✅ done |
| r10 scratch reg | ✅ done |
| exact stack     | ✅ done |
| loop at bottom  | ✅ done |
| limit/step hoist| ✅ done |

---

## Stage 1 — Complete Core Language (C compiler)
> Goal: K can write real programs

### 1.1 — Strings (next immediate task)
- Lexer: scan "hello" → TOK_STRING
- Codegen: emit string into .data section with unique label
- print() works with strings
- str type added

```
let name: str = "kabir"
print(name)
print("hello world")
```

### 1.2 — Type system
Every variable has an explicit type. Compiler enforces it.

```
let x: int   = 10
let f: float = 3.14
let s: str   = "hello"
let p: ptr   = 0
```

Types: int (64-bit), float (64-bit double), str, ptr
No unsigned, no short, no long long — clean and simple.

### 1.3 — Immutable by default
Variables are immutable unless declared with mut.
Compile-time check only — zero runtime cost.

```
let x: int = 10       # immutable — compiler error if reassigned
let mut y: int = 10   # mutable — can reassign
```

### 1.4 — Fixed arrays
Stack-allocated, bounds known at compile time.

```
let nums: int[10]
nums[0] = 42
nums[1] = 99
print(nums[0])
```

Codegen: array is a block on the stack, index * 8 offset.

### 1.5 — Typed function signatures
```
fn add(a: int, b: int) -> int
    return a + b
end
```

Compiler checks argument types and return type.

### 1.6 — Multiple return values
```
fn min_max(a: int, b: int) -> int, int
    return a, b
end

let lo, hi = min_max(3, 7)
```

Codegen: return values in rax and rdx (System V ABI allows this).

### 1.7 — Named arguments
```
fn create(width: int, height: int, fullscreen: int) -> ptr
end

let w = create(width=1920, height=1080, fullscreen=0)
```

Parser maps names to positions. Zero runtime cost.

### 1.8 — match statement
Compiles to a jump table — faster than if/elif chains.

```
match x
    0        -> print(0)
    1 to 10  -> print(1)
    else     -> print(99)
end
```

### 1.9 — Compile-time execution (comptime)
Evaluated entirely by compiler, inlined as constant.

```
let MAX: int      = comptime(1024 * 1024 * 8)
let TABLE: int    = comptime(MAX * 2 + 1)
let SIZE: int     = comptime(sizeof(int) * 100)
```

### 1.10 — sizeof
```
let n: int = sizeof(int)      # 8
let n: int = sizeof(Point)    # sum of field sizes
```

---

## Stage 2 — Memory & Data Structures

### 2.1 — Structs
```
struct Point
    x: int
    y: int
end

struct Player
    x: int
    y: int
    health: int
    name: str
end

let p = Point(10, 20)
print(p.x)
p.y = 99
```

Codegen: struct is a contiguous block on stack.
Field access = base offset + field offset.

### 2.2 — Pointers
Clean syntax — no * and & confusion.

```
let x: int = 42
let p: ptr = addr(x)     # take address
let v: int = deref(p)    # dereference
deref(p) = 99            # write through pointer
```

Codegen: addr → lea instruction, deref → mov [reg].

### 2.3 — Manual memory (alloc/free)
Direct mmap syscall — no libc, no malloc overhead.

```
let buf: ptr = alloc(1024)    # mmap syscall directly
buf[0] = 42
free(buf)                     # munmap syscall
```

### 2.4 — Ownership model (from research — Dhurjati 2003)
Single owner, freed at end of scope automatically.
No borrow checker complexity — simpler than Rust.

```
fn process()
    let buf: ptr = alloc(1024)   # buf owns this
    buf[0] = 42
    # compiler emits free(buf) here automatically
    # no manual free needed, no leak possible
end
```

Rules:
- One owner per allocation
- Owner freed when it goes out of scope
- Can transfer ownership by returning
- Cannot use after free — compile-time error

### 2.5 — Option type (no null crashes)
```
let result = find(arr, 99)
if result == none
    print(0)
else
    print(result)
end
```

none is a compile-time concept. Codegen: none = 0 sentinel.

### 2.6 — Error handling (multiple return)
```
fn divide(a: int, b: int) -> int, int
    if b == 0
        return 0, 1    # value=0, err=1
    end
    return a / b, 0    # value, err=0
end

let result, err = divide(10, 0)
if err != 0
    print(err)
end
```

---

## Stage 3 — Compiler Optimizations (research-backed)

### 3.1 — Linear Scan Register Allocator
**Based on: Poletto & Sarkar 1999**

This is the single most impactful change to K's performance.

How it works:
1. Scan all variables in a function
2. Calculate live intervals (where each variable is alive)
3. Assign variables to registers greedily
4. Spill to memory only when registers run out

Available registers for allocation: r12, r13, r14, r15
(callee-saved — preserved across function calls)

Before (current K):
```nasm
mov rax, [rbp-8]    # load i from RAM every iteration
add rax, 1
mov [rbp-8], rax    # store back to RAM
```

After (with register allocator):
```nasm
add r12, 1          # i lives in r12, never touches RAM
```

Expected speedup: 2-3x on loop-heavy code.
Closes the gap with GCC -O2 to within 10-15%.

### 3.2 — Common Subexpression Elimination (CSE)
If the same expression is computed twice, compute it once.

```
# K source
total = total + i * i
let check = i * i + 1
```

Without CSE: computes i*i twice.
With CSE: computes i*i once, reuses result.

### 3.3 — Loop Invariant Code Motion
Move expressions that don't change out of loops.

```
# K source
for i = 0 to 1000000
    let factor = width * height    # doesn't change!
    total = total + i * factor
end
```

Compiler detects width*height doesn't use i,
moves it before the loop. Runs once not 1M times.

### 3.4 — Loop Tiling (cache optimization)
**Inspired by: text editor virtualization concept**

Split large loops into cache-sized chunks.
L1 cache = 32KB = fits ~4000 integers.

```
# compiler automatically transforms this
for i = 0 to 1000000
    process(arr[i])
end

# into this (tile size tuned to L1 cache)
for block = 0 to 1000000 step 64
    for i = block to block + 64
        process(arr[i])
    end
end
```

Expected speedup on array-heavy code: 2-4x.

### 3.5 — Prefetch hints
Tell CPU to load data into cache before you need it.

```
prefetch(arr, i + 64)    # load arr[i+64] into cache now
for i = 0 to 1000000
    total = total + arr[i]
end
```

CPU fetches arr[i+64] while computing arr[i].
Hides memory latency entirely on large arrays.

### 3.6 — Instruction scheduling
Reorder instructions to avoid CPU pipeline stalls.
Modern CPUs execute out-of-order — K's codegen should
emit instructions in an order that feeds the pipeline.

---

## Stage 4 — Inline Assembly & SIMD

### 4.1 — Inline asm blocks
First-class, cleaner than C's asm volatile.

```
fn fast_copy(dst: ptr, src: ptr, n: int)
    asm
        mov rdi, dst
        mov rsi, src
        mov rcx, n
        rep movsb
    end
end
```

### 4.2 — SIMD as first-class syntax
Process 4/8 integers simultaneously.

```
let va: int[4] = [1, 2, 3, 4]
let vb: int[4] = [5, 6, 7, 8]
let vc: int[4] = simd_add(va, vb)    # emits AVX2 vpaddd
# vc = [6, 8, 10, 12] — all 4 in one instruction
```

C requires intrinsics: `_mm256_add_epi64()` — ugly and non-portable.
K makes it syntax.

### 4.3 — Built-in benchmarking
```
benchmark "matrix multiply"
    # code here
end
# outputs: 124ns / 312 cycles / 2.1x faster than last run
```

---

## Stage 5 — K Standard Library (written in K)

Everything written in K itself — no C, no libc.

```
stdlib/
    string.k    — length, concat, slice, find
    array.k     — push, pop, sort, search
    io.k        — file read/write (direct syscalls)
    math.k      — sqrt, pow, abs, min, max
    mem.k       — alloc, free, copy, zero
    os.k        — syscall wrappers
    fmt.k       — number and string formatting
```

All hot paths written with inline asm.
String ops use SIMD where possible.

---

## Stage 6 — Self Hosting

K compiler rewritten in K itself.

```
compiler/
    lexer.k     — tokenizer
    parser.k    — AST builder
    codegen.k   — x86-64 emitter
    regalloc.k  — register allocator
    runner.k    — entry point
```

Bootstrap process:
1. Compile K compiler using C compiler (current)
2. Rewrite compiler in K
3. Compile K compiler using itself
4. Verify output binaries match
5. Drop C compiler forever

---

## Final Performance Target

After all stages complete:

| Benchmark          | K target      | GCC -O2     |
|--------------------|---------------|-------------|
| Integer loops      | equal         | baseline    |
| Array operations   | faster (+20%) | baseline    |
| String processing  | faster (+15%) | baseline    |
| SIMD math          | faster (+30%) | baseline    |
| Memory copy        | equal         | baseline    |
| Compile speed      | 10x faster    | baseline    |

K beats GCC -O2 on array and SIMD workloads because:
- Loop tiling tuned precisely to hardware cache size
- Prefetch hints placed by programmer not guessed by optimizer
- SIMD is first-class not hinted via intrinsics

---

## Full Timeline

```
Stage 1 — Core language      strings, types, arrays, structs
Stage 2 — Memory             pointers, alloc/free, ownership
Stage 3 — Optimizations      register allocator, CSE, tiling
Stage 4 — SIMD + asm         inline asm, AVX2, prefetch
Stage 5 — Stdlib             everything in K, no libc
Stage 6 — Self hosting        K compiles K, C dropped forever
```

---

## K vs Everyone — Final State

| | C | Rust | K (final) |
|---|---|---|---|
| Speed | fast | fast | equal or faster |
| Memory safety | no | yes (complex) | yes (simple) |
| Null safety | no | yes | yes |
| Compile speed | fast | very slow | very fast |
| Syntax | medium | complex | clean |
| Inline asm | awkward | awkward | first class |
| SIMD | intrinsics | intrinsics | native syntax |
| Multiple return | no | no | yes |
| Named args | no | no | yes |
| comptime | macros | const fn | comptime keyword |
| Deterministic build | no | no | yes |
| Dependencies | libc | std | none |
| Runtime | minimal | small | zero |
| Predictable output | no | no | 100% yes |

---

## Research Papers This Roadmap Is Based On

1. Poletto & Sarkar 1999 — Linear Scan Register Allocation
   ACM Transactions on Programming Languages
   → Stage 3.1 register allocator

2. Dhurjati et al. 2003 — Memory Safety Without Runtime Checks
   ACM SIGPLAN
   → Stage 2.4 ownership model

3. Compiler Optimization Techniques 2023
   E3S Web of Conferences
   → Stage 3 optimization order

4. Lozano et al. — Combinatorial Register Allocation
   ACM / arXiv
   → Stage 3.1 implementation details

5. Rust / CACM — Safe Systems Programming
   Communications of the ACM
   → Stage 2 memory model design
