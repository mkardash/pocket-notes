/* Renders a comparison sheet of icon sets as they would look in the app (grayscale PNG).
 * Usage: preview <out.png> <icon-download-dir> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../third_party/stb/stb_image_write.h"
#include "../../build/icon_cp.h"

#define W 1264
#define H 1900
static unsigned char img[W * H];

typedef struct { unsigned char *data; stbtt_fontinfo info; } Font;

static Font load(const char *path) {
    Font f;
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "missing %s\n", path); exit(1); }
    fseek(fp, 0, SEEK_END); long n = ftell(fp); rewind(fp);
    f.data = malloc(n);
    if (fread(f.data, 1, n, fp) != (size_t)n) exit(1);
    fclose(fp);
    stbtt_InitFont(&f.info, f.data, stbtt_GetFontOffsetForIndex(f.data, 0));
    return f;
}

static void fill(int x, int y, int w, int h, unsigned char v) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++)
        if (i >= 0 && j >= 0 && i < W && j < H) img[j * W + i] = v;
}

static void frame(int x, int y, int w, int h, unsigned char v) {
    fill(x, y, w, 2, v); fill(x, y + h - 2, w, 2, v); fill(x, y, 2, h, v); fill(x + w - 2, y, 2, h, v);
}

/* Draws one glyph centred in a box, blending ink over what is there. */
static void glyph(Font *f, unsigned cp, int bx, int by, int bw, int bh, int px, unsigned char ink) {
    float s = stbtt_ScaleForPixelHeight(&f->info, (float)px);
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&f->info, cp, s, s, &x0, &y0, &x1, &y1);
    int gw, gh, xo, yo;
    unsigned char *b = stbtt_GetCodepointBitmap(&f->info, s, s, cp, &gw, &gh, &xo, &yo);
    int ox = bx + (bw - gw) / 2, oy = by + (bh - gh) / 2;
    for (int j = 0; j < gh; j++) for (int i = 0; i < gw; i++) {
        int a = b[j * gw + i], X = ox + i, Y = oy + j;
        if (!a || X < 0 || Y < 0 || X >= W || Y >= H) continue;
        img[Y * W + X] = (unsigned char)((img[Y * W + X] * (255 - a) + ink * a) / 255);
    }
    stbtt_FreeBitmap(b, NULL);
}

static void text(Font *f, const char *s, int x, int y, int px, unsigned char ink) {
    float sc = stbtt_ScaleForPixelHeight(&f->info, (float)px);
    int asc; stbtt_GetFontVMetrics(&f->info, &asc, NULL, NULL);
    int base = y + (int)(asc * sc);
    while (*s) {
        unsigned c = (unsigned char)*s++;
        if (c >= 0xC0) { c = ((c & 31) << 6) | (*s++ & 63); }
        int adv, lsb; stbtt_GetCodepointHMetrics(&f->info, c, &adv, &lsb);
        int gw, gh, xo, yo;
        unsigned char *b = stbtt_GetCodepointBitmap(&f->info, sc, sc, c, &gw, &gh, &xo, &yo);
        for (int j = 0; j < gh; j++) for (int i = 0; i < gw; i++) {
            int a = b[j * gw + i], X = x + xo + i, Y = base + yo + j;
            if (a && X >= 0 && Y >= 0 && X < W && Y < H) img[Y * W + X] = (unsigned char)((img[Y * W + X] * (255 - a) + ink * a) / 255);
        }
        stbtt_FreeBitmap(b, NULL);
        x += (int)(adv * sc);
    }
}

static int idx(const char *id) { for (int i = 0; i < ICON_COUNT; i++) if (!strcmp(icon_ids[i], id)) return i; return 0; }

