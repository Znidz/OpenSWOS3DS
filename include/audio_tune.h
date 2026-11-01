/*
 * audio_tune.h -- the audio settings that can be changed while the game plays (Nintendo 3DS).
 *
 * The 3D checker's AUDIO page (src/n3ds_debug.c) lists them, changes them and saves them to
 * audio.ini in the game's folder, so crackle and tempo can be chased on the console without a
 * new build for each guess. Nothing here is match state.
 */
#ifndef AUDIO_TUNE_H
#define AUDIO_TUNE_H
#include <stdint.h>

typedef struct {
    const char    *name;
    int32_t       *value;       /* the live setting */
    const int32_t *choices;     /* what it can be, in order */
    int32_t        nChoices;
} AudioParam;

typedef struct {
    uint32_t queuedMs;          /* audio waiting in the queue now */
    uint32_t ticks;             /* 20 ms ticks mixed since the game started */
    uint32_t pads;              /* ticks of silence padded after one */
    uint32_t underruns;         /* the queue found empty */
    uint32_t rate, samples;     /* what the device gave us */
} AudioLive;

const AudioParam *AudioTune_Params(int32_t *n);
void              AudioTune_Changed(int32_t index);   /* a value changed: put it in force */
void              AudioTune_Live(AudioLive *out);
int32_t           AudioTune_Save(void);               /* 1 on success */
void              AudioTune_Load(void);
#endif
