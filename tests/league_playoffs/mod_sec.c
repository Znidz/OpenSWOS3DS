/* The Mod's sec_post (tools/gplay.c, included as it is) on the cases of sec_cases.h. */
#include <stdio.h>
#include <stdlib.h>
#include GPLAY_C
#include "sec_cases.h"
/* 1 country; 14 levels; 3 B teams (global, first team's global, top 1, flags 3) */
const u8 SEC_TAB[] = { 1, 14, 0, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 212, 1, 173, 1, 1, 3, 235, 1, 174, 1, 1, 3, 1, 2, 168, 1, 1, 3 };
s8 SEC_SLT[4096], SEC_COPY[4096];
const u8 GP_PROM_TAB[1] = { 0 };
u16 GP_TOPW[9];
u8 GP_TOPR[9 * 684];
const u8 GP_HLT[4] = { 0 };
int main(int argc, char **argv)
{
    unsigned N = argc > 1 ? (unsigned)atoi(argv[1]) : 20000;
    for (unsigned it = 0; it < N; it++) {
        unsigned short glob[14][32]; int move[14][32];
        make_case(it, glob, move);
        GP_IN.nd = 14;
        for (int d = 0; d < 14; d++) {
            GP_IN.n[d] = (u8)SZ[d];
            for (int k = 0; k < SZ[d]; k++) {
                int g = glob[d][k];
                GP_IN.t[d][k].glob = (u16)g; GP_IN.t[d][k].team = (u16)g;
                SEC_SLT[g] = 0;
                /* a move in division terms, as the game's copy has it: up one level, down one
                 * (a level of groups: its first division), out of the lowest level: past it */
                int to = d;
                if (move[d][k] < 0) to = LVL[d] == 2 ? 1 : LVL[d] == 3 ? 2 : d - 1;
                if (move[d][k] > 0) to = LVL[d] == 1 ? 2 : LVL[d] == 2 ? 5 : LVL[d] == 3 ? 14 : d + 1;
                SEC_COPY[g] = (s8)(to - d);
            }
        }
        sec_post();
        printf("%u", it);
        for (int d = 0; d < 14; d++)
            for (int k = 0; k < SZ[d]; k++) {
                int g = glob[d][k], dl = SEC_COPY[g] - SEC_SLT[g];
                printf("%c", dl < 0 ? 'u' : dl > 0 ? 'd' : '.');
            }
        printf("\n");
    }
    return 0;
}
