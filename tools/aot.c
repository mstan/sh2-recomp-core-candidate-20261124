#include "sh2_recomp/emit.h"

#include <errno.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) {
        fputs("usage: sh2-aot <input.bin> <output.c> <catalog-name> [base]\n", stderr);
        return 2;
    }
    unsigned long long base = 0;
    if (argc == 5) {
        char *end;
        errno = 0;
        base = strtoull(argv[4], &end, 0);
        if (errno || end == argv[4] || *end || base > UINT32_MAX) {
            fprintf(stderr, "Invalid catalog base: %s\n", argv[4]);
            return 2;
        }
    }

    FILE *input = fopen(argv[1], "rb");
    if (!input) {
        perror(argv[1]);
        return 1;
    }
    long size = fseek(input, 0, SEEK_END) == 0 ? ftell(input) : -1;
    uint8_t *data = size > 0 ? malloc((size_t)size) : NULL;
    if (!data || fseek(input, 0, SEEK_SET) != 0 ||
        fread(data, 1, (size_t)size, input) != (size_t)size) {
        fprintf(stderr, "Could not read SH-2 image: %s\n", argv[1]);
        free(data);
        fclose(input);
        return 1;
    }
    fclose(input);

    FILE *output = fopen(argv[2], "wb");
    if (!output) {
        perror(argv[2]);
        free(data);
        return 1;
    }
    bool ok = sh2_emit_catalog(output, argv[3], data, (size_t)size, (uint32_t)base);
    if (fclose(output) != 0)
        ok = false;
    free(data);
    if (!ok) {
        fprintf(stderr, "Could not generate SH-2 catalog: %s\n", argv[2]);
        return 1;
    }
    return 0;
}
