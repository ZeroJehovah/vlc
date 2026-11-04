/* Run the imported VLC 4 algorithm with real pthread locks. */
#define N_(s) s
#include <vlc_common.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include "../../src/clock/clock.h"
#include <vlc_aout.h>
#include "../../src/audio_output/timing.h"
#include "../../src/input/clock.h"
const char vlc_module_name[] = "clock-test";

static vlc_tick_t test_now = 100 * CLOCK_FREQ;
vlc_tick_t mdate(void) { return test_now; }
void vlc_mutex_init(vlc_mutex_t *m) { assert(!pthread_mutex_init(m, NULL)); }
void vlc_mutex_destroy(vlc_mutex_t *m) { assert(!pthread_mutex_destroy(m)); }
void vlc_mutex_lock(vlc_mutex_t *m) { assert(!pthread_mutex_lock(m)); }
void vlc_mutex_unlock(vlc_mutex_t *m) { assert(!pthread_mutex_unlock(m)); }
void vlc_assert_locked(vlc_mutex_t *m) { assert(pthread_mutex_trylock(m) == EBUSY); }
void vlc_cond_init(vlc_cond_t *c) { assert(!pthread_cond_init(c, NULL)); }
void vlc_cond_destroy(vlc_cond_t *c) { assert(!pthread_cond_destroy(c)); }
void vlc_cond_broadcast(vlc_cond_t *c) { assert(!pthread_cond_broadcast(c)); }
void vlc_Log(vlc_object_t *o, int p, const char *m, const char *f,
             unsigned l, const char *fn, const char *fmt, ...)
{ (void)o; (void)p; (void)m; (void)f; (void)l; (void)fn; (void)fmt; }

static vlc_tick_t convert(vlc_clock_t *c, vlc_tick_t pts, double rate)
{
    vlc_clock_Lock(c);
    vlc_tick_t ts = vlc_clock_ConvertToSystem(c, mdate(), pts, rate, NULL);
    vlc_clock_Unlock(c);
    return ts;
}

static void test_audio_feedback(void)
{
    input_clock_t *input = input_clock_New(INPUT_RATE_DEFAULT);
    assert(input && input_clock_EnableOutput(input) == VLC_SUCCESS);
    bool late, master;
    input_clock_SetJitter(input, 300000, 10);
    input_clock_Update(input, NULL, &late, true, false, 10000000, test_now);
    input_clock_ChangeSystemOrigin(input, true, test_now);
    vlc_clock_t *a = input_clock_CreateOutput(input, AUDIO_ES, &master);
    assert(a && master);
    vlc_clock_t *v = input_clock_CreateOutput(input, VIDEO_ES, &master);
    assert(v && !master);
    aout_timing_t t;
    aout_TimingInit(&t);
    t.clock = a;
    t.master = true;
    aout_TimingReset(&t, 48000);
    assert(aout_TimingQueue(&t, 10000000, 48000, 1.));
    assert(aout_TimingQueue(&t, 11000000, 48000, 2.));
    vlc_mutex_lock(&t.lock);
    assert(!aout_TimingUpdate(&t, 99999999, 1)); /* stale before start */
    assert(!aout_TimingUpdate(&t, 100010000, 96001)); /* impossible position */
    assert(aout_TimingUpdate(&t, 100500000, 24000));
    assert(t.last_media == 10500000);
    assert(!aout_TimingUpdate(&t, 100700000, 24000)); /* stopped head */
    assert(aout_TimingUpdate(&t, 101500000, 72000));
    assert(t.last_media == 12000000); /* queued 2x starts at frame 48000 */
    vlc_mutex_unlock(&t.lock);
    assert(convert(v, 13000000, 2.) == 102000000);

    input_clock_ChangePause(input, true, 102000000);
    t.paused = true;
    vlc_mutex_lock(&t.lock);
    assert(!aout_TimingUpdate(&t, 102100000, 80000));
    vlc_mutex_unlock(&t.lock);
    input_clock_ChangePause(input, false, 107000000);
    t.paused = false;
    t.last_system += 5000000;
    t.cutoff = 107000000;
    vlc_mutex_lock(&t.lock);
    assert(!aout_TimingUpdate(&t, 101900000, 90000));
    assert(aout_TimingUpdate(&t, 107300000, 96000));
    vlc_mutex_unlock(&t.lock);
    assert(convert(v, 15000000, 2.) == 108300000);

    /* Full program + backend flush during pause, then new media positions. */
    input_clock_ChangePause(input, true, 108000000);
    t.paused = true;
    test_now = 110000000;
    input_clock_Reset(input);
    aout_TimingReset(&t, 48000);
    assert(t.paused && t.samples == 0);
    input_clock_Update(input, NULL, &late, true, false, 30000000, test_now);
    input_clock_ChangeSystemOrigin(input, true, test_now);
    input_clock_ChangePause(input, false, 111000000);
    t.paused = false;
    t.cutoff = 111000000;
    assert(aout_TimingQueue(&t, 30000000, 48000, 1.));
    vlc_mutex_lock(&t.lock);
    assert(!aout_TimingUpdate(&t, 109000000, 24000));
    assert(aout_TimingUpdate(&t, 111500000, 24000));
    vlc_mutex_unlock(&t.lock);
    assert(convert(v, 31000000, 1.) == 112000000);
    aout_TimingDestroy(&t);
    input_clock_DeleteOutput(input, a);
    a = input_clock_CreateOutput(input, AUDIO_ES, &master);
    assert(a && master);
    input_clock_DeleteOutput(input, a);
    input_clock_DeleteOutput(input, v);
    input_clock_Delete(input);
    puts("PASS: actual input lifecycle + audio feedback, stale/invalid position rejection, queued rate transition, paused seek");
}

