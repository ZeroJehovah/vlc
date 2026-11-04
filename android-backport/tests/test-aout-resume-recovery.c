/* Exercise the real 3.x audio synchronization decision. */
#define N_(s) s
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include AOUT_DEC_SOURCE

static vlc_tick_t fake_now = 100 * CLOCK_FREQ;
static vlc_tick_t fake_delay;
static unsigned flush_count, play_count;

vlc_tick_t mdate(void)
{
    return fake_now;
}

int aout_OutputTimeGet(audio_output_t *aout, vlc_tick_t *delay)
{
    (void)aout;
    *delay = fake_delay;
    return 0;
}

void aout_OutputFlush(audio_output_t *aout, bool wait)
{
    (void)aout;
    (void)wait;
    flush_count++;
}

void aout_OutputPlay(audio_output_t *aout, block_t *block)
{
    (void)aout;
    block_Release(block);
    play_count++;
}

block_t *block_Alloc(size_t size)
{
    (void)size;
    return NULL;
}

bool aout_FiltersCanResample(aout_filters_t *filters)
{
    (void)filters;
    return true;
}

bool aout_FiltersAdjustResampling(aout_filters_t *filters, int adjust)
{
    (void)filters;
    (void)adjust;
    return true;
}

void vlc_Log(vlc_object_t *o, int priority, const char *module,
             const char *file, unsigned line, const char *function,
             const char *format, ...)
{
    (void)o; (void)priority; (void)module; (void)file; (void)line;
    (void)function; (void)format;
}

static void Reset(aout_instance_t *instance, bool recovering)
{
    flush_count = play_count = 0;
    instance->owner.sync.resume_recovery = recovering;
    instance->owner.sync.resume_date = fake_now;
    instance->owner.sync.resume_stable_samples = 0;
    instance->owner.sync.discontinuity = false;
    instance->owner.sync.resamp_type = AOUT_RESAMPLING_NONE;
    instance->owner.mixer_format.i_format = VLC_CODEC_S16N;
    instance->owner.mixer_format.i_rate = 48000;
    instance->owner.mixer_format.i_bytes_per_frame = 4;
    instance->owner.mixer_format.i_frame_length = 1;
}

int main(void)
{
    aout_instance_t instance = {0};
    audio_output_t *aout = &instance.output;

    /* Both real phone anomalies must use the non-destructive recovery path. */
    fake_delay = 2 * CLOCK_FREQ;
    Reset(&instance, true);
    vlc_tick_t pts = fake_now + fake_delay - CLOCK_FREQ * 316 / 1000;
    aout_DecSynchronize(aout, pts, INPUT_RATE_DEFAULT);
    assert(flush_count == 0);

    fake_delay = 0;
    Reset(&instance, true);
    pts = fake_now + CLOCK_FREQ * 17 / 10;
    aout_DecSynchronize(aout, pts, INPUT_RATE_DEFAULT);
    assert(play_count == 0);

    /* Outside recovery, the legacy destructive correction remains intact. */
    fake_delay = 2 * CLOCK_FREQ;
    Reset(&instance, false);
    pts = fake_now + fake_delay - CLOCK_FREQ * 316 / 1000;
    aout_DecSynchronize(aout, pts, INPUT_RATE_DEFAULT);
    assert(flush_count == 1);

    puts("PASS: resume recovery defers late flush and early silence; normal correction remains");
    return 0;
}
