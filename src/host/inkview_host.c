/* Host implementation of the inkview subset: draws into an 8-bit grayscale framebuffer
 * with stb_truetype, so the simulator can save screens as PNG files. */
#include "inkview.h"
#include "sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../third_party/stb/stb_image_write.h"

#define W 1264
#define H 1680

static unsigned char fb[W * H];
static int clip_x = 0, clip_y = 0, clip_w = W, clip_h = H;
static const ifont *cur_font = NULL;
static int cur_color = BLACK;
int sim_update_count = 0;
int sim_closed = 0;

/* ---- framebuffer ---- */

static unsigned char gray(int color) {
    int r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;
    return (unsigned char)((r * 299 + g * 587 + b * 114) / 1000);
}

static void put(int x, int y, unsigned char v) {
    if (x < clip_x || y < clip_y || x >= clip_x + clip_w || y >= clip_y + clip_h) return;
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    fb[y * W + x] = v;
}

int ScreenWidth() { return W; }
int ScreenHeight() { return H; }
void ClearScreen() { memset(fb, 255, sizeof fb); }
void SetClip(int x, int y, int w, int h) { clip_x = x; clip_y = y; clip_w = w; clip_h = h; }

void FillArea(int x, int y, int w, int h, int color) {
    unsigned char v = gray(color);
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) put(i, j, v);
}

void DrawRect(int x, int y, int w, int h, int color) {
    FillArea(x, y, w, 1, color);
    FillArea(x, y + h - 1, w, 1, color);
    FillArea(x, y, 1, h, color);
    FillArea(x + w - 1, y, 1, h, color);
}

void DrawLine(int x1, int y1, int x2, int y2, int color) {
    unsigned char v = gray(color);
    int dx = abs(x2 - x1), dy = -abs(y2 - y1), sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1, err = dx + dy;
    for (;;) {
        put(x1, y1, v);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void InvertArea(int x, int y, int w, int h) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (i >= 0 && j >= 0 && i < W && j < H) fb[j * W + i] = 255 - fb[j * W + i];
}

/* ---- fonts ---- */

typedef struct { unsigned char *data; stbtt_fontinfo info; } FontFile;
typedef struct { unsigned cp; int w, h, xoff, yoff, adv; unsigned char *bmp; } Glyph;
typedef struct { FontFile *file; float scale; int ascent; Glyph *cache; int cap, count; } FontData;

static FontFile *load_font_file(const char *path) {
    static struct { char path[256]; FontFile *f; } loaded[8];
    for (int i = 0; i < 8; i++) if (loaded[i].f && !strcmp(loaded[i].path, path)) return loaded[i].f;
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "sim: missing font %s\n", path); exit(1); }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    rewind(fp);
    FontFile *f = calloc(1, sizeof *f);
    f->data = malloc(n);
    if (fread(f->data, 1, n, fp) != (size_t)n) exit(1);
    fclose(fp);
    stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0));
    for (int i = 0; i < 8; i++) if (!loaded[i].f) { snprintf(loaded[i].path, 256, "%s", path); loaded[i].f = f; break; }
    return f;
}

static const char *font_path_for(const char *name) {
    int bold = strstr(name, "Bold") != NULL, italic = strstr(name, "Italic") != NULL;
    if (strstr(name, "Mono")) return "/System/Library/Fonts/Supplemental/Courier New.ttf";
    if (strstr(name, "Serif") || strstr(name, "Georgia")) {
        if (bold && italic) return "/System/Library/Fonts/Supplemental/Georgia Bold Italic.ttf";
        if (bold) return "/System/Library/Fonts/Supplemental/Georgia Bold.ttf";
        if (italic) return "/System/Library/Fonts/Supplemental/Georgia Italic.ttf";
        return "/System/Library/Fonts/Supplemental/Georgia.ttf";
    }
    if (bold && italic) return "/System/Library/Fonts/Supplemental/Arial Bold Italic.ttf";
    if (bold) return "/System/Library/Fonts/Supplemental/Arial Bold.ttf";
    if (italic) return "/System/Library/Fonts/Supplemental/Arial Italic.ttf";
    return "/System/Library/Fonts/Supplemental/Arial.ttf";
}

static char *sim_fonts[] = { "LiberationSans", "LiberationSans-Bold", "LiberationSans-Italic", "LiberationSerif",
                             "Georgia", "Georgia-Bold", "Georgia-Italic", "Georgia-BoldItalic", "LiberationMono", NULL };
char **EnumFonts() { return sim_fonts; }

char *iv_get_default_font(FONT_TYPE t) {
    switch (t) {
    case FONT_BOLD: return "LiberationSans-Bold";
    case FONT_ITALIC: return "LiberationSans-Italic";
    case FONT_BOLDITALIC: return "LiberationSans-BoldItalic";
    case FONT_MONO: return "LiberationMono";
    default: return "LiberationSans";
    }
}

ifont *OpenFont(const char *name, int size, int aa) {
    ifont *f = calloc(1, sizeof *f);
    FontData *d = calloc(1, sizeof *d);
    d->file = load_font_file(font_path_for(name));
    d->scale = stbtt_ScaleForMappingEmToPixels(&d->file->info, (float)size);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&d->file->info, &asc, &desc, &gap);
    d->ascent = (int)(asc * d->scale + 0.5f);
    f->name = strdup(name);
    f->size = size;
    f->aa = (unsigned char)aa;
    f->height = (int)((asc - desc) * d->scale + 0.5f);
    f->linespacing = (int)((asc - desc + gap) * d->scale + 0.5f);
    f->baseline = d->ascent;
    f->fdata = d;
    return f;
}

