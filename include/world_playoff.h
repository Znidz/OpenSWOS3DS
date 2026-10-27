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
 * Serie B's play-out (the 17th v the 16th, two legs; level: the 16th stays, or extra time
 * and penalties when they finished level on points) and play-off (the 3rd-8th: 5th v 8th and
 * 6th v 7th single, extra time, at the better placed's; the 3rd and the 4th wait -- byes; semis
 * 3rd v W(6v7), 4th v W(5v8), two legs, the return at the 3rd's / 4th's; a final of two legs,
 * the first at the worse placed's, extra time and penalties when the two are level on points;
 * any other level tie: the better placed).
 *
 * And a group's play-out (the pack's knockouts, gplay.c: Serie C's 16th v 19th, 17th v 18th,
 * two legs; Serie D's single, extra time): its ties are independent, so the career club's
 * bracket is its own tie, a contest of two (the other tie is decided at the season end).
 *
 * A bye is a club's tie with itself, listed after the round's real ties and not played
 * (the round plays totalMatchesInRound of its matchPairsPerRound ties): the cup engine's
 * halving bracket holds -- 8 places for Serie B's 6 clubs.
 */
#ifndef WORLD_PLAYOFF_H
#define WORLD_PLAYOFF_H

#include "swos_types.h"

enum { WORLD_PLAYOFF_NONE, WORLD_PLAYOFF_B_PLAYOUT, WORLD_PLAYOFF_B_PLAYOFF, WORLD_PLAYOFF_GROUP_TIE };

/* The contest under way or played this season (saved with the career). */
typedef struct {
    uint8_t  kind;              /* WORLD_PLAYOFF_*; NONE: no contest */
    uint8_t  n;                 /* its clubs */
    uint8_t  etp;               /* level decided by penalties, not by the better placed */
    uint8_t  rules;             /* a group's tie: its round's flag byte (legs / e.t. / pens) */
    uint16_t team[8];           /* {country, ordinal} as the game packs it, entrant order */
    uint8_t  prio[8];           /* the better placed: lower */
    uint16_t pts[8];            /* their points (the final's extra time + penalties) */
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

/* A round of the contest under way has been played and its winners gathered
 * (DIY_UpdateCompetitionProgress): the next round's ties and rules. */
void WorldPlayoff_ArrangeNextRound(void);

/* After a career is loaded: slot 4 back on the contest, if one is under way. */
void WorldPlayoff_RestoreSlot(void);

#endif
