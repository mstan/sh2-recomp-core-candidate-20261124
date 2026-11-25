#include "sh2_recomp/function.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static bool address(const char *text, uint32_t *out)
{
    char *end;
    errno = 0;
    const unsigned long long value = strtoull(text, &end, 0);
    if (errno || end == text || *end || value > UINT32_MAX)
        return false;
    *out = (uint32_t)value;
    return true;
}

static int compare_targets(const void *left, const void *right)
{
    const Sh2ObservedTarget *a = left, *b = right;
    if (a->source != b->source)
        return a->source < b->source ? -1 : 1;
    return (a->target > b->target) - (a->target < b->target);
}

static bool load_targets(const char *path, Sh2FunctionImage *image)
{
    FILE *input = fopen(path, "r");
    if (!input)
        return false;
    Sh2ObservedTarget *targets = NULL;
    size_t used = 0, capacity = 0;
    int fields;
    Sh2ObservedTarget row;
    while ((fields = fscanf(input, "%" SCNx32 " %" SCNx32, &row.source, &row.target)) == 2) {
        if (used == capacity) {
            if (capacity > SIZE_MAX / sizeof(*targets) / 2)
                break;
            size_t next = capacity ? capacity * 2 : 128;
            Sh2ObservedTarget *entries = realloc(targets, next * sizeof(*targets));
            if (!entries)
                break;
            targets = entries;
            capacity = next;
        }
        row.source &= ~image->alias_mask;
        targets[used++] = row;
    }
    const bool ok = fields == EOF && feof(input) && !ferror(input);
    fclose(input);
    if (!ok) {
        free(targets);
        return false;
    }
    if (used)
        qsort(targets, used, sizeof(*targets), compare_targets);
    image->targets = targets;
    image->target_count = used;
    return true;
}

int main(int argc, char **argv)
{
    Sh2FunctionImage image = {0};
    if (argc < 6 || !address(argv[4], &image.base)) {
        fputs("usage: sh2-function-aot <image.bin> <new-output.c> <name> <base> "
              "[--alias=<mask>] [--targets=<hex-pairs.txt>] [--executed=<bitmap.bin>] "
              "[--follow-static] [--local-handoffs=<hex-pairs.txt>] <entry> [entry ...]\n", stderr);
        return 2;
    }
    const char *target_path = NULL;
    const char *bitmap_path = NULL;
    const char *handoff_path = NULL;
    int first_entry = 5;
    while (first_entry < argc) {
        const char *option = argv[first_entry];
        if (strncmp(option, "--alias=", 8) == 0) {
            if (!address(option + 8, &image.alias_mask))
                return 2;
        } else if (strncmp(option, "--targets=", 10) == 0) {
            target_path = option + 10;
        } else if (strncmp(option, "--executed=", 11) == 0) {
            bitmap_path = option + 11;
        } else if (strcmp(option, "--follow-static") == 0) {
            image.follow_static = true;
        } else if (strncmp(option, "--local-handoffs=", 17) == 0) {
            handoff_path = option + 17;
        } else {
            break;
        }
        ++first_entry;
    }
    if (first_entry == argc || (image.follow_static && !bitmap_path))
        return 2;
    image.entry_count = (size_t)(argc - first_entry);
    uint32_t *entries = malloc(image.entry_count * sizeof(*entries));
    if (!entries)
        return 1;
    for (size_t i = 0; i < image.entry_count; ++i) {
        if (!address(argv[first_entry + i], &entries[i])) {
            fprintf(stderr, "Invalid function entry: %s\n", argv[first_entry + i]);
            free(entries);
            return 2;
        }
    }
    image.entries = entries;
    if (target_path && !load_targets(target_path, &image)) {
        fprintf(stderr, "Could not read indirect targets: %s\n", target_path);
        free(entries);
        return 1;
    }

    FILE *input = fopen(argv[1], "rb");
    const long size = input && fseek(input, 0, SEEK_END) == 0 ? ftell(input) : -1;
    uint8_t *bytes = size > 0 ? malloc((size_t)size) : NULL;
    const bool loaded = bytes && fseek(input, 0, SEEK_SET) == 0 &&
                        fread(bytes, 1, (size_t)size, input) == (size_t)size;
    if (input)
        fclose(input);
    if (!loaded) {
        fprintf(stderr, "Could not read SH-2 image: %s\n", argv[1]);
        free(bytes);
        free((void *)image.targets);
        free(entries);
        return 1;
    }
    image.bytes = bytes;
    image.size = (size_t)size;
    uint8_t *bitmap = NULL;
    if (bitmap_path) {
        const size_t bitmap_size = (image.size + 15) / 16;
        bitmap = malloc(bitmap_size);
        FILE *file = fopen(bitmap_path, "rb");
        const bool read_bitmap = file && bitmap && fread(bitmap, 1, bitmap_size, file) == bitmap_size &&
                                 fgetc(file) == EOF && feof(file) && !ferror(file);
        if (file)
            fclose(file);
        if (!read_bitmap) {
            fprintf(stderr, "Expected a %zu-byte executed-word bitmap: %s\n", bitmap_size, bitmap_path);
            free(bitmap);
            free(bytes);
            free((void *)image.targets);
            free(entries);
            return 1;
        }
        image.observed = bitmap;
        image.observed_only = !image.follow_static;
    }

    Sh2FunctionImage handoffs = {.alias_mask = image.alias_mask};
    const bool loaded_handoffs = !handoff_path || load_targets(handoff_path, &handoffs);
    if (!loaded_handoffs)
        fprintf(stderr, "Could not read local call handoffs: %s\n", handoff_path);
    image.local_handoffs = handoffs.targets;
    image.local_handoff_count = handoffs.target_count;
    FILE *output = loaded_handoffs ? fopen(argv[2], "wbx") : NULL;
    bool ok = false;
    if (output) {
        ok = sh2_emit_functions(output, stderr, argv[3], &image);
        ok = fclose(output) == 0 && ok;
        if (!ok)
            remove(argv[2]);
    }
    if (!ok)
        fprintf(stderr, "Could not emit native SH-2 functions: %s\n", argv[2]);
    free(bytes);
    free(bitmap);
    free((void *)image.targets);
    free((void *)handoffs.targets);
    free(entries);
    return ok ? 0 : 1;
}
