/* Host (Mac) stand-in for PocketBook's inkview.h.
 * Declares only what the app uses; constants and signatures match SDK 6.8 (B300). */
#ifndef INKVIEW_HOST_H
#define INKVIEW_HOST_H
#ifdef __cplusplus
extern "C" {
#endif

#define EVT_INIT 21
#define EVT_EXIT 22
#define EVT_SHOW 23
#define EVT_HIDE 24
#define EVT_KEYPRESS 25
#define EVT_KEYRELEASE 26
#define EVT_KEYREPEAT 28
#define EVT_KEYPRESS_EXT 40
#define EVT_KEYRELEASE_EXT 41
#define EVT_KEYREPEAT_EXT 42
#define EVT_POINTERUP 29
#define EVT_POINTERDOWN 30
#define EVT_POINTERMOVE 31
#define EVT_ORIENTATION 32
#define EVT_POINTERLONG 34
#define EVT_POINTERHOLD 35
#define EVT_POINTERDRAG 44
#define EVT_POINTERCANCEL 45
#define EVT_REPAINT 43
#define EVT_FOREGROUND 151
#define EVT_BACKGROUND 152
#define EVT_SAVESTATE 155
#define EVT_EXT_KB 210
#define EVT_LETTER 211

#define IV_KEY_POWER  0x01
#define IV_KEY_DELETE 0x08
#define IV_KEY_OK     0x0a
#define IV_KEY_UP     0x11
#define IV_KEY_DOWN   0x12
#define IV_KEY_LEFT   0x13
#define IV_KEY_RIGHT  0x14
#define IV_KEY_MENU   0x17
#define IV_KEY_PREV   0x18
#define IV_KEY_NEXT   0x19
#define IV_KEY_HOME   0x1a
#define IV_KEY_BACK   0x1b
#define IV_KEY_PREV2  0x1c
#define IV_KEY_NEXT2  0x1d

#define BLACK 0x000000
#define DGRAY 0x555555
#define LGRAY 0xaaaaaa
#define WHITE 0xffffff

#define PANEL_DISABLED 0

typedef enum { FONT_STD = 0, FONT_BOLD = 1, FONT_ITALIC = 2, FONT_BOLDITALIC = 3, FONT_MONO = 4 } FONT_TYPE;

typedef struct ifont_s {
    char *name;
    char *family;
    int size;
    unsigned char aa;
    unsigned char isbold;
    unsigned char isitalic;
    unsigned char _r1;
    unsigned short charset;
    unsigned short _r2;
    int color;
    int height;
    int linespacing;
    int baseline;
    void *fdata;
} ifont;

typedef int (*iv_handler)(int type, int par1, int par2);
typedef void (*iv_timerproc)();

void InkViewMain(iv_handler h);
void CloseApp();
int ScreenWidth();
int ScreenHeight();
void ClearScreen();
void SetClip(int x, int y, int w, int h);
void DrawLine(int x1, int y1, int x2, int y2, int color);
void DrawRect(int x, int y, int w, int h, int color);
void FillArea(int x, int y, int w, int h, int color);
void InvertArea(int x, int y, int w, int h);
char **EnumFonts();
char *iv_get_default_font(FONT_TYPE fonttype);
#define DEFAULTFONT iv_get_default_font(FONT_STD)
#define DEFAULTFONTB iv_get_default_font(FONT_BOLD)
#define DEFAULTFONTI iv_get_default_font(FONT_ITALIC)
#define DEFAULTFONTBI iv_get_default_font(FONT_BOLDITALIC)
#define DEFAULTFONTM iv_get_default_font(FONT_MONO)
ifont *OpenFont(const char *name, int size, int aa);
void CloseFont(ifont *f);
void SetFont(const ifont *font, int color);
void DrawString(int x, int y, const char *s);
int StringWidth(const char *s);
void FullUpdate();
void SoftUpdate();
void PartialUpdate(int x, int y, int w, int h);
void PartialUpdateBW(int x, int y, int w, int h);
void DynamicUpdate(int x, int y, int w, int h);
void DynamicUpdateBW(int x, int y, int w, int h);
void SetHardTimer(const char *name, iv_timerproc tproc, int ms);
void SetWeakTimer(const char *name, iv_timerproc tproc, int ms);
void ClearTimer(iv_timerproc tproc);
const char *currentLang();
void SetPanelType(int type);

void SetOrientation(int n);
int GetBatteryPower();

#ifdef __cplusplus
}
#endif
#endif
