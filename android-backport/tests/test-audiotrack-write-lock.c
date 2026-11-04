/* Prove that a vendor AudioTrack.write() stall cannot hold the producer lock. */
#define N_(s) s
#include <errno.h>
#include AUDIOTRACK_SOURCE
#include <stdio.h>
#include <stdarg.h>

static pthread_mutex_t test_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_cond = PTHREAD_COND_INITIALIZER;
static bool write_entered, release_write, play_started, play_done;
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
{ assert(pthread_cond_wait(c, m) == 0); }
int vlc_cond_timedwait(vlc_cond_t *c, vlc_mutex_t *m, vlc_tick_t deadline)
{
    struct timespec ts = {
        .tv_sec = deadline / CLOCK_FREQ,
        .tv_nsec = (deadline % CLOCK_FREQ) * 1000,
    };
    return pthread_cond_timedwait(c, m, &ts);
}
void vlc_Log(vlc_object_t *o, int priority, const char *module, const char *file,
             unsigned line, const char *function, const char *format, ...)
{ (void)o; (void)priority; (void)module; (void)file; (void)line; (void)function; (void)format; }
static void ReleaseBlock(block_t *block) { (void)block; }
void aout_ChannelReorder(void *p, size_t s, uint8_t c, const uint8_t *t,
                         vlc_fourcc_t f)
{ (void)p; (void)s; (void)c; (void)t; (void)f; }

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
        return 0;
    assert(method == (jmethodID)&write_method);
    va_list ap;
    va_start(ap, method);
    (void)va_arg(ap, jbyteArray);
    (void)va_arg(ap, jsize);
    jsize size = va_arg(ap, jsize);
    (void)va_arg(ap, jint);
    va_end(ap);

    pthread_mutex_lock(&test_lock);
    write_entered = true;
    pthread_cond_broadcast(&test_cond);
    while (!release_write)
        pthread_cond_wait(&test_cond, &test_lock);
    pthread_mutex_unlock(&test_lock);
    return size;
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

static bool WaitFor(bool *flag)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 1;
    pthread_mutex_lock(&test_lock);
    int ret = 0;
    while (!*flag && ret != ETIMEDOUT)
        ret = pthread_cond_timedwait(&test_cond, &test_lock, &deadline);
    bool result = *flag;
    pthread_mutex_unlock(&test_lock);
    return result;
}

static void *ProbePlay(void *opaque)
{
    audio_output_t *aout = opaque;
    uint8_t data[16384] = {0};
    block_t block = {.p_buffer = data, .i_buffer = sizeof(data),
                     .pf_release = ReleaseBlock};

    pthread_mutex_lock(&test_lock);
    play_started = true;
    pthread_cond_broadcast(&test_cond);
    pthread_mutex_unlock(&test_lock);
    Play(aout, &block);
    pthread_mutex_lock(&test_lock);
    play_done = true;
    pthread_cond_broadcast(&test_cond);
    pthread_mutex_unlock(&test_lock);
    return NULL;
}

int main(void)
{
    aout_sys_t sys = {0};
    audio_output_t aout = {.sys = &sys};
    jfields.AudioTrack.play = (jmethodID)&play_method;
    jfields.AudioTrack.pause = (jmethodID)&pause_method;
    jfields.AudioTrack.writeV23 = (jmethodID)&write_method;
    jfields.AudioTrack.getPlaybackHeadPosition = (jmethodID)&head_method;
    sys.fmt.i_rate = 48000;
    sys.fmt.i_bytes_per_frame = 4;
    sys.fmt.i_frame_length = 1;
    sys.i_write_type = WRITE_BYTEARRAYV23;
    sys.i_max_audiotrack_samples = 38400;
    sys.circular.i_size = 192000;
    sys.b_thread_running = true;

#ifdef EXPECT_WRITE_DECOUPLING
    sys.staging.p_data = calloc(1, sys.circular.i_size);
    assert(sys.staging.p_data);
    sys.staging.i_size = sys.circular.i_size;
#endif
    vlc_mutex_init(&sys.lock);
#ifdef EXPECT_WRITE_DECOUPLING
    vlc_mutex_init(&sys.write_lock);
#endif
    vlc_cond_init(&sys.thread_cond);
    vlc_cond_init(&sys.aout_cond);

    pthread_t writer;
    assert(pthread_create(&writer, NULL, AudioTrack_Thread, &aout) == 0);

    uint8_t first_data[16384] = {0};
    block_t first = {.p_buffer = first_data, .i_buffer = sizeof(first_data),
                     .pf_release = ReleaseBlock};
    Play(&aout, &first);
    assert(WaitFor(&write_entered));

    pthread_t probe;
    assert(pthread_create(&probe, NULL, ProbePlay, &aout) == 0);
    assert(WaitFor(&play_started));
    usleep(150000);

    pthread_mutex_lock(&test_lock);
    bool decoupled = play_done;
    release_write = true;
    pthread_cond_broadcast(&test_cond);
    pthread_mutex_unlock(&test_lock);
    assert(pthread_join(probe, NULL) == 0);
    assert(play_done);

    vlc_mutex_lock(&sys.lock);
    sys.b_thread_running = false;
    vlc_cond_signal(&sys.thread_cond);
    vlc_mutex_unlock(&sys.lock);
    assert(pthread_join(writer, NULL) == 0);

#ifdef EXPECT_WRITE_DECOUPLING
    assert(decoupled);
    free(sys.staging.p_data);
    vlc_mutex_destroy(&sys.write_lock);
    puts("PASS: producer keeps enqueueing while vendor AudioTrack.write is stalled");
#else
    assert(!decoupled);
    puts("PASS: old writer lock blocks the producer for the full vendor write");
#endif
    vlc_cond_destroy(&sys.aout_cond);
    vlc_cond_destroy(&sys.thread_cond);
    vlc_mutex_destroy(&sys.lock);
    return 0;
}
