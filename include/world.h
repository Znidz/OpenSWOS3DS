/*
 * world.h -- which football world the game plays (docs/WORLD_2026_27.md §4).
 *
 * A world is a data set: its countries, clubs, competitions and season rules. 96/97 is
 * world 0 -- the original game, and what every save and every build without a world
 * knows -- so a byte that was 0 before worlds existed already means 96/97.
 *
 * The world is chosen by `[general] world` in SWOS.ini (SDL); the Amiga reads no
 * SWOS.ini and plays 96/97. It is written into every save that holds teams or
 * competitions ("world"), and a save of another world is refused; online, the two
 * games compare it at the handshake (src/p2p_link.c) and the server lists a game only
 * to players of the same world (src/mm_client.c).
 */
#ifndef WORLD_H
#define WORLD_H

#include "swos_types.h"   /* the fixed-width types, on both targets */

typedef enum {
    WORLD_9697 = 0,     /* Sensible World of Soccer 96/97 */
    WORLD_2627,         /* the SWOS 96/97 Mod's World 2026-27 (docs/WORLD_2026_27.md): its data
                         * in data_json/worlds/2627/ over 96/97's, file by file and record by
                         * record (src/save_format_json.c); phase 3 holds Italy's leagues */
    WORLD_COUNT
} WorldId;

WorldId     World_Current(void);
const char *World_Tag(void);                 /* the current world's: "9697" */
const char *World_TagOf(WorldId w);          /* NULL for an unknown id */
int32_t     World_FromTag(const char *tag);  /* the id, or -1 */
int32_t     World_Set(const char *tag);      /* 0, or -1 (unknown: unchanged) */

#endif
