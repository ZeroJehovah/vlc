# VLC 4 output clock backport

Branch: `android/3.7.2-v4-clock`. Experimental work in progress; no phone result yet.

The clock algorithm (`src/clock/clock*`, `include/vlc_clock.h`, `include/vlc_vector.h`)
comes from official VLC commit `c6d1df39c7a52ff2caa582521ca61db081fa525e`.
This is a source reference, not a verified identification of the user's preview APK.
The LGPL notices are retained. The adapter uses VLC 3 mutex assertions, logging,
microsecond ticks and allocation helpers. Structured tracing is disabled; the
algorithm, pause/reset handling and clock contexts are retained. Unlike VLC 4's
thread primitives, VLC 3 mutexes and conditions require explicit destruction.

The integration must retain media PTS until the outputs, feed actual audio
positions to a shared master, and convert video deadlines at display time.
Input pacing and unpaced live sources must still use the input clock. Stable
subtitle rendering and snapshots should retain their existing implementation.
No success claim can be made before the integrated build and phone testing.

Validation of imported algorithm: `sh android-backport/tests/test-output-clock.sh`
uses the actual clock source under ASan/UBSan with pthread locks. It covers audio
feedback affecting queued video, pause/resume, seek while paused, playback rate
and master replacement. It does not simulate an Android audio device.

Official preview builds: <https://artifacts.videolan.org/vlc-android/nightly-arm64-v4/>.
The regular nightly channel uses `nightly-arm64/`; the app's nightly updater
links to that regular channel regardless of its own major version. The official
3.7.2 app tree supports both native core versions with `-vlc4` / `forceVlc4`.
