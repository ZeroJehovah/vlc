/*****************************************************************************
 * aaudio.c: Android AAudio output for the VLC 3 audio_output_t interface
 *****************************************************************************
 * Copyright (C) 2018-2022 VLC authors, VideoLAN and VideoLabs
 * Copyright (C) 2026 VLC AAudio backport contributors
 *
 * Inspired by the VLC 4 AAudio module by Romain Vimont and Thomas Guillem.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_aout.h>
#include <vlc_atomic.h>
#include <aaudio/AAudio.h>
#include <dlfcn.h>
#include <time.h>

/* No link-time AAudio dependency: the APK remains loadable on older Android. */
#define AAUDIO_FUNCTIONS(X) \
    X(aaudio_result_t, AAudio_createStreamBuilder, (AAudioStreamBuilder **)) \
    X(const char *, AAudio_convertResultToText, (aaudio_result_t)) \
    X(void, AAudioStreamBuilder_setFormat, (AAudioStreamBuilder *, aaudio_format_t)) \
    X(void, AAudioStreamBuilder_setSampleRate, (AAudioStreamBuilder *, int32_t)) \
    X(void, AAudioStreamBuilder_setChannelCount, (AAudioStreamBuilder *, int32_t)) \
    X(void, AAudioStreamBuilder_setSessionId, (AAudioStreamBuilder *, aaudio_session_id_t)) \
    X(void, AAudioStreamBuilder_setUsage, (AAudioStreamBuilder *, aaudio_usage_t)) \
    X(void, AAudioStreamBuilder_setPerformanceMode, (AAudioStreamBuilder *, aaudio_performance_mode_t)) \
    X(void, AAudioStreamBuilder_setDataCallback, (AAudioStreamBuilder *, AAudioStream_dataCallback, void *)) \
    X(void, AAudioStreamBuilder_setErrorCallback, (AAudioStreamBuilder *, AAudioStream_errorCallback, void *)) \
    X(aaudio_result_t, AAudioStreamBuilder_openStream, (AAudioStreamBuilder *, AAudioStream **)) \
    X(aaudio_result_t, AAudioStreamBuilder_delete, (AAudioStreamBuilder *)) \
    X(int32_t, AAudioStream_getSampleRate, (AAudioStream *)) \
    X(int32_t, AAudioStream_getChannelCount, (AAudioStream *)) \
    X(aaudio_format_t, AAudioStream_getFormat, (AAudioStream *)) \
    X(aaudio_result_t, AAudioStream_getTimestamp, (AAudioStream *, clockid_t, int64_t *, int64_t *)) \
    X(aaudio_stream_state_t, AAudioStream_getState, (AAudioStream *)) \
    X(aaudio_result_t, AAudioStream_waitForStateChange, (AAudioStream *, aaudio_stream_state_t, aaudio_stream_state_t *, int64_t)) \
    X(aaudio_result_t, AAudioStream_requestStart, (AAudioStream *)) \
    X(aaudio_result_t, AAudioStream_requestPause, (AAudioStream *)) \
    X(aaudio_result_t, AAudioStream_requestStop, (AAudioStream *)) \
    X(aaudio_result_t, AAudioStream_close, (AAudioStream *))

struct aout_sys_t
{
    void *library;
#define DECLARE(result, name, args) result (*name) args;
    AAUDIO_FUNCTIONS(DECLARE)
#undef DECLARE
    /* Optional API 32 channel masks. */
    void (*AAudioStreamBuilder_setChannelMask)(AAudioStreamBuilder *, aaudio_channel_mask_t);
    AAudioStream *stream;
    audio_sample_format_t fmt;
    vlc_mutex_t lock;
    vlc_cond_t cond;
    block_t *queue;
    block_t **tail;
    block_t *retired;
    uint64_t queued_frames;
    int64_t written_frames;
    int64_t media_end_frame;
    vlc_tick_t first_date;
    vlc_tick_t timestamp_after;
    bool requested;
    bool paused;
    bool started;
    bool rate_fixed;
    atomic_bool failed;
    atomic_bool closing;
    uint8_t reorder;
    uint8_t chan_table[AOUT_CHAN_MAX];
    float soft_gain;
    bool soft_mute;
};

#include "volume.h"

static int Open(vlc_object_t *);
static void Close(vlc_object_t *);
vlc_module_begin()
    set_shortname("AAudio")
    set_description("Android AAudio output (VLC 3 backport)")
    set_category(CAT_AUDIO)
    set_subcategory(SUBCAT_AUDIO_AOUT)
    /* Preserve the stable release's default AudioTrack/OpenSL ES ordering. */
    set_capability("audio output", 100)
    add_shortcut("aaudio")
    add_sw_gain()
    set_callbacks(Open, Close)
