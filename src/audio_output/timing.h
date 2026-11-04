/* Media/sample position adapter for the VLC 4 clock on the VLC 3 aout ABI.
 * Copyright (C) 2026 VLC authors
 * SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef AOUT_TIMING_H
#define AOUT_TIMING_H
#include <vlc_clock.h>

typedef struct aout_timing_segment aout_timing_segment;
typedef struct
{
    vlc_mutex_t lock;
    vlc_clock_t *clock; /* decoder owns it; detached after backend stop */
    aout_timing_segment *head, *tail;
    unsigned count, sample_rate;
    uint64_t samples, last_position;
    bool paused, master;
    vlc_tick_t cutoff, last_system, last_media, pause_date, next_log;
    double last_rate;
    double queued_rate;
} aout_timing_t;

void aout_TimingInit(aout_timing_t *);
void aout_TimingDestroy(aout_timing_t *);
void aout_TimingReset(aout_timing_t *, unsigned sample_rate);
bool aout_TimingQueue(aout_timing_t *, vlc_tick_t media, uint64_t samples, double rate);
void aout_TimingReport(audio_output_t *, vlc_tick_t system, uint64_t position);
bool aout_TimingUpdate(aout_timing_t *, vlc_tick_t system, uint64_t position);
#endif
