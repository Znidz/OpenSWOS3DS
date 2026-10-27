#include <stdio.h>
#include <stdlib.h>
#include "tables.h"
void mod_dp(const unsigned short (*rows)[32][6], const int *n, unsigned short cupw, FILE *out);
void mod_gp(const unsigned short (*rows)[32][6], const int *n, int nd, const unsigned char *sp, FILE *out);
int main(int argc, char **argv)
{
    unsigned N = argc > 2 ? (unsigned)atoi(argv[2]) : 20000;
    struct trow t[14][32]; int n[14];
    unsigned short rows[14][32][6];
    load_spec(argv[1]);
    for (unsigned it = 0; it < N; it++) {
        make_tables(it, t, n);
        for (int d = 0; d < 14; d++) for (int k = 0; k < n[d]; k++) {
            rows[d][k][0] = t[d][k].team; rows[d][k][1] = t[d][k].glob; rows[d][k][2] = t[d][k].won;
            rows[d][k][3] = t[d][k].gf; rows[d][k][4] = t[d][k].ga; rows[d][k][5] = t[d][k].pts;
        }
        printf("%u", it);
        mod_dp((const unsigned short (*)[32][6])rows, n, cup_winner(it, t, n), stdout);
        mod_gp((const unsigned short (*)[32][6])rows, n, 14, spec, stdout);
        printf("\n");
    }
    return 0;
}