vlc_module_end()

static vlc_tick_t FramesToTicks(aout_sys_t *sys, int64_t frames)
{
    return frames * CLOCK_FREQ / sys->fmt.i_rate;
}

static void Fail(audio_output_t *aout, const char *operation, aaudio_result_t error)
{
    aout_sys_t *sys = aout->sys;
    if (atomic_load(&sys->closing))
        return;
    if (!atomic_exchange(&sys->failed, true))
    {
        msg_Err(aout, "AAudio %s: %s", operation, sys->AAudio_convertResultToText(error));
        /* Closing from an AAudio callback may deadlock. Let VLC restart us. */
        aout_RestartRequest(aout, AOUT_RESTART_OUTPUT);
    }
}

static aaudio_data_callback_result_t DataCallback(AAudioStream *stream, void *user,
                                                  void *data, int32_t frames)
{
    (void) stream;
    audio_output_t *aout = user;
    aout_sys_t *sys = aout->sys;
    size_t frame_bytes = sys->fmt.i_bytes_per_frame;
    uint8_t *dst = data;
    memset(data, 0, (size_t) frames * frame_bytes);
    if (atomic_load(&sys->closing) || atomic_load(&sys->failed))
        return AAUDIO_CALLBACK_RESULT_STOP;

    vlc_mutex_lock(&sys->lock);
    int32_t offset = 0;
    if (!sys->paused && !sys->started && sys->first_date != VLC_TICK_INVALID)
    {
        vlc_tick_t advance = sys->first_date - mdate();
        if (advance > 0)
        {
            int64_t silence = advance * sys->fmt.i_rate / CLOCK_FREQ;
            offset = silence < frames ? silence : frames;
        }
    }
    if (!sys->paused && sys->first_date != VLC_TICK_INVALID)
    {
        while (offset < frames && sys->queue != NULL)
        {
            block_t *b = sys->queue;
            size_t count = b->i_buffer / frame_bytes;
            if (count > (size_t)(frames - offset))
                count = frames - offset;
            size_t bytes = count * frame_bytes;
            memcpy(dst + offset * frame_bytes, b->p_buffer, bytes);
            b->p_buffer += bytes;
            b->i_buffer -= bytes;
            offset += count;
            sys->queued_frames -= count;
            sys->media_end_frame = sys->written_frames + offset;
            sys->started = true;
            if (b->i_buffer == 0)
            {
                sys->queue = b->p_next;
                if (sys->queue == NULL)
                    sys->tail = &sys->queue;
                /* Free on the VLC producer thread, outside the RT callback. */
                b->p_next = sys->retired;
                sys->retired = b;
            }
        }
    }
    /* Includes silence so frame indices match AAudio's monotonic frame clock.
     * media_end_frame deliberately excludes silence after an underrun. */
    sys->written_frames += frames;
    if (sys->queue == NULL)
        vlc_cond_signal(&sys->cond);
    vlc_mutex_unlock(&sys->lock);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void ErrorCallback(AAudioStream *stream, void *user, aaudio_result_t error)
{
    (void) stream;
    Fail(user, "stream error/disconnected", error);
}

static int WaitState(audio_output_t *aout, aaudio_stream_state_t target)
{
    aout_sys_t *sys = aout->sys;
    vlc_tick_t deadline = mdate() + CLOCK_FREQ;
    aaudio_stream_state_t state = sys->AAudioStream_getState(sys->stream);
    while (state != target && !atomic_load(&sys->failed))
    {
        vlc_tick_t remaining = deadline - mdate();
        if (remaining <= 0)
            return VLC_EGENERIC;
        aaudio_result_t result = sys->AAudioStream_waitForStateChange(sys->stream,
                                      state, &state, remaining * 1000);
        if (result != AAUDIO_OK)
            return VLC_EGENERIC;
    }
    return state == target ? VLC_SUCCESS : VLC_EGENERIC;
}

static aaudio_channel_mask_t ChannelMask(uint16_t channels)
{
    aaudio_channel_mask_t mask = 0;
    if (channels & AOUT_CHAN_LEFT) mask |= AAUDIO_CHANNEL_FRONT_LEFT;
    if (channels & AOUT_CHAN_RIGHT) mask |= AAUDIO_CHANNEL_FRONT_RIGHT;
    if (channels & AOUT_CHAN_CENTER) mask |= AAUDIO_CHANNEL_FRONT_CENTER;
    if (channels & AOUT_CHAN_LFE) mask |= AAUDIO_CHANNEL_LOW_FREQUENCY;
    if (channels & AOUT_CHAN_REARLEFT) mask |= AAUDIO_CHANNEL_BACK_LEFT;
    if (channels & AOUT_CHAN_REARRIGHT) mask |= AAUDIO_CHANNEL_BACK_RIGHT;
    if (channels & AOUT_CHAN_REARCENTER) mask |= AAUDIO_CHANNEL_BACK_CENTER;
    if (channels & AOUT_CHAN_MIDDLELEFT) mask |= AAUDIO_CHANNEL_SIDE_LEFT;
    if (channels & AOUT_CHAN_MIDDLERIGHT) mask |= AAUDIO_CHANNEL_SIDE_RIGHT;
    return mask;
}

static int OpenStream(audio_output_t *aout)
{
    aout_sys_t *sys = aout->sys;
    AAudioStreamBuilder *builder = NULL;
    aaudio_result_t result = sys->AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK)
        return VLC_EGENERIC;
    sys->AAudioStreamBuilder_setFormat(builder, sys->fmt.i_format == VLC_CODEC_S16N
                                       ? AAUDIO_FORMAT_PCM_I16 : AAUDIO_FORMAT_PCM_FLOAT);
    if (sys->rate_fixed)
        sys->AAudioStreamBuilder_setSampleRate(builder, sys->fmt.i_rate);
    if (sys->AAudioStreamBuilder_setChannelMask != NULL)
        sys->AAudioStreamBuilder_setChannelMask(builder, ChannelMask(sys->fmt.i_physical_channels));
    else
        sys->AAudioStreamBuilder_setChannelCount(builder, sys->fmt.i_channels);
    int32_t session = var_InheritInteger(aout, "audiotrack-session-id");
    sys->AAudioStreamBuilder_setSessionId(builder, session > 0 ? session : AAUDIO_SESSION_ID_ALLOCATE);
    sys->AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_MEDIA);
    sys->AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
    sys->AAudioStreamBuilder_setDataCallback(builder, DataCallback, aout);
    sys->AAudioStreamBuilder_setErrorCallback(builder, ErrorCallback, aout);
    result = sys->AAudioStreamBuilder_openStream(builder, &sys->stream);
    sys->AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK)
    {
        msg_Err(aout, "AAudio open: %s", sys->AAudio_convertResultToText(result));
        sys->stream = NULL;
        return VLC_EGENERIC;
    }
    if (sys->AAudioStream_getChannelCount(sys->stream) != (int32_t)sys->fmt.i_channels
     || sys->AAudioStream_getFormat(sys->stream) != (sys->fmt.i_format == VLC_CODEC_S16N
                                           ? AAUDIO_FORMAT_PCM_I16 : AAUDIO_FORMAT_PCM_FLOAT))
    {
        sys->AAudioStream_close(sys->stream);
        sys->stream = NULL;
        return VLC_EGENERIC;
    }
    sys->fmt.i_rate = sys->AAudioStream_getSampleRate(sys->stream);
    sys->rate_fixed = true;
    return sys->fmt.i_rate > 0 ? VLC_SUCCESS : VLC_EGENERIC;
}

