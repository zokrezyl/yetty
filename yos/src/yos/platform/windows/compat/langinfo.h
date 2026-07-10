/* <langinfo.h> compat — POSIX locale-category info. Windows uses
 * locale_t / setlocale instead. Provide just the constants + nl_langinfo
 * declaration; yos bridges return cached defaults. */
#ifndef YOS_WIN_COMPAT_LANGINFO_H
#define YOS_WIN_COMPAT_LANGINFO_H

#include <stddef.h>

typedef int nl_item;

/* Standard POSIX item codes — values pulled from glibc's <langinfo.h>. */
#define CODESET           14
#define D_T_FMT           131072
#define D_FMT             131073
#define T_FMT             131074
#define T_FMT_AMPM        131075
#define AM_STR            131076
#define PM_STR            131077
#define DAY_1             131079
#define DAY_2             131080
#define DAY_3             131081
#define DAY_4             131082
#define DAY_5             131083
#define DAY_6             131084
#define DAY_7             131085
#define ABDAY_1           131086
#define ABDAY_2           131087
#define ABDAY_3           131088
#define ABDAY_4           131089
#define ABDAY_5           131090
#define ABDAY_6           131091
#define ABDAY_7           131092
#define MON_1             131093
#define MON_2             131094
#define MON_3             131095
#define MON_4             131096
#define MON_5             131097
#define MON_6             131098
#define MON_7             131099
#define MON_8             131100
#define MON_9             131101
#define MON_10            131102
#define MON_11            131103
#define MON_12            131104
#define ABMON_1           131105
#define ABMON_2           131106
#define ABMON_3           131107
#define ABMON_4           131108
#define ABMON_5           131109
#define ABMON_6           131110
#define ABMON_7           131111
#define ABMON_8           131112
#define ABMON_9           131113
#define ABMON_10          131114
#define ABMON_11          131115
#define ABMON_12          131116
#define ERA               131120
#define ERA_D_FMT         131121
#define ALT_DIGITS        131122
#define ERA_D_T_FMT       131123
#define ERA_T_FMT         131124
#define RADIXCHAR         65536
#define THOUSEP           65537
#define YESEXPR           327680
#define NOEXPR            327681
#define CRNCYSTR          262159

#ifdef __cplusplus
extern "C" {
#endif

extern char *nl_langinfo(nl_item item);

#ifdef __cplusplus
}
#endif

#endif
