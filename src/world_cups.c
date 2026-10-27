/*
 * world_cups.c -- a world's national cups as the SWOS 96/97 Mod's packs have them
 * (include/world_cups.h; docs/WORLD_2026_27.md, phase 4).
 */
#include "game_types.h"
#include "swos_types.h"
#include "world_cups.h"
#include "endian_helpers.h"

extern void *_competitionsTable[];
extern DIYCompetitionEx _diyCompetitionStart;
extern TeamFileEx _careerTeam;
extern void Comp_Register(Competition *c);
extern void DIY_ShuffleTeamGridPairs(void);
extern uint16_t _internationalAutoAdvanceFlag;
extern void SaveSelectedTeams(void);
extern void RestoreSelectedTeams(void);
extern void Career_AutoAdvanceCupCompetitionFromRecord(uint8_t *record);

/* The world cup a record is (0: none, or 96/97's). */
static const WorldCup *cup_of(const uint8_t *record)
{
    int32_t count = 0;
    const WorldCup *cups = World_Cups(&count);
    for (int32_t i = 0; i < count; i++)
        if ((const uint8_t *)cups[i].record == record)
            return &cups[i];
    return 0;
}

/* The world cup under way in the DIY competition, by its id. */
static const WorldCup *cup_under_way(void)
{
    int32_t count = 0;
    const WorldCup *cups = World_Cups(&count);
    const uint8_t id = (uint8_t)_diyCompetitionStart.base.field_2E;
    for (int32_t i = 0; i < count; i++)
        if ((uint8_t)((const Competition *)cups[i].record)->compId == id)
            return &cups[i];
    return 0;
}

/* The round to come's ties, by club (the team file's ordinals), for the log. */
static void log_round(const char *what, int32_t ties)
{
    const DIYCompetitionEx *x = &_diyCompetitionStart;
    const uint8_t *grid = x->teamGridPairings;
    SWOS_DBG("[world-cup] %u %s:", (unsigned)(uint8_t)x->base.field_2E, what);
    for (int32_t k = 0; k < ties && k < 32; k++)
        SWOS_DBG(" %u-%u", (unsigned)(BE_READ_U16(&x->cupTeamIds[grid[2 * k]]) & 0xFF),
                 (unsigned)(BE_READ_U16(&x->cupTeamIds[grid[2 * k + 1]]) & 0xFF));
    SWOS_DBG("\n");
}

WorldCupsState g_worldCups;

void WorldCups_Reset(void)
{
    g_worldCups.seasons = 0;
    g_worldCups.leagueCupSeason = 0;
    g_worldCups.leagueCupWinner = 0;
}

void WorldCups_SeasonEnded(void)
{
    if (g_worldCups.seasons < 255)
        g_worldCups.seasons++;
}

/* The world's first season (to its season end's background cups): the real draw. */
static int32_t first_season(void)
{
    return g_worldCups.seasons == 0;
}

int32_t WorldCups_Plays(const uint8_t *record)
{
    const WorldCup *c = cup_of(record);
    if (!c)
        return 1;
    const TeamId *list = comp_team_seed_list((Competition *)record);
    for (int32_t i = 0; i < c->n; i++)
        if (list[i].countryNumber == _careerTeam.team.countryNumber &&
            list[i].teamOrdinal == _careerTeam.team.teamOrdinal)
            return 1;
    return 0;
}

void WorldCups_Install(void)
{
    int32_t count = 0;
    const WorldCup *cups = World_Cups(&count);
    for (int32_t i = 0; i < count; i++) {
        Competition **tbl = (Competition **)_competitionsTable[cups[i].country];
        int32_t sep = 0;
        while ((intptr_t)tbl[sep] != -2)
            sep++;
        /* [leagues, -2, national cup, league cup, -1]: the league cup's table has the room
         * (competition_data.c, the SDL's DJ_TABLE_SLOTS) */
        if (cups[i].slot == 2 && (intptr_t)tbl[sep + 2] == -1)
            tbl[sep + 3] = (Competition *)(intptr_t)-1;
        tbl[sep + cups[i].slot] = (Competition *)cups[i].record;
        Comp_Register((Competition *)cups[i].record);
    }
}

uint16_t WorldCups_Entrants(const uint8_t *record, uint16_t count)
{
    const WorldCup *c = cup_of(record);
    return c ? c->n : count;
}

void WorldCups_Padded(const uint8_t *record)
{
    const WorldCup *c = cup_of(record);
    uint16_t *ids = _diyCompetitionStart.cupTeamIds;
    if (!c)
        return;
    for (int32_t i = c->n; i < ((const CompCupHeader *)record)->teams; i++)
        ids[i] = ids[0];                        /* (BE pairs: copied as they are) */
}

