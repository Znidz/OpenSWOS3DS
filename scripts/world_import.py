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

The world so far is ONE country, Italy, whole: 256 clubs, its league Serie A, Serie B,
Serie C's three groups side by side and Serie D's nine (phase 4: levels of groups,
include/league_groups.h; Serie D's odd groups play real rounds, include/fixture_rounds.h). Every other country stays 96/97's (the overlay falls back to it).

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
import re
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


SERIE_C_GROUPS = 3          # Serie C: three groups side by side under Serie B


def mod_division(e):
    div = e.get("x_swos9697", {}).get("division")    # a string in the Mod's data ("0")
    return int(div) if div not in (None, "") else None


def italy():
    """Italy whole, 256 clubs (the country's range of global numbers grows to them:
    world_data.c, resize_country), in the Mod's divisions, the league's fourteen: Serie A 0,
    Serie B 1, Serie C's groups 2..4, Serie D's 5..13."""
    base = json.load(open(os.path.join(BASE, "teams", "ITALY.json"), encoding="utf-8"))
    mod = json.load(open(os.path.join(MOD, "teams", "ITA.json"), encoding="utf-8"))
    slots = base["teams"]
    country = slots[0]["team"]["country_number"]
    home = collections.Counter(p["nationality"] for t in slots for p in t["players"]).most_common(1)[0][0]
    by_div = collections.defaultdict(list)
    for e in mod["teams"]:
        by_div[mod_division(e)].append(e)
    sizes = [len(by_div[d]) for d in range(2 + SERIE_C_GROUPS)]
    if sizes != [20] * (2 + SERIE_C_GROUPS) or None in by_div:
        sys.exit("Italy: expected Serie A, B and C's groups of 20 and every club in a division,"
                 " got %s" % sizes)
    picked = [(e, d) for d in sorted(by_div) for e in by_div[d]]
    first = slots[0]["team"]["global_team_number"]   # recomputed at load (SetTeamGlobalNumbers)
    teams = [team(e, country, i, first + i, league, home) for i, (e, league) in enumerate(picked)]
    out = copy.copy(base)
    out["teams"], out["count"] = teams, len(teams)
    return out, picked


POOL_FILE = 194             # the Mod's pseudo team file of Italy's pool (ITA_POOL.json "pool_file")


def area_codes(entry):
    """A club's areas at the levels of groups (the Mod's "X/Y"), each 0.. or None."""
    out = []
    for letter in ((entry.get("x_swos9697", {}).get("area") or "").split("/") + ["", ""])[:2]:
        out.append(ord(letter) - ord("A") if len(letter) == 1 and "A" <= letter <= "Z" else None)
    return out


def italian_pool(country):
    """The off-file pool below Serie D (the Mod's Eccellenza, ITA_POOL.json): its clubs in our
    format, in the pack's first order (the queue a relegated Serie D slot takes from), and
    each one's areas -- the group a slot that takes it plays in (src/team_pool.c)."""
    base = json.load(open(os.path.join(BASE, "teams", "ITALY.json"), encoding="utf-8"))
    mod = json.load(open(os.path.join(MOD, "teams", "ITA_POOL.json"), encoding="utf-8"))
    home = collections.Counter(p["nationality"] for t in base["teams"] for p in t["players"]).most_common(1)[0][0]
    order = mod_league()["reserve"]["first"]
    pack_ids = [e.get("x_swos9697", {}).get("club_id") for e in mod["teams"]]
    if len(order) != len(mod["teams"]):
        sys.exit("Italy's pool: %d clubs, the pack's first order %d" % (len(mod["teams"]), len(order)))
    teams = [team(e, POOL_FILE, i, 0, 0, home) for i, e in enumerate(mod["teams"])]
    out = copy.copy(base)
    out["country"], out["teams"], out["count"] = POOL_FILE, teams, len(teams)
    out.pop("country_name", None)
    areas = [area_codes(e) for e in mod["teams"]]
    counts["pool clubs"] += len(teams)
    return out, {"name": "Italy", "country": country, "file": POOL_FILE, "json": "ITALY_POOL.json",
                 "count": len(teams), "areas": areas, "ids": pack_ids}


def mod_league():
    pack = json.load(open(os.path.join(MOD, "packs", "ita", "pack.json"), encoding="utf-8"))
    return next(c for c in pack["competitions"] if c["key"] == "league")


def italian_league():
    """96/97's record, as 2026-27's pyramid: Serie A 20 (3 down), Serie B 20 (3 up, 3 down),
    Serie C's three groups of 20 (each champion up, 3 down) and Serie D's nine groups of 17
    or 18 (each champion up; none down, while no pool below takes them). The Mod's play-offs
    and play-outs (Serie B's third up, Serie C's and D's) are not played yet."""
    leagues = json.load(open(os.path.join(BASE, "competitions", "leagues.json"), encoding="utf-8"))
    rec = copy.deepcopy(next(l for l in leagues["leagues"] if l["id"] == "_italianLeague"))
    a, b = rec["divisions"]
    a.update(teams=20, relegated=3)
    b.update(teams=20, promoted=3, relegated=3)
    mod = mod_league()["divisions"]
    for d in mod[2:2 + SERIE_C_GROUPS]:
        rec["divisions"].append(dict(b, teams=d["teams"], promoted=1, relegated=3))
    for d in mod[2 + SERIE_C_GROUPS:]:
        rec["divisions"].append(dict(b, teams=d["teams"], promoted=1, relegated=0))
    for d in mod[2:]:
        # Display text: no string symbol has it (world_data_c bakes one).
        rec["name"] += [d["names"]["*"][0]] * 2
    rec["divisions_count"] = len(rec["divisions"])
    return {"type": leagues.get("type", "leagues"), "count": 1, "leagues": [rec]}


def italian_groups(picked, country, league):
    """The league's levels and each club's group at the levels of groups by its area (the
    Mod's "X/Y": X its Serie C group, Y its Serie D one)."""
    rec = league["leagues"][0]
    sizes = [d["teams"] for d in rec["divisions"]]
    levels, first = [], 0
    for n in (1, 1, SERIE_C_GROUPS, len(sizes) - 2 - SERIE_C_GROUPS):
        name = None
        if n > 1:       # the groups' names without the group: "SERIE D GIRONE A" -> "SERIE D"
            names = [rec["name"][2 * d] for d in range(first, first + n)]
            name = re.sub(r"\s*\bGIRONE\s*$", "", os.path.commonprefix(names)).strip()
        levels.append((first, n, sizes[first:first + n], name))
        first += n
    clubs = []
    for ordinal, (e, division) in enumerate(picked):
        areas = []
        for letter in ((e.get("x_swos9697", {}).get("area") or "").split("/") + ["", ""])[:2]:
            if len(letter) == 1 and "A" <= letter <= "Z":
                areas.append(ord(letter) - ord("A"))
            else:
                areas.append(None)
                counts["club without an area at a level of groups"] += 1
        clubs.append((ordinal, division, areas))
    return {"name": "Italy", "country": country, "levels": levels, "clubs": clubs}


# The Mod's knockout play-offs (pack.json 'ko_playoffs'), compiled as tools/gplay.py's
# compile_spec compiles them -- the same bytes, so src/league_playoffs.c walks them as
# gplay.c does -- for the kinds played at our season end: promotion and play-out, entrants
# [division, place], rounds of ties.
KINDS = {"promotion": 1, "playout": 4}
HOSTS = {"listed": 0, "better": 1, "drawn": 2, "neutral": 3}
LEVELS = {"better": 0, "et_pens": 1, "et_better": 2, "pens": 3, "away_pens": 4, "away_better": 5}
RESERVE = 0xFE


def compile_playoff(po, ndiv):
    kind = KINDS[po["kind"]]
    ents = []
    for e in po["entrants"]:
        d, p = e
        if not (0 <= d < ndiv and p >= 1):
            sys.exit("%s: entrant %s" % (po["key"], e))
        ents.append((0, d, p, 0))
    rounds, nt = bytearray(), 0

    def code(m):
        if m == "BL":
            return 0x3F
        if isinstance(m, int):
            return m
        mm = re.fullmatch(r"([WL])(\d+)", str(m))
        if not mm or int(mm.group(2)) >= nt:
            sys.exit("%s: member %s" % (po["key"], m))
        return {"W": 0x80, "L": 0xC0}[mm.group(1)] | int(mm.group(2))

    for rd in po["rounds"]:
        if "group" in rd or rd.get("pairing") or rd.get("clubs"):
            sys.exit("%s: only rounds of listed ties here" % po["key"])
        mem = [m for t in rd["ties"] for m in t]
        legs = 3 if rd.get("series") else rd.get("legs", 1)
        host = HOSTS[rd.get("host", "listed")] | (0x80 if rd.get("skip_if_same") else 0)
        rounds += bytes((len(mem) // 2, legs, host, LEVELS[rd.get("level", "et_pens")], 0,
                         rd.get("gap", 0)))
        rounds += bytes(code(m) for m in mem)
        nt += len(mem) // 2
    ld = po.get("loser_div", 0)
    ld = RESERVE if ld == "reserve" else ld
    flags = 2 if po.get("host_by_record") else 0
    return (bytes((kind, len(ents), len(po["rounds"]), po.get("winner_div", 0), ld, flags)) +
            b"".join(bytes(e) for e in ents) + rounds)


SECOND_RULES = {"ita": (1, 1)}   # the Mod's mkseason.SECOND_RULES: B teams up to Serie B, play-offs too


def italian_playoffs(country, league, picked=None):
    """Italy's play-off rules: the Mod's counts per division, its Italian rules (dplay.c:
    Serie B's play-off and play-out, Serie C's play-off) and the pack's knockouts played
    at the season end (its groups' play-outs; Serie D's go to the pool below, not yet)."""
    mod = mod_league()
    ndiv = len(mod["divisions"])
    spec = b"".join(compile_playoff(po, ndiv) for po in mod.get("ko_playoffs", [])
                    if po["kind"] in KINDS) + b"\0"
    counts["play-offs compiled (promotion / play-out)"] += sum(
        1 for po in mod.get("ko_playoffs", []) if po["kind"] in KINDS)
    seconds, top, flags = [], 0xFF, 0
    rules = (mod.get("second_teams") or {}).get("rules")
    if rules in SECOND_RULES and picked:
        top, po = SECOND_RULES[rules]
        flags = po | (2 if (mod.get("reserve") or {}).get("off_file") else 0)
        ordinal = {e.get("x_swos9697", {}).get("club_id"): o for o, (e, _) in enumerate(picked)}
        for o, (e, _) in enumerate(picked):
            first = e.get("x_swos9697", {}).get("second_of")
            if first:
                seconds.append((o, ordinal.get(first, 0xFF)))
        counts["B teams"] += len(seconds)
    return {"name": "Italy", "country": country, "divisions": ndiv,
            "seconds": seconds, "secondTop": top, "secondFlags": flags,
            "promoted": [d["promoted"] for d in mod["divisions"]],
            "relegated": [d["relegated"] for d in mod["divisions"]],
            "italy": mod.get("playoffs") == "ita", "serieB": 1, "groupFirst": 2,
            "groups": SERIE_C_GROUPS, "spec": spec}


def team_file_binary(teams):
    """The binary TEAM.nnn of a team file, as scripts/emit_teams.py makes 96/97's."""
    return tf.dict_filename(teams), tf.rnc_pack(tf.dict_to_blob(teams), tf.dict_rnc_method(teams))


GLOBAL_TEAM_NUMBERS = 2000      # the league state table they index (career_diy_league_state.c)

WORLD_DATA_C = """/*
 * world_data.c -- the worlds' baked competition records (include/world.h).
 *
 * GENERATED by scripts/world_import.py from data_json/worlds/<tag>/: edit
 * the JSON (or the script), not this. World_InstallBaked copies each world's records over
 * 96/97's compiled ones at start-up, on both targets (frontend.c) -- the Amiga has no
 * JSON -- keeping each league's compiled playoff plumbing, as the SDL JSON install does.
 */
#include "game_types.h"
#include "competition_data.h"
#include "world.h"
#include "league_groups.h"
#include "league_playoffs.h"
#include "team_pool.h"
#include "fixture_rounds.h"

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

/* The leagues with groups side by side (src/league_groups.c): each one's levels, and its
 * clubs with their division in the team file and their area at each level of groups. */
%(groups)s

const LeagueGroups *World_LeagueGroups(int32_t *count)
{
    switch (World_Current()) {
%(group_cases)s
    default:
        *count = 0;
        return 0;
    }
}

/* The leagues' play-offs and play-outs (src/league_playoffs.c): the Mod's counts per
 * division, its Italian rules, its knockouts compiled as tools/gplay.py compiles them. */
%(playoffs)s

const LeaguePlayoffs *World_LeaguePlayoffs(int32_t *count)
{
    switch (World_Current()) {
%(po_cases)s
    default:
        *count = 0;
        return 0;
    }
}

/* The off-file pools (src/team_pool.c): the team file each serves, its own file, its
 * clubs' areas at the levels of groups. */
%(pools)s

const TeamPool *World_TeamPools(int32_t *count)
{
    switch (World_Current()) {
%(pool_cases)s
    default:
        *count = 0;
        return 0;
    }
}
"""


def c_string(text):
    return '"%s"' % text.replace("\\", "\\\\").replace('"', '\\"')


def world_data_c(tag, leagues, resized, groups, playoffs, pools):
    externs, strings, records, installs = set(), [], [], []
    symbols = {}

    def symbol(text):
        if text not in symbols:
            symbols[text] = "kWorld%s_name%d" % (tag, len(symbols))
        return symbols[text]

    for country, n in resized:
        installs.append("        resize_country(%d, %d);" % (country, n))
    for rec in leagues["leagues"]:
        names = []
        for n in rec["name"]:
            if n and not n.startswith("_"):          # display text, not a string symbol
                if n not in symbols:
                    strings.append("static char %s[] = %s;" % (symbol(n), c_string(n)))
                n = symbols[n]
            elif n:
                externs.add("extern char %s[];" % n)
            names.append(n)
        baked = dict(rec, id="kWorld%s%s" % (tag, rec["id"]), name=names)
        records.append("static " + emit_competitions.emit_league(baked))
        externs.add("extern CompetitionLeague %s;" % rec["id"])
        installs.append("        install_league(&%s, &%s);" % (rec["id"], baked["id"]))
    installs.append("        FixtureRounds_Install();")
    cases = "    case WORLD_%s:\n%s\n        break;" % (tag, "\n".join(installs))
    group_tables, group_entries = [], []
    for g in groups:
        sym = "kWorld%s_%s" % (tag, g["name"].lower())
        rows = []
        for first, n, sizes, name in g["levels"]:
            if name:
                strings.append("static char %s[] = %s;" % (symbol(name), c_string(name)))
            rows.append("    { %d, %d, { %s }, %s }," % (first, n, ", ".join(map(str, sizes)),
                                                       symbol(name) if name else "0"))
        group_tables.append("static const LeagueLevel %sLevels[] = {\n%s\n};" % (sym, "\n".join(rows)))
        rows = ["    { %d, %d, { %s } }," % (o, b, ", ".join(
            "LEAGUE_GROUPS_NO_AREA" if x is None else str(x) for x in a)) for o, b, a in g["clubs"]]
        group_tables.append("static const LeagueGroupClub %sClubs[] = {\n%s\n};" % (sym, "\n".join(rows)))
        group_entries.append("    { %d, %d, %sLevels, %d, %sClubs },   /* %s */" % (
            g["country"], len(g["levels"]), sym, len(g["clubs"]), sym, g["name"]))
    group_tables.append("static const LeagueGroups kWorld%sGroups[] = {\n%s\n};" % (
        tag, "\n".join(group_entries)))
    group_cases = ("    case WORLD_%s:\n        *count = %d;\n        return kWorld%sGroups;"
                   % (tag, len(groups), tag))
    po_tables, po_entries = [], []
    for po in playoffs:
        sym = "kWorld%s_%s" % (tag, po["name"].lower())
        po_tables.append("static const uint8_t %sPromoted[] = { %s };" % (sym, ", ".join(map(str, po["promoted"]))))
        po_tables.append("static const uint8_t %sRelegated[] = { %s };" % (sym, ", ".join(map(str, po["relegated"]))))
        spec = po["spec"]
        po_tables.append("static const uint8_t %sSpec[%d] = {\n%s\n};" % (sym, len(spec), "\n".join(
            "    " + ", ".join("0x%02X" % b for b in spec[i:i + 16]) + "," for i in range(0, len(spec), 16))))
        po_tables.append("static const uint8_t %sSeconds[][2] = {\n%s\n};" % (sym, "\n".join(
            "    { %d, %d }," % (b, f) for b, f in po["seconds"]) or "    { 0, 0 },"))
        po_entries.append("    { %d, %d, %sPromoted, %sRelegated, %d, %d, %d, %d, %sSpec, %d, %sSeconds, %d, %d },"
                          "   /* %s */" % (
            po["country"], po["divisions"], sym, sym, int(po["italy"]), po["serieB"], po["groupFirst"],
            po["groups"], sym, len(po["seconds"]), sym, po["secondTop"], po["secondFlags"], po["name"]))
    po_tables.append("static const LeaguePlayoffs kWorld%sPlayoffs[] = {\n%s\n};" % (tag, "\n".join(po_entries)))
    po_cases = ("    case WORLD_%s:\n        *count = %d;\n        return kWorld%sPlayoffs;"
                % (tag, len(playoffs), tag))
    pool_tables, pool_entries = [], []
    for pl in pools:
        sym = "kWorld%s_%sPool" % (tag, pl["name"].lower())
        pool_tables.append("static const uint8_t %sAreas[][2] = {\n%s\n};" % (sym, "\n".join(
            "    { %s }," % ", ".join("LEAGUE_GROUPS_NO_AREA" if a is None else str(a) for a in ar)
            for ar in pl["areas"])))
        pool_entries.append('    { %d, %d, "%s", %d, %sAreas },   /* %s */' % (
            pl["country"], pl["file"], pl["json"], pl["count"], sym, pl["name"]))
    pool_tables.append("static const TeamPool kWorld%sPools[] = {\n%s\n};" % (tag, "\n".join(pool_entries)))
    pool_cases = ("    case WORLD_%s:\n        *count = %d;\n        return kWorld%sPools;"
                  % (tag, len(pools), tag))
    return WORLD_DATA_C % {"externs": "\n".join(sorted(externs)),
                           "records": "\n\n".join(strings + records), "cases": cases,
                           "globals": GLOBAL_TEAM_NUMBERS,
                           "groups": "\n\n".join(group_tables), "group_cases": group_cases,
                           "playoffs": "\n\n".join(po_tables), "po_cases": po_cases,
                           "pools": "\n\n".join(pool_tables), "pool_cases": pool_cases}


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print("wrote", os.path.relpath(path, REPO))


def main():
    if not os.path.isdir(MOD):
        sys.exit("no %s: the Mod's data (Davide's private copies) is not here" % os.path.relpath(MOD, REPO))
    (teams, picked), leagues = italy(), italian_league()
    write(os.path.join(WORLD, "teams", "ITALY.json"), teams)
    write(os.path.join(WORLD, "competitions", "leagues.json"), leagues)
    name, packed = team_file_binary(teams)
    path = os.path.join(WORLD, "data", name.upper())
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(packed)
    print("wrote", os.path.relpath(path, REPO), "(%d bytes)" % len(packed))
    country = teams["teams"][0]["team"]["country_number"]
    pool, pool_info = italian_pool(country)
    write(os.path.join(WORLD, "teams", pool_info["json"]), pool)
    name, packed = team_file_binary(pool)
    path = os.path.join(WORLD, "data", name.upper())
    open(path, "wb").write(packed)
    print("wrote", os.path.relpath(path, REPO), "(%d bytes)" % len(packed))
    with open(os.path.join(REPO, "src", "world_data.c"), "w") as f:
        f.write(world_data_c("2627", leagues, [(country, teams["count"])],
                             [italian_groups(picked, country, leagues)],
                             [italian_playoffs(country, leagues, picked)], [pool_info]))
    print("wrote src/world_data.c")
    for what, n in sorted(counts.items()):
        print("  %-52s %d" % (what, n))


if __name__ == "__main__":
    main()
