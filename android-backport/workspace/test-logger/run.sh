#!/bin/sh
set -eu
task_root=/home/ubuntu/vlc-aaudio
output="$task_root/artifacts/pause-diag2/logger-test-classes"
mkdir -p "$output"
javac -d "$output" \
  "$task_root/app-stable/application/vlc-android/src/org/videolan/vlc/util/DebugLogSnapshot.java" \
  "$task_root/test-logger/DebugLogSnapshotTest.java"
java -ea -cp "$output" DebugLogSnapshotTest
