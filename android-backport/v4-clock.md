# VLC 4 output clock backport

Branch: `android/3.7.2-v4-clock`. Integrated experimental build; no phone result yet.

The clock algorithm (`src/clock/clock*`, `include/vlc_clock.h`, `include/vlc_vector.h`)
comes from official VLC commit `c6d1df39c7a52ff2caa582521ca61db081fa525e`.
This is a source reference, not a verified identification of the user's preview APK.
The LGPL notices are retained. The adapter uses VLC 3 mutex assertions, logging,
microsecond ticks and allocation helpers. Structured tracing is disabled; the
algorithm, pause/reset handling and clock contexts are retained. Unlike VLC 4's
thread primitives, VLC 3 mutexes and conditions require explicit destruction.

The integration retains media PTS until the outputs, feeds actual audio
positions to a shared master, and converts video deadlines at display time.
It is enabled for sources whose reading pace VLC controls (including local
files). Input pacing still uses the input clock. Unpaced live inputs and sout
retain the VLC 3 synchronization path. This is an adaptation to the VLC 3
decoder/aout/vout API, not a replacement with the entire VLC 4 engine.

AudioTrack reports native AudioTimestamp points, with raw playback-head plus
hardware-latency fallback as in VLC 4. Old timestamps from before resume and
positions beyond written audio are rejected. Timing callbacks use a separate
mutex and never acquire the producer/output mutex. The core tracks output
sample ranges so old-rate PCM already queued during a speed change maps to the
correct media PTS. Other outputs adapt their existing time_get delay.

The master audio no longer uses the experimental resume-recovery bypass or the
legacy audio drift correction. Video follows the shared clock. The fix5 input
delay API and fix6 AudioTrack cache translation have been removed. There is no
fixed post-resume video waiting interval, but ordinary scheduling still follows
the measured audio position; a real output stall cannot be hidden by a clock.

Subtitles retain media dates through the existing 3.x SPU renderer; OSD dates
remain system dates. Snapshots are taken before converting the display date.
A cloned picture header supplies the system date expected by Android without
corrupting the media date of the retained picture. A recycled vout synchronously
detaches its borrowed decoder clock before that clock is freed. Flush resets
sample mappings and retains paused state. Video waits are interruptible by
stop/seek/control requests.

Validation of imported algorithm: `sh android-backport/tests/test-output-clock.sh`
uses the actual clock source under ASan/UBSan with pthread locks. It covers audio
feedback affecting queued video, pause/resume, seek while paused, playback rate
and master replacement. It also compiles the real input clock and audio timing
adapter, exercising stale/invalid reports and rate changes with queued PCM.
The AudioTrack JNI harness checks retained-queue wakeup, write-lock separation,
partial-write stop, timestamp dates, raw-head fallback and 32-bit counter wrap.
These tests do not establish audible sync, subtitle appearance or snapshot
success on a real Android device. Blueto​oth route changes, multiple concurrent
audio outputs, DVD menus and long-running playback still need device coverage.

Expected diagnostic markers:
`[v4-clock] enabled master=1 native_timing=1`,
`AudioTrack source=timestamp` (or `raw-head`),
`feedback system_us=...`, `video_clock attached=1`, and `video media_us=...`.

Build with `build-v4-clock1.init.gradle` and package with
`python3 package-v4-clock.py` from the workspace root after installing and
relinking the native libraries. The application ID and signing identity are
the same as the previous personal builds; version code is 13070209.

Official preview builds: <https://artifacts.videolan.org/vlc-android/nightly-arm64-v4/>.
The regular nightly channel uses `nightly-arm64/`; the app's nightly updater
links to that regular channel regardless of its own major version. The official
3.7.2 app tree supports both native core versions with `-vlc4` / `forceVlc4`.