static void CloseStream(audio_output_t *aout)
{
    aout_sys_t *sys = aout->sys;
    if (sys->stream == NULL)
        return;
    atomic_store(&sys->closing, true);
    if (!atomic_load(&sys->failed) && sys->requested)
    {
        if (sys->AAudioStream_requestStop(sys->stream) == AAUDIO_OK)
            WaitState(aout, AAUDIO_STREAM_STATE_STOPPED);
    }
    /* close() waits for callbacks; the queue and callback context stay alive. */
    sys->AAudioStream_close(sys->stream);
    sys->stream = NULL;
}

static void ResetQueue(aout_sys_t *sys)
{
    block_ChainRelease(sys->queue);
    block_ChainRelease(sys->retired);
    sys->queue = sys->retired = NULL;
    sys->tail = &sys->queue;
    sys->queued_frames = 0;
    sys->written_frames = sys->media_end_frame = 0;
    sys->first_date = VLC_TICK_INVALID;
    sys->timestamp_after = mdate();
    sys->requested = sys->started = false;
}

static int TimeGet(audio_output_t *aout, vlc_tick_t *delay)
{
    aout_sys_t *sys = aout->sys;
    if (sys->stream == NULL || atomic_load(&sys->failed))
        return -1;
    vlc_mutex_lock(&sys->lock);
    int64_t position, nanoseconds;
    aaudio_result_t result = sys->AAudioStream_getTimestamp(sys->stream,
                                      CLOCK_MONOTONIC, &position, &nanoseconds);
    vlc_tick_t now = mdate();
    /* AAudio can expose the old timestamp until its first post-resume update.
     * Reporting that as a delay triggers VLC 3's silence/flush correction. */
    if (!sys->started || sys->paused || result != AAUDIO_OK || position < 0
     || nanoseconds / 1000 < sys->timestamp_after || nanoseconds / 1000 > now)
    {
        vlc_mutex_unlock(&sys->lock);
        return -1;
    }
    int64_t played = position + (now - nanoseconds / 1000) * sys->fmt.i_rate / CLOCK_FREQ;
    if (played > sys->written_frames)
        played = sys->written_frames;
    int64_t pending = sys->media_end_frame - played;
    if (pending < 0) pending = 0;
    *delay = FramesToTicks(sys, sys->queued_frames + pending);
    vlc_mutex_unlock(&sys->lock);
    return 0;
}

