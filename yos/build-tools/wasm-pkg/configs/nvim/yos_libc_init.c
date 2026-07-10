/* yos_libc_init.c — minimal FreeBSD libc data initialisation for the
 * wasm guest.
 *
 * The FreeBSD <ctype.h> macros (`isalpha`, `isalnum`, `isdigit`, …)
 * expand inline to `__sbmaskrune(c, mask)` which dereferences
 * `_CurrentRuneLocale->__runetype[c]`. With nothing initialising the
 * rune locale, that pointer is NULL — every isalpha() returns 0 and
 * Lua's tokenizer treats `local` as an unknown character, failing
 * with "unexpected symbol near 'l'".
 *
 * FreeBSD libc proper initialises these via constructors and locale
 * tables. We don't ship full FreeBSD libc; this file provides
 * `_DefaultRuneLocale` fully initialised at compile time (no ctor
 * needed — wasm-ld doesn't reliably wire __attribute__((constructor))
 * functions into __wasm_call_ctors). Linked into nvim by build.sh.
 *
 * The rune-type bits (_CTYPE_*) come from FreeBSD's <_ctype.h>; the
 * 256-entry table mirrors what FreeBSD's `lib/libc/locale/table.c`
 * compiles in for the C locale.
 */

#include <runetype.h>
#include <_ctype.h>

/* Compile-time builder for _DefaultRuneLocale. The macros come from
 * FreeBSD's <_ctype.h>: _CTYPE_C (cntrl), _CTYPE_S (space),
 * _CTYPE_B (blank), _CTYPE_R (printing), _CTYPE_G (graph),
 * _CTYPE_P (punct), _CTYPE_D (digit), _CTYPE_X (hex digit),
 * _CTYPE_U (upper), _CTYPE_L (lower), _CTYPE_A (alpha — set on
 * uppers and lowers; isalpha(c) tests for it), _CTYPE_N (alnum extra,
 * unused for ASCII). The low 8 bits hold the digit value (0-15) for
 * D and X categories. */
#define CC  _CTYPE_C
#define CS  (_CTYPE_C|_CTYPE_S)                          /* whitespace control: \t \n \v \f \r */
#define CSB (_CTYPE_C|_CTYPE_S|_CTYPE_B)                 /* space char (0x20 special: B = blank) */
#define SP  (_CTYPE_S|_CTYPE_B|_CTYPE_R)                 /* the space ' ' itself */
#define PR  (_CTYPE_P|_CTYPE_R|_CTYPE_G)                 /* punctuation */
#define DG(N) (_CTYPE_D|_CTYPE_R|_CTYPE_G|_CTYPE_X|(N))  /* digit, value N */
#define UH(N) (_CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A|_CTYPE_X|(N))  /* upper hex, value N */
#define LH(N) (_CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A|_CTYPE_X|(N))  /* lower hex, value N */
#define UA  (_CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A)        /* uppercase letter */
#define LA  (_CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A)        /* lowercase letter */

/* Build the lowercase/uppercase tables — explicit identity for 0..127,
 * with the A-Z <-> a-z mapping in the right ranges. C lets a designated
 * array initialiser specify any element by index. */
#define ID(N) [N] = N

