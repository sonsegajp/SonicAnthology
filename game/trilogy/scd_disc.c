/*
 * scd_disc.c — the player's Sonic CD disc: its CUE sheet, the ISO 9660 file
 * system of the MODE1/2352 data track, and where each CD-DA track starts.
 *
 * Sonic CD is read in place from the user's own dump. The CUE is found
 * through TRILOGY_SCD_CUE, then scd-disc.txt beside the executable (one
 * line: the CUE's path, written by AnthologyBuilder), then any .cue in a
 * "roms" folder beside it whose data track's volume is SONIC_CD.
 */
#include "scd.h"
#include "scd_internal.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAW 2352
#define USER 2048

static ScdTrack s_track[100];
static int s_tracks, s_state;              /* s_state: 0 not tried, 1 open, -1 none */
static FILE *s_data;
static long s_data_start;
static struct { char name[32]; uint8_t *data; uint32_t size; } s_cache[24];

const ScdTrack *scd_track_info(int n)
{
    for (int i = 0; i < s_tracks; i++)
        if (s_track[i].number == n) return &s_track[i];
    return NULL;
}

static int parse_cue(const char *cue)
{
    FILE *f = fopen(cue, "r");
    if (!f) return 0;
    char dir[1024], file[1024] = "", line[1200];
    snprintf(dir, sizeof dir, "%s", cue);
    char *slash = strrchr(dir, '/'), *bs = strrchr(dir, '\\');
    if (bs > slash) slash = bs;
    if (slash) slash[1] = 0; else dir[0] = 0;
    s_tracks = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        int n, mm, ss, ff;
        char kind[32];
        if (!strncmp(p, "FILE \"", 6)) {
            char *q = strchr(p + 6, '"');
            if (q) { *q = 0; snprintf(file, sizeof file, "%s%s", dir, p + 6); }
        } else if (sscanf(p, "TRACK %d %31s", &n, kind) == 2 && s_tracks < 100) {
            ScdTrack *t = &s_track[s_tracks++];
            memset(t, 0, sizeof *t);
            t->number = n;
            t->audio = !strcmp(kind, "AUDIO");
            snprintf(t->path, sizeof t->path, "%s", file);
        } else if (sscanf(p, "INDEX 01 %d:%d:%d", &mm, &ss, &ff) == 3 && s_tracks) {
            s_track[s_tracks - 1].start = ((long)(mm * 60 + ss) * 75 + ff) * RAW;
        }
    }
    fclose(f);
    for (int i = 0; i < s_tracks; i++) {                 /* each track's length, from its file */
        FILE *t = fopen(s_track[i].path, "rb");
        if (!t) continue;
        fseek(t, 0, SEEK_END);
        long size = ftell(t);
        fclose(t);
        long end = size;                                 /* (a shared file ends at the next track) */
        for (int j = i + 1; j < s_tracks; j++)
            if (!strcmp(s_track[j].path, s_track[i].path)) { end = s_track[j].start; break; }
        s_track[i].size = end > s_track[i].start ? end - s_track[i].start : 0;
    }
    return s_tracks > 0;
}

static int read_sector(long lba, uint8_t *out)
{
    if (fseek(s_data, s_data_start + lba * RAW + 16, SEEK_SET)) return 0;
    return fread(out, 1, USER, s_data) == USER;
}

static int open_data(void)
{
    for (int i = 0; i < s_tracks; i++)
        if (!s_track[i].audio) {
            s_data = fopen(s_track[i].path, "rb");
            s_data_start = s_track[i].start;
            break;
        }
    uint8_t pvd[USER];
    if (!s_data || !read_sector(16, pvd) || memcmp(pvd + 1, "CD001", 5) || memcmp(pvd + 40, "SONIC_CD", 8)) {
        if (s_data) fclose(s_data);
        s_data = NULL;
        return 0;
    }
    return 1;
}

static int try_cue(const char *cue)
{
    if (!cue || !*cue || !parse_cue(cue) || !open_data()) return 0;
    fprintf(stderr, "[scd] disc: %s (%d tracks)\n", cue, s_tracks);
    return 1;
}

