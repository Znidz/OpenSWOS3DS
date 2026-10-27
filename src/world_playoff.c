/*
 * world_playoff.c -- the career club's own play-off, played (include/world_playoff.h).
 *
 * After the SWOS 96/97 Mod's dplay.py / dplay.c (dp_lend, dp_draw, dp_rec, dp_end; MIT,
 * Davide Lorigliola): the contest is built the way 96/97 builds its English play-offs in
 * slot 4 -- a Playoffs record, its clubs from the final table (DIY_Rebuild...'s steps) --
 * but when the league ends, since which one the club plays (if any) is known only then.
 */
#include "world_playoff.h"
#include "league_playoffs.h"
#include "game_types.h"
#include "comp_identity.h"
#include "diy_competition.h"
#include "career_teams.h"
#include "endian_helpers.h"

extern TeamFileEx _careerTeam;
extern DIYCompetitionEx _diyCompetitionStart;
extern CompetitionSlotBuffer _diyCompetitionContextBuf;
extern CompetitionSlotBuffer _primaryCompetitionBuf;
extern uint8_t *_diyCompetitionPtr;
extern uint32_t *_diyCompetitionTablePtr;
extern uint16_t _currentDivisionIndex;
extern uint16_t _competitionSlotUpdatedFlag;
extern Playoffs _div1Playoffs;
extern void Career_SetCurrentCompetitionSlot(uint8_t *comp, uint16_t slot, uint16_t divisionIndex,
                                             uint16_t hasSelectedTeams);
extern void Career_LoadCompetitionSlotStateFromBuffer(uint8_t *slot);
extern void Career_StoreCompetitionSlotStateToBuffer(uint8_t *slot);
extern void DIY_BuildTeamSelectionPointerCache(void);
extern void DIY_UpdateCompetitionProgressOrAdvanceSlot(uint8_t *blk);

#define COMPID_worldPlayoff 129     /* (a dynamic competition: >= 128, comp_identity.h) */

WorldPlayoffState g_worldPlayoff;
static Playoffs s_rec;
static char s_name[] = "PLAY-OUT SERIE B", s_short[] = "PLAY-OUT";

void WorldPlayoff_Reset(void)
{
    uint8_t *p = (uint8_t *)&g_worldPlayoff;
    for (uint32_t i = 0; i < sizeof g_worldPlayoff; i++)
        p[i] = 0;
}

/* The contest's record from the state: 96/97's Division One play-off, re-shaped. */
static void make_record(void)
{
    s_rec = _div1Playoffs;
    s_rec.comp.compId = (uint8_t)COMPID_worldPlayoff;
    s_rec.teams = (int8_t)g_worldPlayoff.n;
    s_rec.awayGoalsMode = 0;                    /* (Italy: no away goals) */
    /* two legs (bits 7-6 = 2); extra time + penalties (bits 5-4, 3-2 = 1) when level on points */
    s_rec.roundRuleFlags[0] = (int8_t)(g_worldPlayoff.etp ? 0x94 : 0x80);
    s_rec.roundRuleFlags[1] = 0;
    s_rec.name[0] = s_name;
    s_rec.name[1] = s_short;
    for (uint8_t i = 0; i < g_worldPlayoff.n && i < 4; i++)
        BE_WRITE_U16(&s_rec.qualifiedTeamIds[2 * i], g_worldPlayoff.team[i]);
}

#ifdef SWOS_SDL_BUILD
/* Test (SWOS_TEST_PLAYOFF_PLACE=<place>): the career club takes that place of its final
 * table, figures and all (the Mod's DPLAY_FORCE / dp_forcecfb); the play-out is held. */
static int32_t force_place(DIYCompetitionEx *ex, int32_t n)
{
    const char *v = getenv("SWOS_TEST_PLAYOFF_PLACE");
    const int32_t want = v ? atoi(v) - 1 : -1;
    uint8_t *base = (uint8_t *)&ex->base;
    uint16_t *order = ex->matchData.league.teamEntryOffsets;
    int32_t mine = -1;
    if (want < 0 || want >= n)
        return 0;
    for (int32_t k = 0; k < n; k++)
        if (((DIYTeamStats *)(base + order[k] + DIY_STANDINGS_OFFSET))->teamSelOffset == -1)
            mine = k;
    if (mine < 0)
        return 0;
    if (mine != want) {
        DIYTeamStats *a = (DIYTeamStats *)(base + order[mine] + DIY_STANDINGS_OFFSET);
        DIYTeamStats *b = (DIYTeamStats *)(base + order[want] + DIY_STANDINGS_OFFSET);
        DIYTeamStats x = *a;
        const int32_t sa = a->teamSelOffset, sb = b->teamSelOffset;
        *a = *b; *b = x;                        /* the figures change places, */
        a->teamSelOffset = sa; b->teamSelOffset = sb;   /* the clubs keep their rows */
        const uint16_t o = order[mine];
        order[mine] = order[want];
        order[want] = o;
    }
    LeaguePlayoffs_TestHeld = 1;
    return 1;
}
#endif

