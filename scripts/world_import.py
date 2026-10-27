#!/usr/bin/env python3
"""world_import.py -- build a world's game data from the SWOS 96/97 Mod's
(docs/WORLD_2026_27.md, phase 3).

    python3 -I scripts/world_import.py              # World 2026-27 -> bin/data_json/worlds/2627/

Reads the Mod's data as Davide Lorigliola sent it (bin/data_json/worlds/2627/mod/: his
teams in OpenSWOS team JSON, his packs) and writes what the game reads for that world:

    bin/data_json/worlds/2627/teams/ITALY.json        the country's team file, our format
    bin/data_json/worlds/2627/competitions/leagues.json  the league records that differ
                                                        from 96/97, by id (an overlay:
                                                        src/save_format_json.c)
    bin/data_json/worlds/2627/data/TEAM.020           the same teams as the binary file the
                                                        Amiga reads (RNC, as 96/97's)
    src/world_data.c                                  the same records baked, for the Amiga
                                                        (and both targets at start-up:
                                                        World_InstallBaked) -- committed, it
                                                        holds only league shapes

Phase 3 is the first world inside today's limits, so it is ONE country in 96/97's shape:
Italy's 51 slots hold Serie A (20, league 0), Serie B (20, league 1) and 11 Serie C clubs
outside the league (league 4), as 96/97's 51 hold A 18, B 20 and 13 others. Every other
country stays 96/97's (the overlay falls back to it).

What our engine cannot show yet is mapped, and counted on the way:
  - 16 players a team (the Mod's extra 14 are left out);
  - kit colour 10 (purple) -> 6 (maroon: the palette entry the Mod borrows for it);
  - face 3 (black skin, blond hair) -> 2;
  - nationalities the game has no name for (153 on) -> the club's country's;
  - shirt numbers above 16 (the game draws 1..16) -> the free ones of 1..16.

Ids: every team and player gets its uuid here, CANONICAL for netplay (docs/TEAM_IDS.md):
uuid5 in the database's namespace of "2627/team/<club id>" and ".../player/<index>" --
the Mod's club id (club:ita.<press name>), so a club keeps its id whatever its file or
place -- and the checksum of what a match plays it by, as scripts/assign_team_uuids.py
computes it. The data is not to be published yet: bin/data_json/worlds/ is ignored.
"""
import collections
import copy
import hashlib
import json
import os
import sys
import uuid

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "scripts"))
import emit_competitions                     # noqa: E402  (emit_league)
import swos_team_file as tf                  # noqa: E402  (the binary TEAM.nnn)
WORLD = os.path.join(REPO, "bin", "data_json", "worlds", "2627")
MOD = os.path.join(WORLD, "mod")
BASE = os.path.join(REPO, "bin", "data_json")

NAMESPACE = uuid.UUID("af19cc5a-6529-537b-935b-407bd73b75fb")   # assign_team_uuids.py's
SKILLS = ("passing", "velocity", "heading", "tackling", "control", "speed", "finishing")
NATIONS_NAMED = 153         # the game's short country names (menus.c, _shortCountryNames)
PURPLE, MAROON = 10, 6
counts = collections.Counter()


def checksum(players):
    data = bytearray()
    for p in players:
        data.append(p["position"] & 7)
        data.extend(p[k] & 15 for k in SKILLS)
        data.append(p["price"] & 255)
    return hashlib.sha1(bytes(data)).hexdigest()[:16]


def player(p, home_nation):
    """The Mod's packed player record, as our split one."""
    pf, ci = p["position_and_face"] & 255, p["cards_injuries"] & 255
    face = (pf >> 3) & 3
    if face == 3:
        face = 2
        counts["face 3 -> 2"] += 1
    nat = p["nationality"] & 255
    if nat >= NATIONS_NAMED:
        nat = home_nation
        counts["nationality without a name -> the club's country's"] += 1
    vh, tc, sf = p["velocity_heading"] & 255, p["tackling_control"] & 255, p["speed_finishing"] & 255
    return {
        "nationality": nat,
        "cached_match_value": p["field_1"] & 255,
        "shirt_number": p["shirt_number"] & 255,
        "player_name": p["player_name"],
        "position": pf >> 5,
        "face": face,
        "flags": pf & 7,
        "cards": ci & 3,
        "injury": (ci >> 2) & 7,
        "status_bucket": ci >> 5,
        "passing": p["passing"] & 255,
        "velocity": vh >> 4, "heading": vh & 15,
        "tackling": tc >> 4, "control": tc & 15,
        "speed": sf >> 4, "finishing": sf & 15,
        "price": p["price"] & 255,
        "goal_stats": [g & 255 for g in p["goal_stats"]],
        "form": p["some_flag"] & 255,
    }