void CloseFont(ifont *f) {
    if (!f) return;
    FontData *d = f->fdata;
    for (int i = 0; i < d->count; i++) free(d->cache[i].bmp);
    free(d->cache);
    free(d);
    free(f->name);
    free(f);
}

void SetFont(const ifont *font, int color) { cur_font = font; cur_color = color; }

static Glyph *glyph(FontData *d, unsigned cp) {
    for (int i = 0; i < d->count; i++) if (d->cache[i].cp == cp) return &d->cache[i];
    if (d->count == d->cap) { d->cap = d->cap ? d->cap * 2 : 64; d->cache = realloc(d->cache, sizeof(Glyph) * d->cap); }
    Glyph *g = &d->cache[d->count++];
    memset(g, 0, sizeof *g);
    g->cp = cp;
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&d->file->info, (int)cp, &adv, &lsb);
    g->adv = (int)(adv * d->scale + 0.5f);
    g->bmp = stbtt_GetCodepointBitmap(&d->file->info, 0, d->scale, (int)cp, &g->w, &g->h, &g->xoff, &g->yoff);
    return g;
}

static unsigned next_cp(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    unsigned c = *p++;
    if (c >= 0xF0) { c = ((c & 7) << 18) | ((p[0] & 63) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); p += 3; }
    else if (c >= 0xE0) { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
    else if (c >= 0xC0) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
    *s = (const char *)p;
    return c;
}

int StringWidth(const char *s) {
    if (!cur_font) return 0;
    FontData *d = cur_font->fdata;
    int w = 0;
    while (*s) w += glyph(d, next_cp(&s))->adv;
    return w;
}

void DrawString(int x, int y, const char *s) {
    if (!cur_font) return;
    FontData *d = cur_font->fdata;
    unsigned char ink = gray(cur_color);
    int base = y + d->ascent;
    while (*s) {
        Glyph *g = glyph(d, next_cp(&s));
        for (int j = 0; j < g->h; j++)
            for (int i = 0; i < g->w; i++) {
                int a = g->bmp[j * g->w + i];
                if (!a) continue;
                int px = x + g->xoff + i, py = base + g->yoff + j;
                if (px < 0 || py < 0 || px >= W || py >= H) continue;
                if (px < clip_x || py < clip_y || px >= clip_x + clip_w || py >= clip_y + clip_h) continue;
                unsigned char bg = fb[py * W + px];
                fb[py * W + px] = (unsigned char)((bg * (255 - a) + ink * a) / 255);
            }
        x += g->adv;
    }
}

/* ---- screen updates (counted only) ---- */
void FullUpdate() { sim_update_count++; }
void SoftUpdate() { sim_update_count++; }
void PartialUpdate(int x, int y, int w, int h) { sim_update_count++; }
void PartialUpdateBW(int x, int y, int w, int h) { sim_update_count++; }
void DynamicUpdate(int x, int y, int w, int h) { sim_update_count++; }
void DynamicUpdateBW(int x, int y, int w, int h) { sim_update_count++; }

/* ---- timers (virtual clock driven by the simulator) ---- */
typedef struct { iv_timerproc proc; long long due; } Timer;
static Timer timers[32];
static long long now_ms = 0;

static void set_timer(iv_timerproc p, int ms) {
    for (int i = 0; i < 32; i++) if (timers[i].proc == p) { timers[i].due = now_ms + ms; return; }
    for (int i = 0; i < 32; i++) if (!timers[i].proc) { timers[i].proc = p; timers[i].due = now_ms + ms; return; }
}
void SetHardTimer(const char *name, iv_timerproc p, int ms) { set_timer(p, ms); }
void SetWeakTimer(const char *name, iv_timerproc p, int ms) { set_timer(p, ms); }
void ClearTimer(iv_timerproc p) { for (int i = 0; i < 32; i++) if (timers[i].proc == p) timers[i].proc = NULL; }

void sim_advance(int ms) {
    long long end = now_ms + ms;
    for (;;) {
        int best = -1;
        for (int i = 0; i < 32; i++)
            if (timers[i].proc && timers[i].due <= end && (best < 0 || timers[i].due < timers[best].due)) best = i;
        if (best < 0) break;
        now_ms = timers[best].due;
        iv_timerproc p = timers[best].proc;
        timers[best].proc = NULL; /* one-shot, like InkView */
        p();
    }
    now_ms = end;
}

long long sim_now(void) { return now_ms; }

/* ---- misc ---- */
static const char *lang = "uk";
void sim_set_lang(const char *l) { lang = l; }
const char *currentLang() { return lang; }
void SetPanelType(int type) {}
void SetOrientation(int n) {}
int GetBatteryPower() { return 87; }
void CloseApp() { sim_closed = 1; }
void InkViewMain(iv_handler h) { (void)h; }

int sim_save_png(const char *path) {
    return stbi_write_png(path, W, H, 1, fb, W) ? 0 : -1;
}
