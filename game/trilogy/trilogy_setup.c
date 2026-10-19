/*
 * trilogy_setup.c — what a plain start needs: the S3K cart (sonic3k.bin) and
 * the trilogy bank built from the player's Sonic 1 and Sonic 2 (trilogy.bin),
 * both beside the executable. Missing either, AnthologyBuilder (our setup:
 * tools/trilogy/builder) is started to make them, and the game closes.
 */
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

static char s_dir[1024];

static const char *beside(const char *name)
{
    static char path[2][1100];
    static int k;
    k ^= 1;
    snprintf(path[k], sizeof path[k], "%s%s", s_dir, name);
    return path[k];
}

static int exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

const char *tri_resolve_rom(void)
{
#ifdef _WIN32
    GetModuleFileNameA(NULL, s_dir, sizeof s_dir);
    char *slash = strrchr(s_dir, '\\');
    if (slash) slash[1] = 0;
#endif
    static char rom[1100];
    snprintf(rom, sizeof rom, "%s", beside("sonic3k.bin"));
    if (exists(rom) && exists(beside("trilogy.bin")))
        return rom;
#ifdef _WIN32
    const char *builder = beside("AnthologyBuilder.exe");
    if (!exists(builder)) builder = beside("TrilogyBuilder.exe");   /* (its name before the rename) */
    if (exists(builder)) {
        ShellExecuteA(NULL, "open", builder, NULL, s_dir, SW_SHOWNORMAL);
    } else {
        MessageBoxA(NULL,
            "Sonic Anthology is built from your own Sonic 1, Sonic 2 and Sonic 3 & Knuckles ROMs.\n\n"
            "Run AnthologyBuilder.exe (beside this game) to set it up.",
            "Sonic Anthology", MB_OK | MB_ICONINFORMATION);
    }
#else
    fprintf(stderr, "Sonic Anthology: sonic3k.bin and trilogy.bin are needed beside the game; "
                    "run the builder (tools/trilogy/builder) to make them.\n");
#endif
    return NULL;
}
