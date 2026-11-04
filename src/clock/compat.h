/* Compatibility for the VLC 4 output clock on the VLC 3 core.
 * The timing algorithm is upstream; structured tracing is unavailable in 3.x. */
#ifndef VLC_CLOCK_COMPAT_H
#define VLC_CLOCK_COMPAT_H
#include "libvlc.h"
#define VLC_TICK_MAX INT64_MAX
#define vlc_tick_now mdate
#define vlc_mutex_assert vlc_assert_locked
#define vlc_warning(obj, ...) do { if (obj) msg_Warn(obj, __VA_ARGS__); } while (0)
#define vlc_error(obj, ...) do { if (obj) msg_Err(obj, __VA_ARGS__); } while (0)
struct vlc_tracer;
#define vlc_tracer_TraceWithTs(...) ((void)0)
#define vlc_tracer_Trace(...) ((void)0)
#define vlc_tracer_TraceEvent(...) ((void)0)
#endif