static void Play(audio_output_t *aout, block_t *block)
{
    aout_sys_t *sys = aout->sys;
    if (sys->stream == NULL || atomic_load(&sys->failed))
    {
        block_Release(block);
        return;
    }
    if (sys->reorder != 0)
        aout_ChannelReorder(block->p_buffer, block->i_buffer, sys->reorder,
                            sys->chan_table, sys->fmt.i_format);
    vlc_mutex_lock(&sys->lock);
    if (sys->first_date == VLC_TICK_INVALID)
        sys->first_date = block->i_pts;
    block->p_next = NULL;
    *sys->tail = block;
    sys->tail = &block->p_next;
    sys->queued_frames += block->i_buffer / sys->fmt.i_bytes_per_frame;
    block_t *retired = sys->retired;
    sys->retired = NULL;
    bool start = !sys->requested && !sys->paused;
    if (start) sys->requested = true;
    vlc_mutex_unlock(&sys->lock);
    block_ChainRelease(retired);
    if (start)
    {
        aaudio_result_t result = sys->AAudioStream_requestStart(sys->stream);
        if (result != AAUDIO_OK) Fail(aout, "start", result);
    }
}

static void Pause(audio_output_t *aout, bool paused, vlc_tick_t date)
{
    (void) date;
    aout_sys_t *sys = aout->sys;
    if (sys->stream == NULL || atomic_load(&sys->failed))
        return;
    vlc_mutex_lock(&sys->lock);
    sys->paused = paused;
    bool requested = sys->requested;
    if (!paused)
    {
        sys->timestamp_after = mdate();
        if (!sys->started && sys->first_date != VLC_TICK_INVALID)
            sys->first_date = mdate();
    }
    vlc_mutex_unlock(&sys->lock);
    if (!requested)
        return; /* Play will start an empty stream after resume. */
    aaudio_result_t result = paused ? sys->AAudioStream_requestPause(sys->stream)
                                   : sys->AAudioStream_requestStart(sys->stream);
    if (result != AAUDIO_OK)
        Fail(aout, paused ? "pause" : "resume", result);
    else if (paused && WaitState(aout, AAUDIO_STREAM_STATE_PAUSED) != VLC_SUCCESS)
        Fail(aout, "wait for pause", AAUDIO_ERROR_TIMEOUT);
}

static void Flush(audio_output_t *aout, bool drain)
{
    aout_sys_t *sys = aout->sys;
    if (drain && sys->stream != NULL && !atomic_load(&sys->failed))
    {
        vlc_tick_t remaining_delay;
        bool timed = TimeGet(aout, &remaining_delay) == 0;
        vlc_mutex_lock(&sys->lock);
        vlc_tick_t deadline = mdate() + (timed ? remaining_delay
                            : FramesToTicks(sys, sys->queued_frames)) + 2 * CLOCK_FREQ;
        /* Poll in short waits: delay is presentation time, not FIFO emptiness. */
        while (!sys->paused && !atomic_load(&sys->failed) && mdate() < deadline)
        {
            bool empty = sys->queued_frames == 0;
            vlc_mutex_unlock(&sys->lock);
            vlc_tick_t delay;
            bool done = empty && TimeGet(aout, &delay) == 0 && delay == 0;
            vlc_mutex_lock(&sys->lock);
            if (done || (!sys->requested && empty)) break;
            vlc_cond_timedwait(&sys->cond, &sys->lock, mdate() + CLOCK_FREQ / 50);
        }
        vlc_mutex_unlock(&sys->lock);
        return;
    }
    /* Reopen rather than requestFlush: some Android releases reset the frame
     * position inconsistently on flush. Pause/resume itself retains the stream. */
    CloseStream(aout);
    ResetQueue(sys);
    atomic_store(&sys->failed, false);
    atomic_store(&sys->closing, false);
    if (OpenStream(aout) != VLC_SUCCESS)
        Fail(aout, "reopen after flush", AAUDIO_ERROR_UNAVAILABLE);
}

