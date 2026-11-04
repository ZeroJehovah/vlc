/* Deterministic tests of the actual backported module, using the real VLC 3
 * headers and a simulated Android AAudio service. No phone behavior is faked
 * into a claim of device validation. */
#define N_(text) text
#define dlopen MockDlopen
#define dlsym MockDlsym
#define dlclose MockDlclose
#include "libvlcjni/vlc/modules/audio_output/aaudio.c"
#undef dlopen
#undef dlsym
#undef dlclose
#undef config_PutFloat
#include <stdio.h>

struct AAudioStreamBuilderStruct {
    int rate, channels, format;
    AAudioStream_dataCallback data;
    AAudioStream_errorCallback error;
    void *user;
};
struct AAudioStreamStruct {
    struct AAudioStreamBuilderStruct cfg;
    aaudio_stream_state_t state;
    int64_t position, timestamp;
    bool timestamp_valid;
};

static vlc_tick_t fake_now = 10000000;
static unsigned allocated, released, opened, closed, restarts;
static float reported_gain, reported_volume;
static bool reported_mute, session_available = true;
static AAudioStream *active_stream;
static void Pump(int frames);

vlc_tick_t mdate(void) { return fake_now; }
void vlc_mutex_init(vlc_mutex_t *m) { assert(pthread_mutex_init(m, NULL) == 0); }
void vlc_mutex_destroy(vlc_mutex_t *m) { assert(pthread_mutex_destroy(m) == 0); }
void vlc_mutex_lock(vlc_mutex_t *m) { assert(pthread_mutex_lock(m) == 0); }
void vlc_mutex_unlock(vlc_mutex_t *m) { assert(pthread_mutex_unlock(m) == 0); }
void vlc_cond_init(vlc_cond_t *c) { assert(pthread_cond_init(c, NULL) == 0); }
void vlc_cond_destroy(vlc_cond_t *c) { assert(pthread_cond_destroy(c) == 0); }
void vlc_cond_signal(vlc_cond_t *c) { (void)c; }
int vlc_cond_timedwait(vlc_cond_t *c, vlc_mutex_t *m, vlc_tick_t deadline)
{
    (void)c;
    vlc_mutex_unlock(m);
    fake_now = deadline;
    if (active_stream && active_stream->state == AAUDIO_STREAM_STATE_STARTED) {
        Pump(240);
        active_stream->timestamp_valid = true;
        active_stream->position += 240;
        active_stream->timestamp = fake_now * 1000;
    }
    vlc_mutex_lock(m);
    return 0;
}
int var_Inherit(vlc_object_t *o, const char *name, int type, vlc_value_t *value)
{
    (void)o;
    memset(value, 0, sizeof(*value));
    if (type == VLC_VAR_FLOAT) value->f_float = 1.f;
    if (strcmp(name, "audiotrack-session-id") == 0) value->i_int = 42;
    return 0;
}
void config_PutFloat(vlc_object_t *o, const char *name, float value)
{ (void)o; (void)name; (void)value; }
void vlc_Log(vlc_object_t *o, int priority, const char *module, const char *file,
             unsigned line, const char *function, const char *format, ...)
{ (void)o; (void)priority; (void)module; (void)file; (void)line; (void)function; (void)format; }
vlc_fourcc_t vlc_fourcc_GetCodec(int category, vlc_fourcc_t codec)
{ (void)category; return codec; }
static void ReleaseBlock(block_t *b) { released++; free(b->p_start); free(b); }
block_t *block_Alloc(size_t bytes)
{
    block_t *b = calloc(1, sizeof(*b));
    assert(b);
    b->p_start = b->p_buffer = malloc(bytes ? bytes : 1);
    assert(b->p_start);
    b->i_size = b->i_buffer = bytes;
    b->pf_release = ReleaseBlock;
    allocated++;
    return b;
}
static int Gain(audio_output_t *a, float gain) { (void)a; reported_gain = gain; return 0; }
static void Volume(audio_output_t *a, float v) { (void)a; reported_volume = v; }
static void Mute(audio_output_t *a, bool m) { (void)a; reported_mute = m; }
static void Restart(audio_output_t *a, unsigned mode)
{ (void)a; assert(mode == AOUT_RESTART_OUTPUT); restarts++; }

aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **b)
{ *b = calloc(1, sizeof(**b)); return *b ? AAUDIO_OK : AAUDIO_ERROR_NO_MEMORY; }
const char *AAudio_convertResultToText(aaudio_result_t r) { (void)r; return "mock result"; }
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *b, aaudio_format_t v) { b->format = v; }
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *b, int32_t v) { b->rate = v; }
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *b, int32_t v) { b->channels = v; }
void AAudioStreamBuilder_setSessionId(AAudioStreamBuilder *b, aaudio_session_id_t v) { (void)b; assert(v == 42); }
void AAudioStreamBuilder_setUsage(AAudioStreamBuilder *b, aaudio_usage_t v) { (void)b; assert(v == AAUDIO_USAGE_MEDIA); }
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *b, aaudio_performance_mode_t v) { (void)b; (void)v; }
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *b, AAudioStream_dataCallback cb, void *user)
{ b->data = cb; b->user = user; }
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *b, AAudioStream_errorCallback cb, void *user)
{ assert(b->user == user); b->error = cb; }
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *b, AAudioStream **s)
{
    *s = calloc(1, sizeof(**s)); assert(*s);
    (*s)->cfg = *b;
    if (!(*s)->cfg.rate) (*s)->cfg.rate = 48000;
    (*s)->state = AAUDIO_STREAM_STATE_OPEN;
    active_stream = *s;
    opened++;
    return AAUDIO_OK;
}
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *b) { free(b); return AAUDIO_OK; }
int32_t AAudioStream_getSampleRate(AAudioStream *s) { return s->cfg.rate; }
int32_t AAudioStream_getChannelCount(AAudioStream *s) { return s->cfg.channels; }
aaudio_format_t AAudioStream_getFormat(AAudioStream *s) { return s->cfg.format; }
aaudio_result_t AAudioStream_getTimestamp(AAudioStream *s, clockid_t clock,
                                         int64_t *pos, int64_t *ns)
{
    assert(clock == CLOCK_MONOTONIC);
    if (!s->timestamp_valid) return AAUDIO_ERROR_INVALID_STATE;
    *pos = s->position; *ns = s->timestamp;
    return AAUDIO_OK;
}
aaudio_stream_state_t AAudioStream_getState(AAudioStream *s) { return s->state; }
aaudio_result_t AAudioStream_waitForStateChange(AAudioStream *s, aaudio_stream_state_t current,
                                              aaudio_stream_state_t *next, int64_t timeout)
{ (void)current; (void)timeout; *next = s->state; return AAUDIO_OK; }
aaudio_result_t AAudioStream_requestStart(AAudioStream *s) { s->state = AAUDIO_STREAM_STATE_STARTED; return AAUDIO_OK; }
aaudio_result_t AAudioStream_requestPause(AAudioStream *s) { s->state = AAUDIO_STREAM_STATE_PAUSED; return AAUDIO_OK; }
aaudio_result_t AAudioStream_requestStop(AAudioStream *s) { s->state = AAUDIO_STREAM_STATE_STOPPED; return AAUDIO_OK; }
aaudio_result_t AAudioStream_close(AAudioStream *s)
{ closed++; if (active_stream == s) active_stream = NULL; free(s); return AAUDIO_OK; }

void *MockDlopen(const char *name, int flags) { (void)flags; assert(!strcmp(name, "libaaudio.so")); return (void *)1; }
int MockDlclose(void *handle) { assert(handle == (void *)1); return 0; }
void *MockDlsym(void *handle, const char *symbol)
{
    assert(handle == (void *)1);
    if (!session_available && !strcmp(symbol, "AAudioStreamBuilder_setSessionId")) return NULL;
#define LOOKUP(result, name, args) if (!strcmp(symbol, #name)) return (void *)(uintptr_t)&name;
    AAUDIO_FUNCTIONS(LOOKUP)
#undef LOOKUP
    return NULL;
}

static audio_output_t NewOutput(void)
{
    audio_output_t a = {0};
    a.event.gain_request = Gain;
    a.event.volume_report = Volume;
    a.event.mute_report = Mute;
    a.event.restart_request = Restart;
    return a;
}
static block_t *Samples(audio_output_t *a, unsigned frames, float sample)
{
    block_t *b = block_Alloc(frames * a->sys->fmt.i_bytes_per_frame);
    b->i_pts = fake_now;
    b->i_nb_samples = frames;
    for (size_t i = 0; i < b->i_buffer / sizeof(float); i++) ((float *)b->p_buffer)[i] = sample;
    return b;
}
static void Pump(int frames)
{
    AAudioStream *s = active_stream;
    size_t bytes = frames * s->cfg.channels * (s->cfg.format == AAUDIO_FORMAT_PCM_FLOAT ? 4 : 2);
    void *buffer = malloc(bytes);
    assert(s->cfg.data(s, s->cfg.user, buffer, frames) == AAUDIO_CALLBACK_RESULT_CONTINUE);
    free(buffer);
}
static void Timestamp(int64_t position)
{
    active_stream->position = position;
    active_stream->timestamp = fake_now * 1000;
    active_stream->timestamp_valid = true;
}

