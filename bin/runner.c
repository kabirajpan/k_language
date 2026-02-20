#include <stdio.h>
#include <stdlib.h>
#include "../include/main.h"

int main(int argc, char **argv) {
    const char *input_file = "src/main.k";
    if (argc > 1) input_file = argv[1];

    // read source file
    FILE *f = fopen(input_file, "r");
    if (!f) {
        perror("Failed to open input file");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *src = malloc(len + 1);
    fread(src, 1, len, f);
    src[len] = 0;
    fclose(f);

    // pipeline
    printf("[1] Tokenizing...\n");
    tokenize(src);

    printf("[2] Parsing...\n");
    Node *ast = parse();

    printf("[3] Generating assembly...\n");
    generate(ast, "output.s");

    printf("[4] Assembling...\n");
    system("nasm -f elf64 output.s -o output.o");

    printf("[5] Linking...\n");
    system("gcc -no-pie output.o -o output_exe");

    printf("[6] Running...\n");
    printf("─────────────────\n");
    system("./output_exe");
    printf("─────────────────\n");

    free(src);
    return 0;
}
