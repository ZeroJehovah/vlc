/* Exercise the real EOF drain and seek flush with a fake JNI sink. */
#define N_(s) s
#include <errno.h>
#include AUDIOTRACK_SOURCE
#include <stdio.h>
#include <stdarg.h>

static pthread_mutex_t test_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_cond = PTHREAD_COND_INITIALIZER;
static aout_sys_t *active;
static bool draining, drained;
static size_t written, expected;
static unsigned stops, plays, flushes;
static int play_method, pause_method, stop_method, flush_method, write_method, head_method;

vlc_tick_t mdate(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (vlc_tick_t)ts.tv_sec * CLOCK_FREQ + ts.tv_nsec / 1000;
}
void vlc_mutex_init(vlc_mutex_t *m) { assert(!pthread_mutex_init(m, NULL)); }
void vlc_mutex_destroy(vlc_mutex_t *m) { assert(!pthread_mutex_destroy(m)); }
void vlc_mutex_lock(vlc_mutex_t *m) { assert(!pthread_mutex_lock(m)); }
void vlc_mutex_unlock(vlc_mutex_t *m) { assert(!pthread_mutex_unlock(m)); }
void vlc_cond_init(vlc_cond_t *c) { assert(!pthread_cond_init(c, NULL)); }
void vlc_cond_destroy(vlc_cond_t *c) { assert(!pthread_cond_destroy(c)); }
void vlc_cond_signal(vlc_cond_t *c) { assert(!pthread_cond_signal(c)); }
void vlc_cond_wait(vlc_cond_t *c, vlc_mutex_t *m)
{
    if (c == &active->aout_cond) {
        pthread_mutex_lock(&test_lock);
        draining = true;
        pthread_cond_broadcast(&test_cond);
        pthread_mutex_unlock(&test_lock);
    }
    assert(!pthread_cond_wait(c, m));
}
int vlc_cond_timedwait(vlc_cond_t *c, vlc_mutex_t *m, vlc_tick_t deadline)
{
    (void)c; (void)m; (void)deadline;
    assert(!"Unexpected backpressure: mock sink has sufficient capacity");
    return ETIMEDOUT;
}
void vlc_Log(vlc_object_t *o, int p, const char *module, const char *file,
             unsigned line, const char *function, const char *format, ...)
{ (void)o; (void)p; (void)module; (void)file; (void)line; (void)function; (void)format; }
int var_Inherit(vlc_object_t *o, const char *n, int type, vlc_value_t *v)
{ (void)o; (void)n; (void)type; memset(v, 0, sizeof(*v)); return 0; }

static jboolean MockExceptionCheck(JNIEnv *env) { (void)env; return JNI_FALSE; }
static void MockGetBytes(JNIEnv *env, jbyteArray a, jsize start, jsize len, jbyte *buf)
{ (void)env; (void)a; (void)start; memset(buf, 0, len); }
static void MockSetBytes(JNIEnv *env, jbyteArray a, jsize start, jsize len, const jbyte *buf)
{ (void)env; (void)a; (void)start; (void)len; (void)buf; }
static void MockCallVoid(JNIEnv *env, jobject obj, jmethodID method, ...)
{
    (void)env; (void)obj;
    if (method == (jmethodID)&stop_method) {
        assert(written == expected); /* no tail PCM may be lost before stop */
        stops++;
    } else if (method == (jmethodID)&play_method)
        plays++;
    else if (method == (jmethodID)&flush_method)
        flushes++;
    else
        assert(method == (jmethodID)&pause_method);
}
static jint MockCallInt(JNIEnv *env, jobject obj, jmethodID method, ...)
{
    (void)env; (void)obj;
    if (method == (jmethodID)&head_method)
        return 0;
    assert(method == (jmethodID)&write_method);
    va_list ap;
    va_start(ap, method);
    (void)va_arg(ap, jbyteArray);
    (void)va_arg(ap, jsize);
    jsize size = va_arg(ap, jsize);
    va_end(ap);
    /* Force several partial staged writes, not just a single transfer. */
    size = size > 4096 ? 4096 : size;
    written += size;
    return size;
}
static const struct JNINativeInterface_ jni = {
    .ExceptionCheck = MockExceptionCheck, .GetByteArrayRegion = MockGetBytes,
    .SetByteArrayRegion = MockSetBytes, .CallVoidMethod = MockCallVoid,
    .CallIntMethod = MockCallInt,
};
static JNIEnv env = &jni;
JNIEnv *android_getEnv(vlc_object_t *o, const char *n)
{ (void)o; (void)n; return &env; }

