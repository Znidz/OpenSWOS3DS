/*
 * world.c -- which football world the game plays (include/world.h).
 */
#include "world.h"

static const char *const kTags[WORLD_COUNT] = {
    "9697",
};

static WorldId s_world = WORLD_9697;

WorldId World_Current(void)
{
    return s_world;
}

const char *World_TagOf(WorldId w)
{
    return (uint32_t)w < WORLD_COUNT ? kTags[w] : NULL;
}

const char *World_Tag(void)
{
    return kTags[s_world];
}

/* By hand: the Amiga build has no C library (-nostdlib). */
static int32_t same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

int32_t World_FromTag(const char *tag)
{
    if (tag)
        for (int32_t i = 0; i < WORLD_COUNT; i++)
            if (same(tag, kTags[i]))
                return i;
    return -1;
}

int32_t World_Set(const char *tag)
{
    const int32_t w = World_FromTag(tag);
    if (w < 0)
        return -1;
    s_world = (WorldId)w;
    return 0;
}