const _RuneLocale _DefaultRuneLocale = {
    .__magic        = "RuneMagi",
    .__encoding     = "NONE",
    .__sgetrune     = (void *)0,
    .__sputrune     = (void *)0,
    .__invalid_rune = 0xFFFD,
    .__runetype = {
        /*   0..  8 */ CC, CC, CC, CC, CC, CC, CC, CC, CC,
        /*   9.. 13 */ CSB, CS, CS, CS, CS,
        /*  14.. 31 */ CC, CC, CC, CC, CC, CC, CC, CC, CC, CC,
                       CC, CC, CC, CC, CC, CC, CC, CC,
        /*  32      */ SP,
        /*  33.. 47 */ PR, PR, PR, PR, PR, PR, PR, PR, PR, PR, PR, PR, PR, PR, PR,
        /*  48.. 57 */ DG(0), DG(1), DG(2), DG(3), DG(4),
                       DG(5), DG(6), DG(7), DG(8), DG(9),
        /*  58.. 64 */ PR, PR, PR, PR, PR, PR, PR,
        /*  65.. 70 */ UH(10), UH(11), UH(12), UH(13), UH(14), UH(15),
        /*  71.. 90 */ UA, UA, UA, UA, UA, UA, UA, UA, UA, UA,
                       UA, UA, UA, UA, UA, UA, UA, UA, UA, UA,
        /*  91.. 96 */ PR, PR, PR, PR, PR, PR,
        /*  97..102 */ LH(10), LH(11), LH(12), LH(13), LH(14), LH(15),
        /* 103..122 */ LA, LA, LA, LA, LA, LA, LA, LA, LA, LA,
                       LA, LA, LA, LA, LA, LA, LA, LA, LA, LA,
        /* 123..126 */ PR, PR, PR, PR,
        /* 127      */ CC,
        /* 128..255 */ 0,  /* trailing zeros; non-ASCII */
    },
    .__maplower = {
        ID(0),  ID(1),  ID(2),  ID(3),  ID(4),  ID(5),  ID(6),  ID(7),
        ID(8),  ID(9),  ID(10), ID(11), ID(12), ID(13), ID(14), ID(15),
        ID(16), ID(17), ID(18), ID(19), ID(20), ID(21), ID(22), ID(23),
        ID(24), ID(25), ID(26), ID(27), ID(28), ID(29), ID(30), ID(31),
        ID(32), ID(33), ID(34), ID(35), ID(36), ID(37), ID(38), ID(39),
        ID(40), ID(41), ID(42), ID(43), ID(44), ID(45), ID(46), ID(47),
        ID(48), ID(49), ID(50), ID(51), ID(52), ID(53), ID(54), ID(55),
        ID(56), ID(57), ID(58), ID(59), ID(60), ID(61), ID(62), ID(63),
        ID(64),
        /* A..Z (65..90) -> a..z (97..122) */
        [65]=97,  [66]=98,  [67]=99,  [68]=100, [69]=101, [70]=102, [71]=103, [72]=104,
        [73]=105, [74]=106, [75]=107, [76]=108, [77]=109, [78]=110, [79]=111, [80]=112,
        [81]=113, [82]=114, [83]=115, [84]=116, [85]=117, [86]=118, [87]=119, [88]=120,
        [89]=121, [90]=122,
        ID(91), ID(92), ID(93), ID(94), ID(95), ID(96),
        ID(97),  ID(98),  ID(99),  ID(100), ID(101), ID(102), ID(103), ID(104),
        ID(105), ID(106), ID(107), ID(108), ID(109), ID(110), ID(111), ID(112),
        ID(113), ID(114), ID(115), ID(116), ID(117), ID(118), ID(119), ID(120),
        ID(121), ID(122), ID(123), ID(124), ID(125), ID(126), ID(127),
    },
    .__mapupper = {
        ID(0),  ID(1),  ID(2),  ID(3),  ID(4),  ID(5),  ID(6),  ID(7),
        ID(8),  ID(9),  ID(10), ID(11), ID(12), ID(13), ID(14), ID(15),
        ID(16), ID(17), ID(18), ID(19), ID(20), ID(21), ID(22), ID(23),
        ID(24), ID(25), ID(26), ID(27), ID(28), ID(29), ID(30), ID(31),
        ID(32), ID(33), ID(34), ID(35), ID(36), ID(37), ID(38), ID(39),
        ID(40), ID(41), ID(42), ID(43), ID(44), ID(45), ID(46), ID(47),
        ID(48), ID(49), ID(50), ID(51), ID(52), ID(53), ID(54), ID(55),
        ID(56), ID(57), ID(58), ID(59), ID(60), ID(61), ID(62), ID(63),
        ID(64),
        ID(65), ID(66), ID(67), ID(68), ID(69), ID(70), ID(71), ID(72),
        ID(73), ID(74), ID(75), ID(76), ID(77), ID(78), ID(79), ID(80),
        ID(81), ID(82), ID(83), ID(84), ID(85), ID(86), ID(87), ID(88),
        ID(89), ID(90),
        ID(91), ID(92), ID(93), ID(94), ID(95), ID(96),
        /* a..z (97..122) -> A..Z (65..90) */
        [97]=65,  [98]=66,  [99]=67,  [100]=68, [101]=69, [102]=70, [103]=71, [104]=72,
        [105]=73, [106]=74, [107]=75, [108]=76, [109]=77, [110]=78, [111]=79, [112]=80,
        [113]=81, [114]=82, [115]=83, [116]=84, [117]=85, [118]=86, [119]=87, [120]=88,
        [121]=89, [122]=90,
        ID(123), ID(124), ID(125), ID(126), ID(127),
    },
};

/* `_CurrentRuneLocale` is `extern const _RuneLocale *` per the
 * user-facing runetype.h. table.c #undef-s it (since it's a macro
 * pointing through __getCurrentRuneLocale) and defines as a real
 * pointer. Mirror that. */
#undef _CurrentRuneLocale
const _RuneLocale *_CurrentRuneLocale = &_DefaultRuneLocale;

/* Single-byte locale limit used by the __sb* form of the ctype macros.
 * 128 is the C/POSIX default — every ASCII codepoint is in range. */
int __mb_sb_limit = 128;

/* Some FreeBSD libc internals reference a thread-local rune-locale
 * pointer. We don't support per-thread locale switching; leave it
 * NULL so __getCurrentRuneLocale() falls back to _CurrentRuneLocale. */
_Thread_local const _RuneLocale *_ThreadRuneLocale = (void *)0;
