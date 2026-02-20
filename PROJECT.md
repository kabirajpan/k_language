# K Language — Project Details

---

## Vision

K is a compiled, low-level programming language designed to **beat C in raw performance**
by giving the programmer direct control over hardware while keeping syntax clean and simple.

Where C abstracts the machine slightly, K exposes it fully.
Where C relies on the compiler to optimize, K lets you dictate exactly what happens.

---

## How We Beat C

### 1. Assembly Where It Matters
K's compiler emits hand-optimized x86-64 NASM assembly directly.
No intermediate IR. No LLVM. No GCC middle-end guessing what you meant.

```
K source → AST → raw x86-64 asm → nasm → binary
C source → tokens → IR → optimization passes → asm → binary
```

C has more steps. More steps = more assumptions. More assumptions = slower code.

### 2. Zero Runtime Overhead
- No garbage collector
- No runtime type checking
- No exception handling overhead
- No hidden function call wrappers
- Direct syscalls where possible — no libc middleman

### 3. Manual Memory Control
You decide when and how memory is allocated.
No malloc hiding latency behind your back.

### 4. SIMD by Default (planned)
K will expose SIMD instructions (SSE/AVX) as first-class syntax.
C requires intrinsics or compiler hints. K will do it natively.

### 5. Cache-Friendly Data Layouts
K encourages flat arrays and struct-of-arrays layouts.
No hidden pointer chasing. Data lives where the CPU expects it.

---

## K vs C — Feature Comparison

| Feature                  | K                          | C                          |
|--------------------------|----------------------------|----------------------------|
| Compilation target       | Direct x86-64 asm          | Machine code via GCC/Clang |
| Runtime                  | None                       | libc (minimal)             |
| Memory management        | Manual                     | Manual                     |
| Syntax                   | Clean, `end`-based blocks  | `{}` braces                |
| Pointer arithmetic       | Planned                    | Yes                        |
| Inline assembly          | Native (it IS assembly)    | `asm volatile()`           |
| SIMD support             | Planned first-class        | Intrinsics only            |
| Bootstrap                | Self-hosting (planned)     | Self-hosting (GCC)         |
| Undefined behavior       | None (explicit ops only)   | Many UB pitfalls           |
| Build system             | Single build.sh            | make / cmake               |
| Dependencies             | None                       | libc                       |
| Compilation speed        | Fast (single pass planned) | Slower (multi-pass)        |

---

## K vs C — Performance Philosophy

### Where C wins today
- Mature optimizer (50+ years of GCC/Clang work)
- Huge ecosystem of optimized libraries
- Auto-vectorization in `-O3`

### Where K will win
- No optimizer needed — you write optimal code directly
- Zero-cost abstractions by design, not by optimization pass
- Direct hardware access without compiler interference
- Hot paths written in inline asm with zero overhead
- No ABI surprises — you control the calling convention

---

## Assembly Strategy

K uses assembly in three ways:

**1. Codegen output**
The K compiler emits clean NASM x86-64 directly.
No bloated IR. No dead code. Every instruction intentional.

**2. Stdlib hot paths**
Performance-critical stdlib functions (string ops, memory copy, math)
will be written directly in NASM inside `.k` files using inline asm blocks.

**3. SIMD extensions (Stage 4+)**
```
# future K syntax
let result = simd_add(vec_a, vec_b)   # emits AVX2 directly
```

---

## Benchmarks (planned)

Once K is self-hosting we will benchmark against C on:

| Test                  | What it measures              |
|-----------------------|-------------------------------|
| fibonacci(40)         | raw integer recursion         |
| bubble sort 1M ints   | memory access + branching     |
| string search 100MB   | byte throughput               |
| matrix multiply 1024x | SIMD / cache performance      |
| file read/write 1GB   | syscall overhead              |

Goal: **match or beat gcc -O2 on all benchmarks.**

---

## Current Status

| Component        | Status         |
|------------------|----------------|
| Lexer            | Done (C)       |
| Parser           | Done (C)       |
| Codegen x86-64   | Done (C)       |
| `let` / math     | Done           |
| `if` / `while`   | Done           |
| `fn` / `return`  | Done           |
| `else` / `elif`  | In progress    |
| Strings          | Planned        |
| Arrays           | Planned        |
| Stdlib           | Planned        |
| Self-hosting     | Planned        |
| SIMD             | Planned        |

---

## The End Goal

```
# this K program compiles itself
# and the binary it produces beats gcc -O2
./k_runner compiler/runner.k myprogram.k
```

No C. No LLVM. No GCC.
Just K, asm, and raw metal.
