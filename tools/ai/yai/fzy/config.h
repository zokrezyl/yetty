/* Scoring constants for the vendored fzy matcher — the subset of
 * upstream's src/config.def.h that match.c/bonus.h consume (the rest of
 * that file configures the fzy TTY program, which is not vendored). */
#ifndef FZY_CONFIG_H
#define FZY_CONFIG_H

#define SCORE_GAP_LEADING -0.005
#define SCORE_GAP_TRAILING -0.005
#define SCORE_GAP_INNER -0.01
#define SCORE_MATCH_CONSECUTIVE 1.0
#define SCORE_MATCH_SLASH 0.9
#define SCORE_MATCH_WORD 0.8
#define SCORE_MATCH_CAPITAL 0.7
#define SCORE_MATCH_DOT 0.6

#endif /* FZY_CONFIG_H */