int main(void)
{
    audio_output_t a = NewOutput();
    session_available = false;
    assert(Open((vlc_object_t *)&a) != VLC_SUCCESS); /* Android < 9 gate */
    session_available = true;
    assert(Open((vlc_object_t *)&a) == VLC_SUCCESS);
    audio_sample_format_t fmt = {.i_rate = 44100, .i_format = VLC_CODEC_FL32,
                                 .i_physical_channels = AOUT_CHANS_STEREO};
    assert(Start(&a, &fmt) == VLC_SUCCESS);
    assert(fmt.i_rate == 48000 && fmt.i_channels == 2 && fmt.i_bytes_per_frame == 8);
    vlc_tick_t delay;
    assert(TimeGet(&a, &delay) != 0);
    Play(&a, Samples(&a, 480, 0.25f));
    float out[480];
    assert(DataCallback(active_stream, &a, out, 240) == AAUDIO_CALLBACK_RESULT_CONTINUE);
    for (unsigned i = 0; i < 480; i++) assert(out[i] == 0.25f);
    Timestamp(0);
    assert(TimeGet(&a, &delay) == 0 && delay == 10000);
    Pump(240);
    Pump(240); /* silence after media must not increase its delay */
    Timestamp(480);
    assert(TimeGet(&a, &delay) == 0 && delay == 0);

    Play(&a, Samples(&a, 960, 0.5f));
    Pump(240);
    uint64_t queued = a.sys->queued_frames;
    int64_t written = a.sys->written_frames;
    Pause(&a, true, fake_now);
    assert(active_stream->state == AAUDIO_STREAM_STATE_PAUSED);
    DataCallback(active_stream, &a, out, 240); /* an in-flight callback */
    assert(a.sys->queued_frames == queued);
    for (unsigned i = 0; i < 480; i++) assert(out[i] == 0.f);
    fake_now += CLOCK_FREQ;
    Pause(&a, false, fake_now);
    assert(TimeGet(&a, &delay) != 0); /* stale pre-pause timestamp */
    Timestamp(written);
    assert(TimeGet(&a, &delay) == 0 && delay == 15000);
    Pump(240);
    assert(a.sys->queued_frames == queued - 240);
    Timestamp(a.sys->written_frames - 240);
    assert(TimeGet(&a, &delay) == 0 && delay == 15000);

    assert(a.volume_set(&a, 0.5f) == 0);
    assert(reported_gain == 0.125f && reported_volume == 0.5f);
    assert(a.mute_set(&a, true) == 0 && reported_gain == 0.f && reported_mute);
    assert(a.mute_set(&a, false) == 0 && reported_gain == 0.125f && !reported_mute);

    unsigned old_opened = opened;
    Flush(&a, false);
    assert(opened == old_opened + 1 && a.sys->queued_frames == 0 && a.sys->written_frames == 0);
    assert(active_stream->cfg.rate == 48000 && TimeGet(&a, &delay) != 0);
    Play(&a, Samples(&a, 960, 0.1f));
    Flush(&a, true);
    assert(a.sys->queued_frames == 0 && TimeGet(&a, &delay) == 0 && delay == 0);
    active_stream->cfg.error(active_stream, &a, AAUDIO_ERROR_DISCONNECTED);
    active_stream->cfg.error(active_stream, &a, AAUDIO_ERROR_DISCONNECTED);
    assert(restarts == 1 && atomic_load(&a.sys->failed));
    Play(&a, Samples(&a, 480, 0.2f)); /* discarded safely until VLC restart */
    Close((vlc_object_t *)&a);
    assert(allocated == released && opened == closed);

    a = NewOutput();
    assert(Open((vlc_object_t *)&a) == VLC_SUCCESS);
    fmt.i_physical_channels = AOUT_CHAN_CENTER;
    fmt.i_format = VLC_CODEC_S16N;
    assert(Start(&a, &fmt) == VLC_SUCCESS);
    assert(fmt.i_channels == 1 && fmt.i_bytes_per_frame == 2);
    Close((vlc_object_t *)&a);
    assert(opened == closed);
    puts("PASS: API gate, float/stereo, S16/mono, PCM delivery, underrun timing, pause/resume, volume/mute, flush, drain, disconnect and ownership");
}