int32_t WorldPlayoff_CareerLeagueEnded(void)
{
    DIYCompetitionEx *ex = &_diyCompetitionStart;
    int32_t count = 0;
    const LeaguePlayoffs *all = World_LeaguePlayoffs(&count), *lp = 0;
    if (g_worldPlayoff.kind)
        return 1;                               /* (built already: slot 4 is ours) */
    if (_diyCompetitionPtr)
        return 0;                               /* (slot 4 is 96/97's) */
    for (int32_t i = 0; i < count; i++)
        if (all[i].country == _careerTeam.team.countryNumber)
            lp = &all[i];
    if (!lp || !lp->italy || _currentDivisionIndex != lp->serieB || ex->base.competitionType != 0)
        return 0;

    const int32_t n = (uint16_t)ex->base.numTeams;
    if (n < 17 || n > 32)
        return 0;
#ifdef SWOS_SDL_BUILD
    if (force_place(ex, n))
        Career_StoreCompetitionSlotStateToBuffer((uint8_t *)&_primaryCompetitionBuf);
#endif
    static uint16_t team[32], pts[32];      /* (static: in BSS, no memset on the Amiga) */
    int32_t mine = -1;
    for (int32_t k = 0; k < n; k++) {
        const DIYTeamStats *st = (const DIYTeamStats *)((uint8_t *)&ex->base +
                                 ex->matchData.league.teamEntryOffsets[k] + DIY_STANDINGS_OFFSET);
        const TeamFileEx *t = (const TeamFileEx *)diy_resolve_team(st->teamSelOffset);
        team[k] = PACK_BE_U16(t->team.countryNumber, t->team.teamOrdinal);
        pts[k] = st->points;
        if (st->teamSelOffset == -1)
            mine = k;
    }
    /* Serie B's play-out (dplay.c dp_lend): the 16th and the 17th, held unless the 16th
     * leads by more than 4 points; the first leg at the 17th's. */
    if (!((mine == 15 || mine == 16) && LeaguePlayoffs_OutHeld(pts[15], pts[16])))
        return 0;
    WorldPlayoff_Reset();
    g_worldPlayoff.kind = WORLD_PLAYOFF_B_PLAYOUT;
    g_worldPlayoff.n = 2;
    g_worldPlayoff.etp = pts[15] == pts[16];
    g_worldPlayoff.team[0] = team[16]; g_worldPlayoff.prio[0] = 16;
    g_worldPlayoff.team[1] = team[15]; g_worldPlayoff.prio[1] = 15;

    /* Into slot 4, as 96/97's play-offs: the record (its calendar), then its clubs. */
    make_record();
    _diyCompetitionTablePtr = (uint32_t *)0;
    Career_SetCurrentCompetitionSlot((uint8_t *)&s_rec, 4, 0, 0);
    {
        uint8_t *ctx = (uint8_t *)&_diyCompetitionContextBuf;
        uint16_t ids[8];
        for (uint8_t i = 0; i < g_worldPlayoff.n; i++)
            BE_WRITE_U16(&ids[i], g_worldPlayoff.team[i]);
        AppendTeamsToSelectionFromTmdOrTeamFile(((DIYCompetitionEx *)ctx)->cupTeamIds, ids,
                                                g_worldPlayoff.n);
        Career_LoadCompetitionSlotStateFromBuffer(ctx);
        DIY_BuildTeamSelectionPointerCache();
        DIY_UpdateCompetitionProgressOrAdvanceSlot((uint8_t *)&_diyCompetitionStart);
        Career_StoreCompetitionSlotStateToBuffer(ctx);
        _competitionSlotUpdatedFlag = 1;
        _diyCompetitionContextBuf.tourn.header.slotStateA = _primaryCompetitionBuf.tourn.header.slotStateA;
        _diyCompetitionContextBuf.tourn.header.slotStateB = _primaryCompetitionBuf.tourn.header.slotStateB;
        _diyCompetitionContextBuf.tourn.header.slotStateC = _primaryCompetitionBuf.tourn.header.slotStateC;
    }
    SWOS_DBG("[world-playoff] PLAY-OUT SERIE B: %04X (17th) v %04X (16th)%s\n",
             (unsigned)team[16], (unsigned)team[15], g_worldPlayoff.etp ? ", e.t. + penalties" : "");
    return 1;
}

static int32_t ours(void)
{
    return g_worldPlayoff.kind && (uint8_t)_diyCompetitionStart.base.field_2E == COMPID_worldPlayoff;
}

static int32_t prio_of(uint16_t team)
{
    for (uint8_t i = 0; i < g_worldPlayoff.n; i++)
        if (g_worldPlayoff.team[i] == team)
            return g_worldPlayoff.prio[i];
    return 0xFF;
}

/* A tie of the contest: the competition under way is ours and both clubs are entrants
 * (the id alone is not enough: a later cup's state can keep it). */
static int32_t our_tie(uint16_t team1, uint16_t team2)
{
    return ours() && prio_of(team1) != 0xFF && prio_of(team2) != 0xFF;
}

int32_t WorldPlayoff_LevelWinner(uint16_t team1, uint16_t team2)
{
    if (!our_tie(team1, team2) || g_worldPlayoff.etp)
        return 0;
    /* (the play-out: level -> the better placed, the 16th, stays: dplay.c dp_draw) */
    return prio_of(team1) <= prio_of(team2) ? 1 : 2;
}

void WorldPlayoff_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won)
{
    if (!our_tie(team1, team2))
        return;
    g_worldPlayoff.winner = team2Won ? team2 : team1;
    SWOS_DBG("[world-playoff] decided: %04X\n", (unsigned)g_worldPlayoff.winner);
}

void WorldPlayoff_RestoreSlot(void)
{
    if (!g_worldPlayoff.kind)
        return;
    make_record();
    _diyCompetitionPtr = (uint8_t *)&s_rec;
    _diyCompetitionTablePtr = (uint32_t *)0;
}
