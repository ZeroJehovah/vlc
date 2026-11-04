/* Exercise the real AudioTrack writer and Pause(), with a fake JNI sink.
 * No decoded block is supplied after resume: retained PCM must wake itself. */
#define N_(s) s
#include <errno.h>
#include AUDIOTRACK_SOURCE
#include <stdio.h>
#include <stdarg.h>

static pthread_mutex_t test_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_cond = PTHREAD_COND_INITIALIZER;
static bool waiting, wrote;
static size_t bytes_written;
static unsigned write_calls, head_position;
static aout_sys_t *active;
static int play_method, pause_method, write_method, head_method;

vlc_tick_t mdate(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (vlc_tick_t)ts.tv_sec * CLOCK_FREQ + ts.tv_nsec / 1000;
}
void vlc_mutex_init(vlc_mutex_t *m) { assert(pthread_mutex_init(m, NULL) == 0); }
void vlc_mutex_destroy(vlc_mutex_t *m) { assert(pthread_mutex_destroy(m) == 0); }
void vlc_mutex_lock(vlc_mutex_t *m) { assert(pthread_mutex_lock(m) == 0); }
void vlc_mutex_unlock(vlc_mutex_t *m) { assert(pthread_mutex_unlock(m) == 0); }
void vlc_cond_init(vlc_cond_t *c) { assert(pthread_cond_init(c, NULL) == 0); }
void vlc_cond_destroy(vlc_cond_t *c) { assert(pthread_cond_destroy(c) == 0); }
void vlc_cond_signal(vlc_cond_t *c) { assert(pthread_cond_signal(c) == 0); }
void vlc_cond_wait(vlc_cond_t *c, vlc_mutex_t *m)
{
    if (c == &active->thread_cond && active->b_thread_paused) {
        pthread_mutex_lock(&test_lock);
        waiting = true;
        pthread_cond_signal(&test_cond);
        pthread_mutex_unlock(&test_lock);
    }
    assert(pthread_cond_wait(c, m) == 0);
}
int vlc_cond_timedwait(vlc_cond_t *c, vlc_mutex_t *m, vlc_tick_t deadline)
{
    (void)c; (void)m; (void)deadline;
    /* The simulated Android sink always has room. */
    assert(!"Unexpected backpressure");
    return ETIMEDOUT;
}
void vlc_Log(vlc_object_t *o, int priority, const char *module, const char *file,
             unsigned line, const char *function, const char *format, ...)
{ (void)o; (void)priority; (void)module; (void)file; (void)line; (void)function; (void)format; }

static jboolean MockExceptionCheck(JNIEnv *env) { (void)env; return JNI_FALSE; }
static void MockGetByteArrayRegion(JNIEnv *env, jbyteArray array, jsize start,
                                   jsize len, jbyte *buf)
{ (void)env; (void)array; (void)start; (void)len; (void)buf; }
static void MockSetByteArrayRegion(JNIEnv *env, jbyteArray array, jsize start,
                                   jsize len, const jbyte *buf)
{ (void)env; (void)array; (void)start; (void)len; (void)buf; }
static void MockCallVoid(JNIEnv *env, jobject obj, jmethodID method, ...)
{
    (void)env; (void)obj;
    assert(method == (jmethodID)&play_method || method == (jmethodID)&pause_method);
}
static jint MockCallInt(JNIEnv *env, jobject obj, jmethodID method, ...)
{
    (void)env; (void)obj;
    if (method == (jmethodID)&head_method)
        return head_position;
    assert(method == (jmethodID)&write_method);
    va_list ap;
    va_start(ap, method);
    (void)va_arg(ap, jbyteArray);
    (void)va_arg(ap, size_t);
    size_t size = va_arg(ap, size_t);
    va_end(ap);
    pthread_mutex_lock(&test_lock);
    bytes_written += size;
    write_calls++;
    wrote = true;
    pthread_cond_signal(&test_cond);
    pthread_mutex_unlock(&test_lock);
    return (jint)size;
}
static const struct JNINativeInterface_ jni = {
    .ExceptionCheck = MockExceptionCheck,
    .GetByteArrayRegion = MockGetByteArrayRegion,
    .SetByteArrayRegion = MockSetByteArrayRegion,
    .CallVoidMethod = MockCallVoid,
    .CallIntMethod = MockCallInt,
};
static JNIEnv env = &jni;
JNIEnv *android_getEnv(vlc_object_t *o, const char *name)
{ (void)o; (void)name; return &env; }
int var_Inherit(vlc_object_t *o, const char *name, int type, vlc_value_t *value)
{ (void)o; (void)name; (void)type; memset(value, 0, sizeof(*value)); return 0; }

static bool WaitFlag(bool *flag)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 300000000;
    if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
    pthread_mutex_lock(&test_lock);
    int ret = 0;
    while (!*flag && ret != ETIMEDOUT)
        ret = pthread_cond_timedwait(&test_cond, &test_lock, &deadline);
    bool result = *flag;
    pthread_mutex_unlock(&test_lock);
    return result;
}

