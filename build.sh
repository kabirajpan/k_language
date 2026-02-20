#!/bin/bash
set -e

mkdir -p build

echo "[1/3] Assembling asm hot path..."
nasm -f elf64 src/codegen_asm.asm -o build/codegen_asm.o

echo "[2/3] Compiling C sources..."
gcc -c -o build/lexer.o src/lexer.c -Iinclude
gcc -c -o build/parser.o src/parser.c -Iinclude
gcc -c -o build/codegen.o src/codegen.c -Iinclude
gcc -c -o build/runner.o bin/runner.c -Iinclude

echo "[3/3] Linking..."
gcc -no-pie -o build/k_runner \
  build/runner.o \
  build/lexer.o \
  build/parser.o \
  build/codegen.o \
  build/codegen_asm.o

echo "Build OK -> ./build/k_runner <file.k>"
