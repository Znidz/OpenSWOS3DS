/* The Mod's gp_season (tools/gplay.c, included as it is) over one season's tables. */
#include <stdio.h>
#include <stdlib.h>
#include GPLAY_C
const u8 SEC_TAB[1] = { 0 };            /* (no second teams: play-outs take them anyway) */
s8 SEC_SLT[4096], SEC_COPY[4096];
const u8 GP_PROM_TAB[1] = { 0 };
u16 GP_TOPW[9];
u8 GP_TOPR[9 * 684];
const u8 GP_HLT[4] = { 0 };
void mod_gp(const unsigned short (*rows)[32][6], const int *n, int nd, const u8 *sp, FILE *out)
{
    GP_IN.nd = (u8)nd;
    for (int d = 0; d < nd; d++) {
        GP_IN.n[d] = (u8)n[d];
        for (int k = 0; k < n[d]; k++) {
            struct gp_row *r = &GP_IN.t[d][k];
            r->team = rows[d][k][0]; r->glob = rows[d][k][1]; r->won = rows[d][k][2];
            r->gf = rows[d][k][3]; r->ga = rows[d][k][4]; r->pts = rows[d][k][5];
        }
    }
    GP_IN.spec = sp;
    gp_season();
    for (int i = 0; i < GP_OUT.nmv; i++) fprintf(out, " g%u:%d", GP_OUT.mv[i].glob, GP_OUT.mv[i].delta);
}
