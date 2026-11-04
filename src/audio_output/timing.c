/* Copyright (C) 2026 VLC authors
 * SPDX-License-Identifier: LGPL-2.1-or-later */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_aout.h>
#include <assert.h>
#include "aout_internal.h"
#include "../clock/clock.h"

struct aout_timing_segment
{
    aout_timing_segment *next;
    uint64_t start, end;
    vlc_tick_t media;
    double rate;
};

void aout_TimingInit(aout_timing_t *t)
{
    memset(t, 0, sizeof(*t));
    vlc_mutex_init(&t->lock);
}

/* Caller holds timing lock; backend has been quiesced before resetting. */
void aout_TimingReset(aout_timing_t *t, unsigned sample_rate)
{
    while(t->head)
    {
        aout_timing_segment *next = t->head->next;
        free(t->head);
        t->head = next;
    }
    t->tail = NULL;
    t->count = 0;
    t->samples = t->last_position = 0;
    t->sample_rate = sample_rate;
    t->cutoff = mdate();
    t->last_system = t->last_media = VLC_TICK_INVALID;
    t->next_log = 0;
    t->last_rate = t->queued_rate = 1.;
    /* Preserve paused state across seek, like vlc_clock_main_Reset(). */
}

void aout_TimingDestroy(aout_timing_t *t)
{
    aout_TimingReset(t, 0);
    vlc_mutex_destroy(&t->lock);
}

bool aout_TimingQueue(aout_timing_t *t, vlc_tick_t media, uint64_t samples,
                      double rate)
{
    vlc_mutex_lock(&t->lock);
    aout_timing_segment *p = t->tail;
    const vlc_tick_t expected = p ? p->media +
        (vlc_tick_t)((p->end - p->start) * (double)CLOCK_FREQ * p->rate / t->sample_rate) : 0;
    if (p && p->rate == rate && llabs(expected - media) <= 2)
        p->end += samples;
    else
    {
        p = malloc(sizeof(*p));
        if (!p)
        {
            vlc_mutex_unlock(&t->lock);
            return false;
        }
        *p = (aout_timing_segment) { .start = t->samples,
            .end = t->samples + samples, .media = media, .rate = rate };
        if (t->tail)
            t->tail->next = p;
        else
            t->head = p;
        t->tail = p;
        t->count++;
        /* Bound memory if a backend never produces valid timing. */
        if (t->count > 1024)
        {
            p = t->head;
            t->head = p->next;
            free(p);
            t->count--;
        }
    }
    t->samples += samples;
    t->queued_rate = rate;
    vlc_mutex_unlock(&t->lock);
    return true;
}

/* The backend reports positions already consumed, never an extrapolated head.
 * This adapter also handles rate changes while old-rate PCM is still queued. */
bool aout_TimingUpdate(aout_timing_t *t, vlc_tick_t system, uint64_t position)
{
    if (!t->clock || t->paused || !t->head || !t->sample_rate ||
        system < t->cutoff || system <= t->last_system ||
        position < t->head->start || position > t->samples ||
        (t->last_system != VLC_TICK_INVALID && position <= t->last_position))
        return false;

    while(t->head->next && position >= t->head->next->start)
    {
        aout_timing_segment *p = t->head;
        t->head = p->next;
        free(p);
        t->count--;
    }
    aout_timing_segment *p = t->head;
    if (position > p->end)
        return false;
    vlc_tick_t media = p->media + (vlc_tick_t)((position - p->start) *
                       (double)CLOCK_FREQ * p->rate / t->sample_rate);
    vlc_clock_Lock(t->clock);
    if (vlc_clock_IsPaused(t->clock))
    {
        vlc_clock_Unlock(t->clock);
        return false;
    }
    vlc_clock_Update(t->clock, system, media, p->rate);
    vlc_clock_Unlock(t->clock);
    t->last_system = system;
    t->last_media = media;
    t->last_position = position;
    t->last_rate = p->rate;
    return true;
}

void aout_TimingReport(audio_output_t *aout, vlc_tick_t system, uint64_t position)
{
    aout_timing_t *t = &aout_owner(aout)->timing;
    vlc_mutex_lock(&t->lock);
    if (aout_TimingUpdate(t, system, position) && system >= t->next_log)
    {
        msg_Dbg(aout, "[v4-clock] feedback system_us=%"PRId64
                " media_us=%"PRId64" position=%"PRIu64" queued=%"PRIu64" rate=%f",
                system, t->last_media, position, t->samples - position, t->last_rate);
        t->next_log = system + CLOCK_FREQ;
    }
    vlc_mutex_unlock(&t->lock);
}