int scd_disc_open(void)
{
    if (s_state) return s_state > 0;
    s_state = -1;
    if (try_cue(getenv("TRILOGY_SCD_CUE"))) return s_state = 1;
    char *base = SDL_GetBasePath(), path[1024], line[1024];
    snprintf(path, sizeof path, "%sscd-disc.txt", base ? base : "");
    FILE *f = fopen(path, "r");
    if (f) {
        if (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (try_cue(line)) { fclose(f); SDL_free(base); return s_state = 1; }
        }
        fclose(f);
    }
    snprintf(path, sizeof path, "%sroms", base ? base : "");
    SDL_free(base);
#ifdef _WIN32
    char pattern[1100];
    snprintf(pattern, sizeof pattern, "%s\\*.cue", path);
    extern int scd_find_first(const char *pattern, char *out, int cap, int index);
    for (int i = 0; scd_find_first(pattern, line, sizeof line, i); i++) {
        char full[2100];
        snprintf(full, sizeof full, "%s\\%s", path, line);
        if (try_cue(full)) return s_state = 1;
    }
#endif
    fprintf(stderr, "[scd] no Sonic CD disc found\n");
    return 0;
}

#ifdef _WIN32
#include <windows.h>
int scd_find_first(const char *pattern, char *out, int cap, int index)
{
    WIN32_FIND_DATAA d;
    HANDLE h = FindFirstFileA(pattern, &d);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int i = 0, ok = 0;
    do {
        if (i++ == index) { snprintf(out, (size_t)cap, "%s", d.cFileName); ok = 1; break; }
    } while (FindNextFileA(h, &d));
    FindClose(h);
    return ok;
}
#endif

/* the root directory's record for `name` (ISO names carry ";1") */
static int find_file(const char *name, long *lba, uint32_t *size)
{
    uint8_t pvd[USER], sec[USER];
    if (!read_sector(16, pvd)) return 0;
    const uint8_t *root = pvd + 156;
    long rl = (long)(root[2] | root[3] << 8 | root[4] << 16 | (uint32_t)root[5] << 24);
    uint32_t rs = root[10] | root[11] << 8 | root[12] << 16 | (uint32_t)root[13] << 24;
    size_t len = strlen(name);
    for (uint32_t off = 0; off < rs; off += USER) {
        if (!read_sector(rl + (long)(off / USER), sec)) return 0;
        for (int p = 0; p < USER && sec[p];) {
            const uint8_t *r = sec + p;
            int nl = r[32];
            if (nl >= (int)len && !memcmp(r + 33, name, len) && (nl == (int)len || r[33 + len] == ';')) {
                *lba = (long)(r[2] | r[3] << 8 | r[4] << 16 | (uint32_t)r[5] << 24);
                *size = r[10] | r[11] << 8 | r[12] << 16 | (uint32_t)r[13] << 24;
                return 1;
            }
            p += r[0];
        }
    }
    return 0;
}

const uint8_t *scd_disc_file(const char *name, uint32_t *size)
{
    if (!scd_disc_open()) return NULL;
    int slot = -1;
    for (int i = 0; i < (int)(sizeof s_cache / sizeof s_cache[0]); i++) {
        if (s_cache[i].data && !strcmp(s_cache[i].name, name)) {
            if (size) *size = s_cache[i].size;
            return s_cache[i].data;
        }
        if (!s_cache[i].data && slot < 0) slot = i;
    }
    long lba;
    uint32_t n;
    if (slot < 0 || !find_file(name, &lba, &n) || n > 0x2000000u) return NULL;
    uint8_t *buf = (uint8_t *)malloc(n + USER);
    if (!buf) return NULL;
    for (uint32_t off = 0; off < n; off += USER)
        if (!read_sector(lba + (long)(off / USER), buf + off)) { free(buf); return NULL; }
    snprintf(s_cache[slot].name, sizeof s_cache[slot].name, "%s", name);
    s_cache[slot].data = buf;
    s_cache[slot].size = n;
    if (size) *size = n;
    return buf;
}