void WorldCups_CupMade(const uint8_t *record)
{
    const WorldCup *c = cup_of(record);
    DIYCompetitionEx *x = &_diyCompetitionStart;
    uint8_t *grid = x->teamGridPairings;
    if (!c)
        return;
    if (!first_season() && c->ties * 2 == c->n)
        DIY_ShuffleTeamGridPairs();             /* (no byes: a later season's draw) */
    if (c->ties * 2 == c->n)
        return;
    /* the real ties first, as listed; then each waiting club's tie with itself */
    const int32_t pairs = x->base.matchPairsPerRound;
    for (int32_t k = c->ties; k < pairs; k++) {
        int32_t e = 2 * c->ties + (k - c->ties);
        if (e >= c->n)
            e = c->n - 1;                       /* (a bracket wider than its clubs: never) */
        grid[2 * k] = grid[2 * k + 1] = (uint8_t)e;
    }
    x->base.totalMatchesInRound = c->ties;
    log_round("round 1", c->ties);
}

void WorldCups_ArrangeNextRound(void)
{
    const WorldCup *c = cup_under_way();
    DIYCompetitionEx *x = &_diyCompetitionStart;
    const int32_t played = x->base.currentRoundIndex;   /* (the round just played) */
    if (!c || !first_season() || played + 1 >= c->rounds)
        return;
    /* the draw undone: the winners as gathered, tie by tie (DIY_ReorderTeamGridByOrderList
     * put them in teamShuffleOrder's order) */
    uint8_t *grid = x->teamGridPairings, w[64];
    const int32_t n = x->base.matchPairsPerRound;
    for (int32_t i = 0; i < n && i < 64; i++)
        w[x->teamShuffleOrder[i]] = grid[i];
    for (int32_t i = 0; i < n && i < 64; i++)
        grid[i] = w[i];
    const uint16_t flips = c->flips[played];
    for (int32_t k = 0; 2 * k + 1 < n; k++)
        if (flips & (1u << k)) {
            const uint8_t t = grid[2 * k];
            grid[2 * k] = grid[2 * k + 1];
            grid[2 * k + 1] = t;
        }
    log_round("the next round (the real bracket)", n / 2);
}

static int32_t lists(const WorldCup *c, uint16_t team)
{
    const TeamId *list = comp_team_seed_list((Competition *)c->record);
    for (int32_t i = 0; i < c->n; i++)
        if (PACK_BE_U16(list[i].countryNumber, list[i].teamOrdinal) == team)
            return 1;
    return 0;
}

void WorldCups_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won)
{
    const WorldCup *c = cup_under_way();
    const DIYCompetition *b = &_diyCompetitionStart.base;
    /* (the id alone is not enough: a later cup's state can keep it) */
    if (!c || c->slot != 2 || b->currentRoundIndex + 1 != b->numRounds ||
        !lists(c, team1) || !lists(c, team2))
        return;
    g_worldCups.leagueCupWinner = team2Won ? team2 : team1;
    g_worldCups.leagueCupSeason = g_worldCups.seasons;
    SWOS_DBG("[world-cup] %u won by %04X\n", (unsigned)(uint8_t)b->field_2E,
             (unsigned)g_worldCups.leagueCupWinner);
}

static const WorldCup *league_cup(uint8_t country)
{
    int32_t count = 0;
    const WorldCup *cups = World_Cups(&count);
    for (int32_t i = 0; i < count; i++)
        if (cups[i].country == country && cups[i].slot == 2)
            return &cups[i];
    return 0;
}

uint16_t WorldCups_LeagueCupWinner(uint8_t country)
{
    if (!league_cup(country) || g_worldCups.leagueCupSeason != g_worldCups.seasons)
        return 0;
    return g_worldCups.leagueCupWinner;
}

void WorldCups_PlayLeagueCupEarly(uint8_t country)
{
    const WorldCup *c = league_cup(country);
    if (!c || WorldCups_LeagueCupWinner(country) || WorldCups_Plays((const uint8_t *)c->record))
        return;
    /* as the season end will: the same seeded run (the selected teams parked, as for a
     * group's early season: world_playoff.c simulate_early) */
    _internationalAutoAdvanceFlag = 1;
    SaveSelectedTeams();
    Career_AutoAdvanceCupCompetitionFromRecord((uint8_t *)c->record);
    RestoreSelectedTeams();
    _internationalAutoAdvanceFlag = 0;
}

int32_t WorldCups_RoundStringIndex(uint8_t compId, uint16_t round, uint16_t rounds)
{
    int32_t count = 0, ours = 0;
    const WorldCup *cups = World_Cups(&count);
    for (int32_t i = 0; i < count; i++)
        if ((uint8_t)((const Competition *)cups[i].record)->compId == compId)
            ours = 1;
    if (!ours || round + 3 >= rounds)
        return -1;                              /* (the last three: the engine's names) */
    if (round + 4 == rounds)
        return 12;                              /* ROUND OF 16 (_cupRoundStringTable) */
    return round;                               /* FIRST ROUND ... */
}

int32_t WorldCups_LeagueCupEurope(const uint8_t *record)
{
    const WorldCup *c = cup_of(record);
    return !c || c->europe;
}
