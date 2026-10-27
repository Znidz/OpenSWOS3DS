/*
 * league_playoffs.h -- a world's play-offs and play-outs at the season end
 * (src/league_playoffs.c; docs/WORLD_2026_27.md, phase 4).
 *
 * The original engine promotes and relegates by place: a division's top `promoted`, its
 * bottom `relegated`. A world's league can decide some of those places by knockout ties
 * after the season -- Serie B's third promotion by a play-off of the 3rd-8th, its fourth
 * relegation by a play-out of the 16th and 17th, a Serie C group's by play-outs. They are
 * decided here, as the SWOS 96/97 Mod decides them (ported: its dplay.c and gplay.c),
 * from every division's final table, and simulated: the career club's ties too, for now.
 *
 * The league's own record keeps BALANCED counts, so the season's one-division moves pass
 * the original's balanced-moves check as ever; after it, each club whose place the Mod's
 * rules send elsewhere gets its move (one level up, down, or none) in the scratch table,
 * which the parallel-groups pass then places (include/league_groups.h).
 */
#ifndef LEAGUE_PLAYOFFS_H
#define LEAGUE_PLAYOFFS_H

#include "swos_types.h"

/* A league's play-off rules (src/world_data.c, generated from the Mod's pack.json). */
typedef struct {
    uint8_t        country;
    uint8_t        divisions;
    const uint8_t *promoted;    /* per division, the Mod's counts by place (the play-offs */
    const uint8_t *relegated;   /* add the rest); a division of the lowest level: ignored */
    uint8_t        italy;       /* the Italian rules (dplay.c): Serie B and its groups */
    uint8_t        serieB, groupFirst, groups;
    const uint8_t *spec;        /* the generic ones (gplay.c's compiled bytes), 0-ended */
    uint8_t        seconds;     /* B teams: (its ordinal, its first team's or 0xFF) in the */
    const uint8_t (*second)[2]; /* team file; the highest level one may reach (0xFF any), */
    uint8_t        secondTop;   /* flags: 1 play-offs allowed, 2 it may leave the league */
    uint8_t        secondFlags; /* from the lowest division (the pool) */
} LeaguePlayoffs;

const LeaguePlayoffs *World_LeaguePlayoffs(int32_t *count);

/* The season end of the career country's primary league: Begin before its divisions are
 * played out, KeepTable after each (its final table in the DIY competition), Decide after
 * the balanced-moves check (DIY_ValidateLeagueScratchBuffer). */
void LeaguePlayoffs_Begin(void);
void LeaguePlayoffs_KeepTable(const uint8_t *competition, uint16_t division);
void LeaguePlayoffs_Decide(uint16_t country, const int8_t *table, int8_t *scratch);

#endif
