# K Language Roadmap

---

## What We Have
- `let` variables
- math `+ - * /`
- comparisons `> < ==`
- `if` / `while` / `end`
- `fn` / `return`
- `print()`

---

## Stage 1 — Extend C Compiler
- [ ] `else` / `elif`
- [ ] strings `"hello"`
- [ ] arrays `let arr = [1, 2, 3]`
- [ ] nested fn calls `print(add(1, 2))`
- [ ] `for` loop
- [ ] comparisons `!=` `>=` `<=`

## Stage 2 — K Standard Library (written in K)
- [ ] string ops (length, concat, slice)
- [ ] file read / write
- [ ] array ops (push, pop, get, set)
- [ ] basic I/O

## Stage 3 — K Compiler in K
- [ ] lexer.k
- [ ] parser.k
- [ ] codegen.k
- [ ] runner.k

## Stage 4 — Bootstrap
- [ ] compile K compiler using C compiler
- [ ] K compiler compiles itself
- [ ] verify output matches
- [ ] drop C compiler forever

---

## Bootstrap Folder Structure
> Current + everything needed to reach self-hosting

```
k_language/
  build.sh
  ROADMAP.md
  .gitignore
  include/
    main.h                  # shared structs and types
  src/
    lexer.c                 # tokenizer
    parser.c                # AST builder
    codegen.c               # x86-64 asm emitter
  bin/
    runner.c                # entry point
  tests/
    test.k
    fn_test.k
    while_test.k
    string_test.k
    array_test.k
    if_test.k
  stdlib/
    string.k                # string ops
    array.k                 # array ops
    io.k                    # file read/write
    math.k                  # extra math helpers
  compiler/
    lexer.k                 # K compiler lexer written in K
    parser.k                # K compiler parser written in K
    codegen.k               # K compiler codegen written in K
    runner.k                # K compiler entry written in K
  build/
    k_runner                # compiled binary        [gitignored]
    output.s                # generated asm          [gitignored]
    output.o                # object file            [gitignored]
    output_exe              # final exe              [gitignored]
```

---

## Final Completed Folder Structure
> After bootstrap — K is fully self-hosted, C compiler dropped

```
k_language/
  build.sh
  ROADMAP.md
  .gitignore
  compiler/
    lexer.k                 # tokenizer
    parser.k                # AST builder
    codegen.k               # x86-64 asm emitter
    runner.k                # entry point
  stdlib/
    string.k                # string ops
    array.k                 # array ops
    io.k                    # file read/write
    math.k                  # math helpers
    os.k                    # syscall wrappers
    fmt.k                   # formatting and print helpers
  tools/
    repl.k                  # interactive K shell
    fmt.k                   # code formatter
    test.k                  # test runner
    lsp.k                   # language server (future)
  programs/
    hello.k                 # example programs written in K
    fib.k
    sort.k
  tests/
    test.k
    fn_test.k
    while_test.k
    string_test.k
    array_test.k
    stdlib_test.k
    compiler_test.k         # tests the K compiler itself
  build/
    k_runner                # self-compiled K binary  [gitignored]
```

---

## .gitignore
```
build/
output.s
output.o
output_exe
```