static bool WaitFor(bool *flag)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 300000000;
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    pthread_mutex_lock(&test_lock);
    int ret = 0;
    while (!*flag && ret != ETIMEDOUT)
        ret = pthread_cond_timedwait(&test_cond, &test_lock, &ts);
    bool result = *flag;
    pthread_mutex_unlock(&test_lock);
    return result;
}
static void *Drain(void *aout)
{
    Flush(aout, true);
    pthread_mutex_lock(&test_lock);
    drained = true;
    pthread_cond_broadcast(&test_cond);
    pthread_mutex_unlock(&test_lock);
    return NULL;
}
static void Init(aout_sys_t *sys)
{
    memset(sys, 0, sizeof(*sys));
    active = sys;
    draining = drained = false;
    written = stops = plays = flushes = 0;
    sys->fmt.i_rate = 48000;
    sys->fmt.i_bytes_per_frame = 4;
    sys->fmt.i_frame_length = 1;
    sys->i_write_type = WRITE_BYTEARRAYV23;
    sys->i_max_audiotrack_samples = 38400;
    sys->circular.i_size = sys->circular.i_write = 38400;
    sys->staging.p_data = calloc(1, sys->circular.i_size);
    assert(sys->staging.p_data);
    sys->staging.i_size = sys->circular.i_size;
    sys->b_thread_running = true;
    vlc_mutex_init(&sys->lock);
    vlc_mutex_init(&sys->write_lock);
    vlc_cond_init(&sys->thread_cond);
    vlc_cond_init(&sys->aout_cond);
}
static void Destroy(aout_sys_t *sys)
{
    free(sys->staging.p_data);
    vlc_cond_destroy(&sys->aout_cond);
    vlc_cond_destroy(&sys->thread_cond);
    vlc_mutex_destroy(&sys->write_lock);
    vlc_mutex_destroy(&sys->lock);
}

int main(int argc, char **argv)
{
    bool before = argc > 1 && !strcmp(argv[1], "--expect-stall");
    jfields.AudioTrack.play = (jmethodID)&play_method;
    jfields.AudioTrack.pause = (jmethodID)&pause_method;
    jfields.AudioTrack.stop = (jmethodID)&stop_method;
    jfields.AudioTrack.flush = (jmethodID)&flush_method;
    jfields.AudioTrack.writeV23 = (jmethodID)&write_method;
    jfields.AudioTrack.getPlaybackHeadPosition = (jmethodID)&head_method;
    jfields.AudioTimestamp.clazz = (jclass)&jni; /* no old-Android recreation */
    for (unsigned round = 0; round < (before ? 1u : 20u); round++) {
        aout_sys_t sys;
        Init(&sys);
        audio_output_t aout = {.sys = &sys};
        expected = sys.circular.i_write;
        pthread_t drain, writer;
        /* Deterministically enter EOF draining while tail PCM is still queued. */
        assert(!pthread_create(&drain, NULL, Drain, &aout));
        assert(WaitFor(&draining));
        assert(!pthread_create(&writer, NULL, AudioTrack_Thread, &aout));
        bool complete = WaitFor(&drained);
        if (before) {
            assert(!complete);
            /* Escape the reproduced deadlock so sanitizer teardown can run. */
            vlc_mutex_lock(&sys.lock);
            sys.b_error = true;
            vlc_cond_signal(&sys.aout_cond);
            vlc_mutex_unlock(&sys.lock);
            assert(WaitFor(&drained));
        } else {
            assert(complete);
            assert(written == expected && stops == 1 && plays == 1 && flushes == 0);
            assert(!sys.b_flushing && !sys.i_samples_inflight);
        }
        assert(!pthread_join(drain, NULL));
        vlc_mutex_lock(&sys.lock);
        sys.b_thread_running = false;
        vlc_cond_signal(&sys.thread_cond);
        vlc_mutex_unlock(&sys.lock);
        assert(!pthread_join(writer, NULL));
        Destroy(&sys);
    }
    if (before) {
        puts("PASS: v4-clock2 EOF drain deadlocks with retained PCM");
        return 0;
    }
    /* A seek discards buffered PCM immediately; a paused seek stays paused. */
    for (unsigned paused = 0; paused < 2; paused++) {
        aout_sys_t sys;
        Init(&sys);
        audio_output_t aout = {.sys = &sys};
        sys.b_thread_paused = paused;
        sys.staging.i_length = 4096;
        sys.i_samples_inflight = 1024;
        Flush(&aout, false);
        assert(!written && !stops && flushes == 1 && plays == !paused);
        assert(!sys.circular.i_write && !sys.i_samples_inflight);
        assert(!sys.staging.i_length || sys.staging.i_generation != sys.i_write_generation);
        assert(sys.b_thread_paused == paused && !sys.b_flushing);
        Destroy(&sys);
    }
    puts("PASS: EOF drains all partial PCM writes (20 runs); seek flush discards PCM and preserves pause");
}
