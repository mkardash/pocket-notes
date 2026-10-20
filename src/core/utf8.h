/* UTF-8 helpers. Invalid bytes decode as U+FFFD and count as one byte. */
#ifndef UTF8_H
#define UTF8_H
#include <stdint.h>

/* Decodes one code point at s (len bytes available). Returns bytes consumed (>=1 when len>0). */
int utf8_decode(const char *s, int len, uint32_t *cp);
/* Encodes cp into out (needs 4 bytes). Returns bytes written. */
int utf8_encode(uint32_t cp, char *out);
/* Byte offset of the character before pos (0 if pos is 0). */
int utf8_prev(const char *s, int pos);
/* Byte offset of the character after pos (len if at end). */
int utf8_next(const char *s, int len, int pos);
/* Number of code points in s[0..len). */
int utf8_count(const char *s, int len);
/* Lowercase for ASCII, Ukrainian/Russian Cyrillic letters; other code points unchanged. */
uint32_t utf8_tolower(uint32_t cp);

#endif
