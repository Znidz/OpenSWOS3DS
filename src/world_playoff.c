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
static CompetitionCup s_rec;
static char s_outName[] = "PLAY-OUT SERIE B", s_outShort[] = "PLAY-OUT";
static char s_offName[] = "PLAY-OFF SERIE B", s_offShort[] = "PLAY-OFF";

void WorldPlayoff_Reset(void)
{
    uint8_t *p = (uint8_t *)&g_worldPlayoff;
    for (uint32_t i = 0; i < sizeof g_worldPlayoff; i++)
        p[i] = 0;
}

/* The contest's record from the state: a cup record (a Playoffs record carries only two
 * rounds' rules, and the career calendar is built from them when the contest is made),
 * headed as 96/97's Division One play-off -- its dates after the league -- with no draw. */
static void make_record(void)
{
    const int32_t off = g_worldPlayoff.kind == WORLD_PLAYOFF_B_PLAYOFF;
    uint8_t *z = (uint8_t *)&s_rec;
    for (uint32_t i = 0; i < sizeof s_rec; i++)
        z[i] = 0;
    s_rec.header.comp = _div1Playoffs.comp;
    s_rec.header.comp.compId = (uint8_t)COMPID_worldPlayoff;
    s_rec.header.comp.isPlayoff = 0;
    s_rec.header.comp.reserved1 = 15;           /* (a cup's, inert) */
    s_rec.header.comp.reserved2 = 21;
    s_rec.header.comp.seedReorderFlag = 1;      /* the clubs as listed: no draw */
    s_rec.header.teams = (int8_t)g_worldPlayoff.n;
    s_rec.header.awayGoalsMode = 0;             /* (Italy: no away goals) */
    s_rec.header.subsAndPointsForWin = _div1Playoffs.subsAndMaxSubs;
    s_rec.header.roundStringBase = 1;
    /* per round: legs (bits 7-6: 0 single, 2 two), extra time (5-4), penalties (3-2) */
    if (off) {
        s_rec.roundRuleFlags[0] = 0x10;         /* 5v8, 6v7: single, e.t. */
        s_rec.roundRuleFlags[1] = (int8_t)0x80; /* semis: two legs */
        s_rec.roundRuleFlags[2] = (int8_t)0x80; /* final: two legs (e.t. + pens: ArrangeNextRound) */
    } else
        s_rec.roundRuleFlags[0] = (int8_t)(g_worldPlayoff.etp ? 0x94 : 0x80);
    s_rec.name[0] = off ? s_offName : s_outName;
    s_rec.name[1] = off ? s_offShort : s_outShort;
    for (uint8_t i = 0; i < g_worldPlayoff.n && i < 32; i++) {
        s_rec.teamSeedList[i].countryNumber = (uint8_t)(g_worldPlayoff.team[i] >> 8);
        s_rec.teamSeedList[i].teamOrdinal = (uint8_t)g_worldPlayoff.team[i];
    }
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

/* A round's rules, in the competition state (a Playoffs record carries two rounds'):
 * legs 0 single / 2 two; extra time, penalties 1 on. */
static void round_rules(int32_t r, uint16_t legs, uint16_t et, uint16_t pens)
{
    _diyCompetitionStart.matchData.cup.roundLegs[r] = legs;
    _diyCompetitionStart.matchData.cup.roundExtraTime[r] = et;
    _diyCompetitionStart.matchData.cup.roundPenalties[r] = pens;
}

/* The first round, the contest just made from its record: only its real ties played
 * (the byes after them). */
static void first_round(void)
{
    DIYCompetitionEx *c = &_diyCompetitionStart;
    if (g_worldPlayoff.kind == WORLD_PLAYOFF_B_PLAYOFF)
        c->base.totalMatchesInRound = 2;        /* (the 3rd's and the 4th's byes not played) */
}

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
    /* Serie B's play-off (dplay.c dp_lend): the 3rd-8th, unless the 3rd leads the 4th by
     * more than 14 points (it goes up directly); the list 5th, 8th, 6th, 7th, then the
     * 3rd and the 4th each with itself (its bye). */
    static const uint8_t kPlayoffPlaces[8] = { 4, 7, 5, 6, 2, 2, 3, 3 };
    WorldPlayoff_Reset();
    if (mine >= 2 && mine <= 7 && LeaguePlayoffs_PlayoffHeld(pts[2], pts[3])) {
        g_worldPlayoff.kind = WORLD_PLAYOFF_B_PLAYOFF;
        g_worldPlayoff.n = 8;
        for (uint8_t i = 0; i < 8; i++) {
            g_worldPlayoff.team[i] = team[kPlayoffPlaces[i]];
            g_worldPlayoff.prio[i] = kPlayoffPlaces[i];
            g_worldPlayoff.pts[i] = pts[kPlayoffPlaces[i]];
        }
    /* Serie B's play-out (dplay.c dp_lend): the 16th and the 17th, held unless the 16th
     * leads by more than 4 points; the first leg at the 17th's. */
    } else if ((mine == 15 || mine == 16) && LeaguePlayoffs_OutHeld(pts[15], pts[16])) {
        g_worldPlayoff.kind = WORLD_PLAYOFF_B_PLAYOUT;
        g_worldPlayoff.n = 2;
        g_worldPlayoff.etp = pts[15] == pts[16];
        g_worldPlayoff.team[0] = team[16]; g_worldPlayoff.prio[0] = 16; g_worldPlayoff.pts[0] = pts[16];
        g_worldPlayoff.team[1] = team[15]; g_worldPlayoff.prio[1] = 15; g_worldPlayoff.pts[1] = pts[15];
    } else
        return 0;

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
        first_round();
        DIY_BuildTeamSelectionPointerCache();
        DIY_UpdateCompetitionProgressOrAdvanceSlot((uint8_t *)&_diyCompetitionStart);
        Career_StoreCompetitionSlotStateToBuffer(ctx);
        _competitionSlotUpdatedFlag = 1;
        _diyCompetitionContextBuf.tourn.header.slotStateA = _primaryCompetitionBuf.tourn.header.slotStateA;
        _diyCompetitionContextBuf.tourn.header.slotStateB = _primaryCompetitionBuf.tourn.header.slotStateB;
        _diyCompetitionContextBuf.tourn.header.slotStateC = _primaryCompetitionBuf.tourn.header.slotStateC;
    }
    SWOS_DBG("[world-playoff] %s: %u clubs, the career club %d%s\n",
             g_worldPlayoff.kind == WORLD_PLAYOFF_B_PLAYOFF ? "PLAY-OFF SERIE B" : "PLAY-OUT SERIE B",
             (unsigned)g_worldPlayoff.n, (int)mine + 1, g_worldPlayoff.etp ? ", e.t. + penalties" : "");
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
    const DIYCompetition *b = &_diyCompetitionStart.base;
    if (!our_tie(team1, team2) || b->currentRoundIndex + 1 != b->numRounds)
        return;                                 /* (the last round's: the contest's result) */
    g_worldPlayoff.winner = team2Won ? team2 : team1;
    SWOS_DBG("[world-playoff] decided: %04X\n", (unsigned)g_worldPlayoff.winner);
}

