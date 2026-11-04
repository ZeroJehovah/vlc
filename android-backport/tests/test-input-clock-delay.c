/* Exercise the shared stream->system mapping used by resume-fix5. */
#define N_(s) s
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include CLOCK_SOURCE

vlc_tick_t mdate(void)
{
    return 100 * CLOCK_FREQ;
}

void vlc_mutex_init(vlc_mutex_t *mutex)
{
    (void) mutex;
}

void vlc_mutex_destroy(vlc_mutex_t *mutex)
{
    (void) mutex;
}

void vlc_mutex_lock(vlc_mutex_t *mutex)
{
    (void) mutex;
}

void vlc_mutex_unlock(vlc_mutex_t *mutex)
{
    (void) mutex;
}

void vlc_Log(vlc_object_t *o, int priority, const char *module,
             const char *file, unsigned line, const char *function,
             const char *format, ...)
{
    (void)o; (void)priority; (void)module; (void)file; (void)line;
    (void)function; (void)format;
}

static vlc_tick_t Convert(input_clock_t *clock, vlc_tick_t stream)
{
    int ret = input_clock_ConvertTS(NULL, clock, NULL, &stream, NULL,
                                     INT64_MAX);
    assert(ret == VLC_SUCCESS);
    return stream;
}

int main(void)
{
    input_clock_t *clock = input_clock_New(INPUT_RATE_DEFAULT);
    assert(clock != NULL);

    bool late = false;
    input_clock_Update(clock, NULL, &late, true, false,
                       10 * CLOCK_FREQ, 100 * CLOCK_FREQ);
    assert(!late);
    assert(Convert(clock, 11 * CLOCK_FREQ) == 101 * CLOCK_FREQ);

    input_clock_AddSystemDelay(clock, VLC_TICK_FROM_MS(320));
    assert(Convert(clock, 11 * CLOCK_FREQ) ==
           101 * CLOCK_FREQ + VLC_TICK_FROM_MS(320));

    /* A subsequent external-clock update must not undo the correction. */
    input_clock_ChangeSystemOrigin(clock, false, 500 * CLOCK_FREQ);
    input_clock_AddSystemDelay(clock, VLC_TICK_FROM_MS(80));
    input_clock_ChangeSystemOrigin(clock, false, 500 * CLOCK_FREQ);
    const vlc_tick_t converted = Convert(clock, 11 * CLOCK_FREQ);
    assert(converted == 101 * CLOCK_FREQ + VLC_TICK_FROM_MS(400));

    input_clock_Delete(clock);
    puts("PASS: input clock delay shifts future A/V timestamps and preserves external origin");
    return 0;
}