int main(void)
{
    vlc_clock_main_t *m = vlc_clock_main_New(NULL, NULL);
    assert(m);
    vlc_clock_main_Lock(m);
    vlc_clock_main_SetFirstPcr(m, 100000000, 10000000);
    vlc_clock_t *a = vlc_clock_main_CreateMaster(m, "audio", NULL, NULL);
    vlc_clock_t *v = vlc_clock_main_CreateSlave(m, "video", VIDEO_ES, NULL, NULL);
    assert(a && v);
    vlc_clock_main_Unlock(m);
    vlc_clock_Lock(a);
    vlc_clock_Update(a, 100000000, 10000000, 1.);
    vlc_clock_Unlock(a);
    assert(convert(v, 11000000, 1.) == 101000000);

    /* Audio feedback must update already queued video's future deadline. */
    vlc_clock_Lock(a);
    vlc_clock_Update(a, 101250000, 11000000, 1.);
    vlc_clock_Unlock(a);
    assert(convert(v, 12000000, 1.) == 102250000);
    vlc_clock_main_Lock(m);
    vlc_clock_main_ChangePause(m, 101500000, true);
    vlc_clock_main_ChangePause(m, 106500000, false);
    vlc_clock_main_Unlock(m);
    assert(convert(v, 12000000, 1.) == 107250000);
    vlc_clock_Lock(a);
    vlc_clock_Update(a, 107250000, 12000000, 1.);
    vlc_clock_Unlock(a);
    assert(convert(v, 13000000, 1.) == 108250000);

    /* A seek while paused must not apply the old five-second pause twice. */
    vlc_clock_main_Lock(m);
    vlc_clock_main_ChangePause(m, 108000000, true);
    vlc_clock_main_Reset(m);
    vlc_clock_main_SetFirstPcr(m, 110000000, 30000000);
    vlc_clock_main_ChangePause(m, 113000000, false);
    vlc_clock_main_Unlock(m);
    vlc_clock_Lock(a);
    vlc_clock_Reset(a);
    vlc_clock_Update(a, 113000000, 30000000, 1.);
    vlc_clock_Unlock(a);
    assert(convert(v, 31000000, 1.) == 114000000);
    vlc_clock_Lock(a);
    vlc_clock_Update(a, 114000000, 31000000, 2.);
    vlc_clock_Unlock(a);
    assert(convert(v, 33000000, 2.) == 115000000);
    vlc_clock_Delete(a);
    vlc_clock_main_Lock(m);
    a = vlc_clock_main_CreateMaster(m, "replacement", NULL, NULL);
    vlc_clock_main_Unlock(m);
    assert(a);
    vlc_clock_Delete(a);
    vlc_clock_Delete(v);
    vlc_clock_main_Delete(m);
    puts("PASS: shared A/V feedback, pause, paused seek, rate, master replacement");
    test_audio_feedback();
}
