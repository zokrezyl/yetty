#ifndef _YOS_VIS_H_
#define _YOS_VIS_H_
#include <sys/types.h>
#define VIS_OCTAL   0x01
#define VIS_CSTYLE  0x02
#define VIS_TAB     0x08
#define VIS_NL      0x10
#define VIS_WHITE   (VIS_TAB | VIS_NL)
#define VIS_NOSLASH 0x40
char *strvis(char *, const char *, int);
int   strvisx(char *, const char *, size_t, int);
#endif