def shirt_numbers(players):
    """Numbers above 16 (the game draws only 1..16) take the free ones, in order."""
    used = {p["shirt_number"] for p in players if 1 <= p["shirt_number"] <= 16}
    free = [n for n in range(1, 17) if n not in used]
    for p in players:
        if not 1 <= p["shirt_number"] <= 16:
            p["shirt_number"] = free.pop(0)
            counts["shirt number above 16 -> a free one"] += 1


def team(entry, country, ordinal, global_number, league, home_nation):
    t, x = entry["team"], entry.get("x_swos9697", {})
    kits = [k & 255 for k in t["kit_colors"]]
    if PURPLE in kits:
        kits = [MAROON if k == PURPLE else k for k in kits]
        counts["purple kit -> maroon"] += 1
    players = [player(p, home_nation) for p in entry["players"]]
    shirt_numbers(players)
    club = x.get("club_id") or "club:%d.%d" % (country, ordinal)
    tid = str(uuid.uuid5(NAMESPACE, "2627/team/%s" % club))
    for i, p in enumerate(players):
        p_out = {"uuid": str(uuid.uuid5(NAMESPACE, "2627/team/%s/player/%d" % (club, i)))}
        p_out.update(p)
        players[i] = p_out
    names = ("shirt_type", "stripes_color", "basic_color", "shorts_color", "socks_color")
    head = {
        "country_number": country, "team_ordinal": ordinal,
        "global_team_number": global_number, "team_controls": t["team_controls"] & 255,
        "team_name": t["team_name"], "writes_zero_here": t["writes_zero_here"] & 255,
        "and_with_0xfe": t["and_with_0xfe"] & 255, "tactics": t["tactics"] & 255,
        "league": league,
    }
    for i, n in enumerate(names):
        head["primary_" + n] = kits[i]
    for i, n in enumerate(names):
        head["secondary_" + n] = kits[5 + i]
    head["coach_name"] = t["coach_name"]
    head["player_values_dirty"] = t["some_flag"] & 255
    head["player_numbers"] = [n & 255 for n in t["player_numbers"]]
    return {"uuid": tid, "checksum": checksum(players), "team": head, "players": players}


def italy():
    """Italy whole: Serie A (league 0), Serie B (league 1), then Serie C and D -- outside the
    league (league 4) until a league can have their divisions (phase 4). 256 clubs: the
    country's range of global numbers grows to them (world_data.c, resize_country)."""
    base = json.load(open(os.path.join(BASE, "teams", "ITALY.json"), encoding="utf-8"))
    mod = json.load(open(os.path.join(MOD, "teams", "ITA.json"), encoding="utf-8"))
    slots = base["teams"]
    country = slots[0]["team"]["country_number"]
    home = collections.Counter(p["nationality"] for t in slots for p in t["players"]).most_common(1)[0][0]
    by_div = collections.defaultdict(list)
    for e in mod["teams"]:
        div = e.get("x_swos9697", {}).get("division")    # a string in the Mod's data ("0")
        by_div[int(div) if div not in (None, "") else None].append(e)
    serie_a, serie_b, serie_c = by_div[0], by_div[1], by_div[2]
    if len(serie_a) != 20 or len(serie_b) != 20 or len(serie_c) < 20:
        sys.exit("Italy: expected Serie A 20, Serie B 20, a Serie C group, got %d %d %d"
                 % (len(serie_a), len(serie_b), len(serie_c)))
    lower = [e for d in sorted(k for k in by_div if k not in (None, 0, 1)) for e in by_div[d]]
    picked = [(e, 0) for e in serie_a] + [(e, 1) for e in serie_b] + [(e, 4) for e in lower]
    first = slots[0]["team"]["global_team_number"]   # recomputed at load (SetTeamGlobalNumbers)
    teams = [team(e, country, i, first + i, league, home) for i, (e, league) in enumerate(picked)]
    out = copy.copy(base)
    out["teams"], out["count"] = teams, len(teams)
    return out


def italian_league():
    """96/97's record, resized: Serie A 20 (3 down), Serie B 20 (3 up), as 2026-27."""
    leagues = json.load(open(os.path.join(BASE, "competitions", "leagues.json"), encoding="utf-8"))
    rec = copy.deepcopy(next(l for l in leagues["leagues"] if l["id"] == "_italianLeague"))
    a, b = rec["divisions"]
    a.update(teams=20, relegated=3)
    b.update(teams=20, promoted=3)
    return {"type": leagues.get("type", "leagues"), "count": 1, "leagues": [rec]}