static int Start(audio_output_t *aout, audio_sample_format_t *fmt)
{
    aout_sys_t *sys = aout->sys;
    sys->fmt = *fmt;
    sys->fmt.channel_type = AUDIO_CHANNEL_TYPE_BITMAP;
    if (sys->fmt.i_format != VLC_CODEC_S16N)
        sys->fmt.i_format = VLC_CODEC_FL32;
    unsigned channels = aout_FormatNbChannels(&sys->fmt);
    if (sys->AAudioStreamBuilder_setChannelMask == NULL)
        sys->fmt.i_physical_channels = channels > 7 ? AOUT_CHANS_7_1
                                      : channels > 5 ? AOUT_CHANS_5_1
                                      : channels == 1 ? AOUT_CHAN_CENTER : AOUT_CHANS_STEREO;
    if (sys->fmt.i_physical_channels == 0)
        sys->fmt.i_physical_channels = AOUT_CHANS_STEREO;
    aout_FormatPrepare(&sys->fmt);
    static const uint32_t android_order[] = {
        AOUT_CHAN_LEFT, AOUT_CHAN_RIGHT, AOUT_CHAN_CENTER, AOUT_CHAN_LFE,
        AOUT_CHAN_REARLEFT, AOUT_CHAN_REARRIGHT, AOUT_CHAN_REARCENTER,
        AOUT_CHAN_MIDDLELEFT, AOUT_CHAN_MIDDLERIGHT, 0
    };
    sys->reorder = aout_CheckChannelReorder(NULL, android_order,
                            sys->fmt.i_physical_channels, sys->chan_table);
    sys->paused = false;
    sys->rate_fixed = false;
    atomic_store(&sys->failed, false);
    atomic_store(&sys->closing, false);
    ResetQueue(sys);
    if (OpenStream(aout) != VLC_SUCCESS)
    {
        CloseStream(aout);
        return VLC_EGENERIC;
    }
    *fmt = sys->fmt;
    aout_SoftVolumeStart(aout);
    msg_Dbg(aout, "AAudio PCM: %u Hz, %u channels", fmt->i_rate, fmt->i_channels);
    return VLC_SUCCESS;
}

static void Stop(audio_output_t *aout)
{
    CloseStream(aout);
    ResetQueue(aout->sys);
}

static int Open(vlc_object_t *obj)
{
    audio_output_t *aout = (audio_output_t *)obj;
    aout_sys_t *sys = calloc(1, sizeof(*sys));
    if (sys == NULL) return VLC_ENOMEM;
    sys->library = dlopen("libaaudio.so", RTLD_NOW | RTLD_LOCAL);
    if (sys->library == NULL) goto error;
#define LOAD(result, name, args) \
    do { *(void **)(&sys->name) = dlsym(sys->library, #name); \
         if (sys->name == NULL) goto error; } while (0);
    /* setSessionId is required to exclude Android 8's stream lifetime bugs. */
    AAUDIO_FUNCTIONS(LOAD)
#undef LOAD
    *(void **)(&sys->AAudioStreamBuilder_setChannelMask) =
        dlsym(sys->library, "AAudioStreamBuilder_setChannelMask");
    atomic_init(&sys->failed, false);
    atomic_init(&sys->closing, false);
    vlc_mutex_init(&sys->lock);
    vlc_cond_init(&sys->cond);
    sys->tail = &sys->queue;
    aout->sys = sys;
    aout->start = Start;
    aout->stop = Stop;
    aout->play = Play;
    aout->pause = Pause;
    aout->flush = Flush;
    aout->time_get = TimeGet;
    aout_SoftVolumeInit(aout);
    return VLC_SUCCESS;
error:
    if (sys->library != NULL) dlclose(sys->library);
    free(sys);
    return VLC_EGENERIC;
}

static void Close(vlc_object_t *obj)
{
    audio_output_t *aout = (audio_output_t *)obj;
    aout_sys_t *sys = aout->sys;
    Stop(aout);
    vlc_cond_destroy(&sys->cond);
    vlc_mutex_destroy(&sys->lock);
    dlclose(sys->library);
    free(sys);
}