void WorldPlayoff_ArrangeNextRound(void)
{
    DIYCompetitionEx *c = &_diyCompetitionStart;
    uint8_t *grid = c->teamGridPairings;
    if (!ours() || g_worldPlayoff.kind != WORLD_PLAYOFF_B_PLAYOFF)
        return;
    if (c->base.currentRoundIndex == 0) {
        /* the winners (entrant indices): 0/1 W(5v8), 2/3 W(6v7), 4 the 3rd, 6 the 4th --
         * semis 3rd v W(6v7), 4th v W(5v8), the first leg at the winner's */
        uint8_t w58 = 0xFF, w67 = 0xFF;
        for (int32_t i = 0; i < 4; i++) {
            if (grid[i] <= 1) w58 = grid[i];
            else if (grid[i] <= 3) w67 = grid[i];
        }
        if (w58 == 0xFF || w67 == 0xFF)
            return;
        grid[0] = w67; grid[1] = 4;
        grid[2] = w58; grid[3] = 6;
    } else if (c->base.currentRoundIndex == 1) {
        /* the final: the first leg at the worse placed's; level on points: e.t. + penalties */
        const uint8_t a = grid[0], b = grid[1];
        const int32_t aBetter = g_worldPlayoff.prio[a] < g_worldPlayoff.prio[b];
        grid[0] = aBetter ? b : a;
        grid[1] = aBetter ? a : b;
        g_worldPlayoff.etp = g_worldPlayoff.pts[a] == g_worldPlayoff.pts[b];
        round_rules(2, 2, g_worldPlayoff.etp, g_worldPlayoff.etp);
    }
}

void WorldPlayoff_RestoreSlot(void)
{
    if (!g_worldPlayoff.kind)
        return;
    make_record();
    _diyCompetitionPtr = (uint8_t *)&s_rec;
    _diyCompetitionTablePtr = (uint32_t *)0;
}
