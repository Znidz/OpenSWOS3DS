/*
 * world_playoff.h -- the career club's own play-off, played (src/world_playoff.c;
 * docs/WORLD_2026_27.md, phase 4).
 *
 * A world's play-offs and play-outs are decided at the season end (league_playoffs.h).
 * When the career club takes part in one, it is a contest it plays instead: built when the
 * club's league has played its last match day, into competition slot 4 -- where 96/97's
 * English play-offs are -- dated after the league; its result then stands at the season
 * end in place of the simulated one (as the SWOS 96/97 Mod's dp_lend / dp_end do).
 *
 * Stage 1: Serie B's play-out (the 17th v the 16th, two legs; level: the 16th stays, or
 * extra time and penalties when they finished level on points).
 */
#ifndef WORLD_PLAYOFF_H
#define WORLD_PLAYOFF_H

#include "swos_types.h"

enum { WORLD_PLAYOFF_NONE, WORLD_PLAYOFF_B_PLAYOUT };

/* The contest under way or played this season (saved with the career). */
typedef struct {
    uint8_t  kind;              /* WORLD_PLAYOFF_*; NONE: no contest */
    uint8_t  n;                 /* its clubs */
    uint8_t  etp;               /* extra time + penalties (the clubs level on points) */
    uint8_t  pad;
    uint16_t team[8];           /* {country, ordinal} as the game packs it, entrant order */
    uint8_t  prio[8];           /* the better placed: lower */
    uint16_t winner;            /* its last tie's winner, 0: not decided yet */
} WorldPlayoffState;

extern WorldPlayoffState g_worldPlayoff;

/* A new season / career: no contest. */
void WorldPlayoff_Reset(void);

/* The career club's league has played its last match day: build its play-off, if it has one.
 * 1 when slot 4 holds a world's play-off (built now or before): 96/97's slot-4 rebuild
 * (DIY_RebuildCompetitionAndTeamSelectionCaches, the English play-offs) is not for it. */
int32_t WorldPlayoff_CareerLeagueEnded(void);

/* A tie of the contest under way ended level, and its rules give it no replay: 1 if team1
 * goes through, 2 if team2, 0 if not ours (the game's rule stands). */
int32_t WorldPlayoff_LevelWinner(uint16_t team1, uint16_t team2);

/* A tie of the contest under way was decided. */
void WorldPlayoff_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won);

/* After a career is loaded: slot 4 back on the contest, if one is under way. */
void WorldPlayoff_RestoreSlot(void);

#endif
