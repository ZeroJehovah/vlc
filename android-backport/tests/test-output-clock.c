/* Run the imported VLC 4 algorithm with real pthread locks. */
#define N_(s) s
#include <vlc_common.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include "../../src/clock/clock.h"
const char vlc_module_name[] = "clock-test";

vlc_tick_t mdate(void) { return 100 * CLOCK_FREQ; }
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
}