def team_file_binary(teams):
    """The binary TEAM.nnn of a team file, as scripts/emit_teams.py makes 96/97's."""
    return tf.dict_filename(teams), tf.rnc_pack(tf.dict_to_blob(teams), tf.dict_rnc_method(teams))


GLOBAL_TEAM_NUMBERS = 2000      # the league state table they index (career_diy_league_state.c)

WORLD_DATA_C = """/*
 * world_data.c -- the worlds' baked competition records (include/world.h).
 *
 * GENERATED by scripts/world_import.py from data_json/worlds/<tag>/competitions/: edit
 * the JSON (or the script), not this. World_InstallBaked copies each world's records over
 * 96/97's compiled ones at start-up, on both targets (frontend.c) -- the Amiga has no
 * JSON -- keeping each league's compiled playoff plumbing, as the SDL JSON install does.
 */
#include "game_types.h"
#include "competition_data.h"
#include "world.h"

%(externs)s

%(records)s

/* A country's range of global team numbers (base + ordinal, _teamsCountryNumbers) sized
 * to the world's clubs: every later country's base moves by the difference. The league
 * state table holds 2000; past it is a broken world, said so. */
extern uint16_t _teamsCountryNumbers[256];
extern volatile uint16_t _panicReasonCode;
extern void PanicHaltLoop(void);

static void resize_country(uint16_t country, uint16_t teams)
{
    const int16_t delta = (int16_t)(teams - (_teamsCountryNumbers[country + 1] -
                                             _teamsCountryNumbers[country]));
    for (uint16_t i = country + 1; i < 256; i++)
        _teamsCountryNumbers[i] = (uint16_t)(_teamsCountryNumbers[i] + delta);
    if (_teamsCountryNumbers[255] > %(globals)d) {
        _panicReasonCode = 0x8F02;
        PanicHaltLoop();
    }
}

static void install_league(CompetitionLeague *dst, const CompetitionLeague *src)
{
    char *p0 = dst->playoffLinkPtrs[0], *p1 = dst->playoffLinkPtrs[1];
    *dst = *src;
    dst->playoffLinkPtrs[0] = p0;
    dst->playoffLinkPtrs[1] = p1;
}

void World_InstallBaked(void)
{
    switch (World_Current()) {
%(cases)s
    default:
        break;
    }
}
"""


def world_data_c(tag, leagues, resized):
    externs, records, installs = set(), [], []
    for country, n in resized:
        installs.append("        resize_country(%d, %d);" % (country, n))
    for rec in leagues["leagues"]:
        baked = dict(rec, id="kWorld%s%s" % (tag, rec["id"]))
        records.append("static " + emit_competitions.emit_league(baked))
        externs.add("extern CompetitionLeague %s;" % rec["id"])
        externs.update("extern char %s[];" % n for n in rec["name"] if n)
        installs.append("        install_league(&%s, &%s);" % (rec["id"], baked["id"]))
    cases = "    case WORLD_%s:\n%s\n        break;" % (tag, "\n".join(installs))
    return WORLD_DATA_C % {"externs": "\n".join(sorted(externs)),
                           "records": "\n\n".join(records), "cases": cases,
                           "globals": GLOBAL_TEAM_NUMBERS}


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print("wrote", os.path.relpath(path, REPO))


def main():
    if not os.path.isdir(MOD):
        sys.exit("no %s: the Mod's data (Davide's private copies) is not here" % os.path.relpath(MOD, REPO))
    teams, leagues = italy(), italian_league()
    write(os.path.join(WORLD, "teams", "ITALY.json"), teams)
    write(os.path.join(WORLD, "competitions", "leagues.json"), leagues)
    name, packed = team_file_binary(teams)
    path = os.path.join(WORLD, "data", name.upper())
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(packed)
    print("wrote", os.path.relpath(path, REPO), "(%d bytes)" % len(packed))
    with open(os.path.join(REPO, "src", "world_data.c"), "w") as f:
        f.write(world_data_c("2627", leagues, [(teams["teams"][0]["team"]["country_number"],
                                                 teams["count"])]))
    print("wrote src/world_data.c")
    for what, n in sorted(counts.items()):
        print("  %-52s %d" % (what, n))


if __name__ == "__main__":
    main()
