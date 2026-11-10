// Textscreen stub for the 3DS port.  Heretic only uses textscreen for its
// optional DOS-style text-mode startup screen; TXT_Init() returning 0
// makes the game skip it and log to stdout (the bottom-screen console).

#ifndef TXT_MAIN_STUB_H
#define TXT_MAIN_STUB_H

enum
{
    TXT_COLOR_BLACK, TXT_COLOR_BLUE, TXT_COLOR_GREEN, TXT_COLOR_CYAN,
    TXT_COLOR_RED, TXT_COLOR_MAGENTA, TXT_COLOR_BROWN, TXT_COLOR_GREY,
    TXT_COLOR_DARK_GREY, TXT_COLOR_BRIGHT_BLUE, TXT_COLOR_BRIGHT_GREEN,
    TXT_COLOR_BRIGHT_CYAN, TXT_COLOR_BRIGHT_RED, TXT_COLOR_BRIGHT_MAGENTA,
    TXT_COLOR_YELLOW, TXT_COLOR_BRIGHT_WHITE
};

static inline int TXT_Init(void) { return 0; }
static inline void TXT_Shutdown(void) { }
static inline unsigned char *TXT_GetScreenData(void) { return 0; }
static inline void TXT_UpdateScreen(void) { }
static inline int TXT_GetChar(void) { return -1; }

#endif