int main(int argc, char **argv)
{
    bool old = argc > 1 && strcmp(argv[1], "--expect-stall") == 0;
    jfields.AudioTrack.play = (jmethodID)&play_method;
    jfields.AudioTrack.pause = (jmethodID)&pause_method;
    jfields.AudioTrack.writeV23 = (jmethodID)&write_method;
    jfields.AudioTrack.getPlaybackHeadPosition = (jmethodID)&head_method;

#ifdef EXPECT_V23_BUFFER_GUARD
    /* A full vendor buffer must not be passed to the "non-blocking" Java
     * write, which can block during Bluetooth route restoration. */
    aout_sys_t full = {0};
    audio_output_t full_aout = {.sys = &full};
    full.fmt.i_rate = 48000;
    full.fmt.i_bytes_per_frame = 4;
    full.fmt.i_frame_length = 1;
    full.i_samples_written = 38400;
    full.i_max_audiotrack_samples = 38400;
    head_position = write_calls = 0;
    assert(AudioTrack_PlayByteArrayV23(&env, &full_aout, 4096, 0) == 0);
    assert(write_calls == 0);
#endif

    for (unsigned round = 0; round < (old ? 1u : 20u); round++) {
        aout_sys_t sys = {0};
        audio_output_t aout = {.sys = &sys};
        active = &sys;
        waiting = wrote = false;
        bytes_written = 0;
        write_calls = 0;
        head_position = 0;
        sys.fmt.i_rate = 48000;
        sys.fmt.i_bytes_per_frame = 4;
        sys.fmt.i_frame_length = 1;
        sys.i_write_type = WRITE_BYTEARRAYV23;
        sys.i_max_audiotrack_samples = 38400;
        sys.circular.i_size = 38400;
        sys.circular.i_write = 38400; /* 200 ms already queued before pause */
#ifdef EXPECT_V23_BUFFER_GUARD
        sys.staging.p_data = calloc(1, sys.circular.i_size);
        assert(sys.staging.p_data);
        sys.staging.i_size = sys.circular.i_size;
#endif
        sys.b_thread_running = true;
        vlc_mutex_init(&sys.lock);
#ifdef EXPECT_V23_BUFFER_GUARD
        vlc_mutex_init(&sys.write_lock);
        sys.i_pause_date = VLC_TICK_INVALID;
        sys.timestamp.i_frame_us = 12345;
        sys.smoothpos.i_count = 2;
        sys.smoothpos.p_us[0] = -1000;
        sys.smoothpos.p_us[1] = -2000;
        sys.smoothpos.i_us = -1500;
#endif
        vlc_cond_init(&sys.thread_cond);
        vlc_cond_init(&sys.aout_cond);
        const vlc_tick_t pause_date = mdate() - CLOCK_FREQ;
        Pause(&aout, true, pause_date);
        pthread_t worker;
        assert(pthread_create(&worker, NULL, AudioTrack_Thread, &aout) == 0);
        assert(WaitFlag(&waiting));
        const vlc_tick_t resume_date = mdate();
        Pause(&aout, false, resume_date);
#ifdef EXPECT_V23_BUFFER_GUARD
        const vlc_tick_t pause_duration = resume_date - pause_date;
        vlc_mutex_lock(&sys.lock);
        assert(sys.i_pause_date == VLC_TICK_INVALID);
        assert(sys.timestamp.i_frame_us == 12345 + pause_duration);
        assert(sys.smoothpos.p_us[0] == -1000 - pause_duration);
        assert(sys.smoothpos.p_us[1] == -2000 - pause_duration);
        assert(sys.smoothpos.i_us == -1500 - pause_duration);
        vlc_mutex_unlock(&sys.lock);
#endif
        bool progressed = WaitFlag(&wrote);
        if (old) {
            assert(!progressed);
            /* The old code only resumes when a later Play() signals the queue. */
            vlc_mutex_lock(&sys.lock);
            vlc_cond_signal(&sys.thread_cond);
            vlc_mutex_unlock(&sys.lock);
            assert(WaitFlag(&wrote));
        } else assert(progressed);
#ifdef EXPECT_V23_BUFFER_GUARD
        for (unsigned i = 0; i < 1000; i++) {
            vlc_mutex_lock(&sys.lock);
            bool complete = sys.i_samples_inflight == 0
                         && sys.i_samples_written == 9600;
            vlc_mutex_unlock(&sys.lock);
            if (complete)
                break;
            usleep(1000);
        }
#endif
        vlc_mutex_lock(&sys.lock);
        assert(sys.circular.i_read == sys.circular.i_write);
        assert(sys.i_samples_written == 9600);
        sys.b_thread_running = false;
        vlc_cond_signal(&sys.thread_cond);
        vlc_mutex_unlock(&sys.lock);
        assert(pthread_join(worker, NULL) == 0);
        assert(bytes_written == 38400);
#ifdef EXPECT_V23_BUFFER_GUARD
        free(sys.staging.p_data);
#endif
        vlc_cond_destroy(&sys.aout_cond);
        vlc_cond_destroy(&sys.thread_cond);
#ifdef EXPECT_V23_BUFFER_GUARD
        vlc_mutex_destroy(&sys.write_lock);
#endif
        vlc_mutex_destroy(&sys.lock);
    }
    puts(old ? "PASS: original source stalls with queued PCM until an unrelated later signal"
             : "PASS: patched source sends retained PCM on resume, without a new decoded block (20 runs)");
}
