#ifndef TXT_IO_STUB_H
#define TXT_IO_STUB_H

#include "txt_main.h"

static inline void TXT_GotoXY(int x, int y) { (void) x; (void) y; }
static inline void TXT_FGColor(int c) { (void) c; }
static inline void TXT_BGColor(int c, int blink) { (void) c; (void) blink; }
static inline void TXT_Puts(const char *s) { (void) s; }
static inline void TXT_PutChar(int c) { (void) c; }

#endif
