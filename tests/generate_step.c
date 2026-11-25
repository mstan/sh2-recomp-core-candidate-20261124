#include "boot_image.h"
#include "sh2_recomp/emit.h"

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    FILE *out = fopen(argv[1], "wb");
    if (!out)
        return 1;
    bool ok = sh2_emit_catalog(out, "fixture", boot_image, sizeof(boot_image), 0);
    return fclose(out) == 0 && ok ? 0 : 1;
}
