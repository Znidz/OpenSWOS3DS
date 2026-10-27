/* league_playoffs check: random final tables for a league of 14 divisions (Italy 2026-27's
 * sizes), the same in both programs (their own LCG, not rand()). */
static const int SIZES[14] = { 20, 20, 20, 20, 20, 17, 17, 17, 18, 17, 18, 17, 17, 18 };
static unsigned tab_seed;
static unsigned tab_rand(unsigned n) { tab_seed = tab_seed * 1664525u + 1013904223u; return (tab_seed >> 8) % n; }
struct trow { unsigned short team, glob, won, gf, ga, pts; };
static void make_tables(unsigned it, struct trow t[14][32], int n[14])
{
    tab_seed = it * 2654435761u + 1;
    for (int d = 0; d < 14; d++) {
        int pts[32];
        n[d] = SIZES[d];
        for (int k = 0; k < n[d]; k++) pts[k] = 20 + tab_rand(60);
        for (int i = 0; i < n[d]; i++) for (int j = i + 1; j < n[d]; j++)
            if (pts[j] > pts[i]) { int x = pts[i]; pts[i] = pts[j]; pts[j] = x; }
        for (int k = 0; k < n[d]; k++) {
            struct trow *r = &t[d][k];
            r->team = (unsigned short)(0x1400 + d * 32 + k); r->glob = (unsigned short)(424 + d * 20 + k);
            r->pts = pts[k]; r->won = pts[k] / 3; r->gf = 30 + tab_rand(30); r->ga = 40;
        }
    }
}
static unsigned char spec[4096];
static void load_spec(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f || fread(spec, 1, sizeof spec, f) == 0) { fprintf(stderr, "no spec %s\n", path); exit(2); }
    fclose(f);
}