int main(int argc, char **argv) {
    char p[1024];
    const char *d = argv[2];
    Font label = load("/System/Library/Fonts/Supplemental/Arial.ttf");
    Font bold = load("/System/Library/Fonts/Supplemental/Arial Bold.ttf");
    Font serif = load("/System/Library/Fonts/Supplemental/Georgia.ttf");
    struct { const char *name; const char *file; const unsigned *cp; int scale; } sets[4];
    char f0[512], f1[512], f2[512], f3[512];
    snprintf(f0, sizeof f0, "%s/phosphor/package/src/regular/Phosphor.ttf", d);
    snprintf(f1, sizeof f1, "%s/phosphor/package/src/bold/Phosphor-Bold.ttf", d);
    snprintf(f2, sizeof f2, "%s/lucide/package/font/lucide.ttf", d);
    snprintf(f3, sizeof f3, "%s/MaterialIconsRound-Regular.otf", d);
    sets[0].name = "A. Phosphor (тонкі, округлі)"; sets[0].file = f0; sets[0].cp = phosphor_cp; sets[0].scale = 100;
    sets[1].name = "B. Phosphor Bold (жирніші)"; sets[1].file = f1; sets[1].cp = phosphor_cp; sets[1].scale = 100;
    sets[2].name = "C. Lucide (лінійні, м'які)"; sets[2].file = f2; sets[2].cp = lucide_cp; sets[2].scale = 90;
    sets[3].name = "D. Material Round (заповнені)"; sets[3].file = f3; sets[3].cp = material_cp; sets[3].scale = 100;
    memset(img, 255, sizeof img);
    text(&bold, "Варіанти іконок", 40, 24, 46, 0);
    int y = 100;
    for (int s = 0; s < 4; s++) {
        Font f = load(sets[s].file);
        text(&bold, sets[s].name, 40, y, 36, 0);
        y += 52;
        /* all icons in a row */
        int n = ICON_COUNT, cell = (W - 40) / n;
        for (int i = 0; i < n; i++) glyph(&f, sets[s].cp[i], 20 + i * cell, y, cell, 64, 48 * sets[s].scale / 100, 0);
        y += 78;
        /* in context: a header bar ... */
        fill(20, y, W - 40, 2, 0);
        int hh = 110;
        glyph(&f, sets[s].cp[idx("BACK")], 20, y, hh, hh, 64 * sets[s].scale / 100, 0);
        text(&bold, "Сцена 1", 20 + hh + 6, y + 34, 38, 0);
        glyph(&f, sets[s].cp[idx("BULB")], W - 20 - 3 * hh, y, hh, hh, 64 * sets[s].scale / 100, 0);
        glyph(&f, sets[s].cp[idx("TEXT")], W - 20 - 2 * hh, y, hh, hh, 64 * sets[s].scale / 100, 0);
        glyph(&f, sets[s].cp[idx("KEYBOARD")], W - 20 - hh, y, hh, hh, 64 * sets[s].scale / 100, 0);
        y += hh;
        fill(20, y - 3, W - 40, 3, 0);
        /* ... an outline row ... */
        glyph(&f, sets[s].cp[idx("CHEVRON_DOWN")], 30, y, 50, 80, 36 * sets[s].scale / 100, 80);
        glyph(&f, sets[s].cp[idx("FOLDER")], 80, y, 70, 80, 52 * sets[s].scale / 100, 0);
        text(&bold, "Розділ 1", 165, y + 22, 36, 0);
        glyph(&f, sets[s].cp[idx("DOC")], 520, y, 70, 80, 52 * sets[s].scale / 100, 60);
        text(&label, "Сцена 1", 600, y + 22, 36, 0);
        glyph(&f, sets[s].cp[idx("DOTS")], W - 120, y, 80, 80, 52 * sets[s].scale / 100, 80);
        y += 84;
        /* ... and keyboard keys */
        const char *keys[] = { "LEFT", "RIGHT", "UP", "DOWN", "SHIFT", "BACKSPACE", "ENTER", "HIDE" };
        int kw = (W - 40) / 8;
        for (int k = 0; k < 8; k++) {
            int kx = 20 + k * kw;
            fill(kx + 4, y + 4, kw - 8, 92, 0xDD);
            frame(kx + 4, y + 4, kw - 8, 92, 0x55);
            glyph(&f, sets[s].cp[idx(keys[k])], kx + 4, y + 4, kw - 8, 92, 56 * sets[s].scale / 100, 0);
        }
        y += 120;
        free(f.data);
    }
    (void)serif;
    stbi_write_png(argv[1], W, H, 1, img, W);
    printf("wrote %s (y=%d)\n", argv[1], y);
    return 0;
}
