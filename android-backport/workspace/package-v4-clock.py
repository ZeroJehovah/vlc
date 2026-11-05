#!/usr/bin/env python3
"""Sign and verify the shared output-clock experiment, retaining earlier APKs."""
import hashlib
import json
import os
import pathlib
import re
import subprocess
import zipfile

root = pathlib.Path(__file__).resolve().parent
core_repo = root / 'libvlcjni/vlc'
subprocess.run(['git', 'diff', '--quiet', 'HEAD', '--', 'modules', 'src', 'include'],
               cwd=core_repo, check=True)
core_source_commit = subprocess.check_output(
    ['git', 'log', '-1', '--format=%H', '--', 'modules', 'src', 'include'],
    cwd=core_repo, text=True).strip()
release = os.environ.get('VLC_RELEASE_TAG', 'v4-clock2')
version_name = '3.7.2-' + release
version_code = int(os.environ.get('VLC_VERSION_CODE', '13070210'))
previous_release = os.environ.get('VLC_PREVIOUS_RELEASE', 'v4-clock1')
artifacts = root / 'artifacts' / release
artifacts.mkdir(parents=True, exist_ok=True)
source = root / ('app-stable/application/app/build/outputs/apk/release/VLC-Android-' +
                 version_name + '-all.apk')
assert source.is_file(), source
tool = root / 'toolchain'
sdk = tool / 'android-sdk/build-tools/36.0.0'
aligned = artifacts / ('VLC-' + version_name + '-arm64-v8a-unsigned-aligned.apk')
apk = artifacts / ('VLC-' + version_name + '-arm64-v8a.apk')
qemu = ['/usr/bin/qemu-x86_64', '-L', str(tool / 'x86-root'), '-E',
        'LD_LIBRARY_PATH=' + str(sdk / 'lib64')]
subprocess.run(qemu + [str(sdk / 'zipalign'), '-P', '16', '-f', '4', str(source), str(aligned)], check=True)
subprocess.run(['apksigner', 'sign', '--ks', str(tool / 'signing/vlc-aaudio.p12'),
                '--ks-key-alias', 'vlc-aaudio', '--ks-pass', 'file:' + str(tool / 'signing/password'),
                '--out', str(apk), str(aligned)], check=True)
signature = subprocess.check_output(['apksigner', 'verify', '--verbose', '--print-certs', str(apk)], text=True)
(artifacts / 'signature-verification.txt').write_text(signature)
previous_signature = (root / 'artifacts/signature-verification.txt').read_text()
certificate_pattern = r'Signer #1 certificate SHA-256 digest: ([0-9a-f]+)'
assert re.search(certificate_pattern, signature).group(1) == re.search(certificate_pattern, previous_signature).group(1)
assert 'Verified using v2 scheme (APK Signature Scheme v2): true' in signature
subprocess.run(qemu + [str(sdk / 'zipalign'), '-c', '-P', '16', '4', str(apk)], check=True)
badging = subprocess.check_output([str(tool / 'host-tools/aapt2'), 'dump', 'badging', str(apk)], text=True)
(artifacts / 'apk-badging.txt').write_text(badging)
assert "package: name='org.videolan.vlc.aaudio'" in badging
assert "versionCode='%d'" % version_code in badging
assert "versionName='%s'" % version_name in badging
assert "native-code: 'arm64-v8a'" in badging
manifest = subprocess.check_output([str(tool / 'host-tools/aapt2'), 'dump', 'xmltree',
                                    str(apk), '--file', 'AndroidManifest.xml'], text=True)
(artifacts / 'apk-manifest.txt').write_text(manifest)
logger_declaration = re.search(r'"org\.videolan\.vlc\.DebugLogService".*?foregroundServiceType[^\n]*=(0x[0-9a-fA-F]+)',
                               manifest, flags=re.DOTALL)
assert logger_declaration
logger_types = int(logger_declaration.group(1), 16)
assert logger_types & 0x1, 'Android 10-13 dataSync foreground type missing'
assert logger_types & 0x40000000, 'Android 14+ specialUse foreground type missing'
native = {}
native_build = core_repo / 'build-android-aarch64-linux-android'
plugin_manifest_path = native_build / 'ndk/static-plugins/manifest.json'
plugin_manifest = json.loads(plugin_manifest_path.read_text())
assert 'android_audiotrack' in plugin_manifest
for name, entry in plugin_manifest.items():
    original = native_build / entry['source']
    linked = native_build / 'ndk/static-plugins' / ('lib' + name + '_plugin.a')
    assert hashlib.sha256(original.read_bytes()).hexdigest() == entry['source_sha256'], name
    assert hashlib.sha256(linked.read_bytes()).hexdigest() == entry['linked_sha256'], name
with zipfile.ZipFile(apk) as package:
    libs = [name for name in package.namelist() if name.startswith('lib/') and name.endswith('.so')]
    assert libs and all(name.startswith('lib/arm64-v8a/') for name in libs)
    for name in ('libvlc.so', 'libvlcjni.so', 'libc++_shared.so', 'libmla.so'):
        data = package.read('lib/arm64-v8a/' + name)
        native[name] = hashlib.sha256(data).hexdigest()
        assert data[:4] == b'\x7fELF' and int.from_bytes(data[18:20], 'little') == 183
        if name in ('libvlc.so', 'libvlcjni.so'):
            built = root / 'libvlcjni/libvlc/jni/libs/arm64-v8a' / name
            assert native[name] == hashlib.sha256(built.read_bytes()).hexdigest(), name
    core = package.read('lib/arm64-v8a/libvlc.so')
    for marker in (b'[pause-diag] build=diag1 audio_module=',
                   b'[pause-diag] decoder_audio_convert', b'[pause-diag] input_clock_pause',
                   b'[pause-diag] action=insert_silence', b'[pause-diag] action=flush_late',
                   b'[pause-diag] backend_play', b'3.0.25 Vetinari',
                   b'Android AAudio output (VLC 3 backport)',
                   b'[v4-clock] enabled master='):
        assert marker in core, marker
    previous = json.loads((root / 'artifacts' / previous_release / 'build-info.json').read_text())
    assert native['libvlc.so'] != previous['native_sha256']['libvlc.so']
    # Medialibrary and C++ runtime payloads stay identical.
    assert native['libmla.so'] == previous['native_sha256']['libmla.so']
    assert native['libc++_shared.so'] == previous['native_sha256']['libc++_shared.so']
    diagnostic_previous = json.loads((root / 'artifacts/pause-diag1/build-info.json').read_text())
    assert native['libvlc.so'] != diagnostic_previous['native_sha256']['libvlc.so']
    assert b'[pause-fix] resume_notify' in core
    assert b'[pause-fix] writer_resumed' in core
    assert b'[pause-fix] nonblocking_write blocked_us=' in core
    assert b'audiotrack position is ahead after resume' in core
    assert b'action=defer_flush_resume' in core
    assert b'action=defer_silence_resume' in core
    assert b'action=resume_recovery_end' in core
    assert b'[pause-fix] staged_write blocked_us=' in core
    assert b'[v4-clock] enabled master=' in core
    for marker in (b'[v4-clock] feedback system_us=', b'[v4-clock] AudioTrack source=',
                   b'[v4-clock] video media_us=', b'[v4-clock] video_clock attached='):
        assert marker in core, marker
    assert b'[pause-fix] timing_shift' not in core
    assert b'action=resume_clock_delay' not in core
    assert b'action=input_clock_delay' not in core
    dex_files = [package.read(name) for name in package.namelist() if name.endswith('.dex')]
    for marker in (b'Could not start diagnostic foreground service', b'DebugLogSnapshot', b'pause-diagnostics'):
        assert any(marker in dex for dex in dex_files), marker

info = {
    'apk': apk.name, 'bytes': apk.stat().st_size,
    'sha256': hashlib.sha256(apk.read_bytes()).hexdigest(),
    'package': 'org.videolan.vlc.aaudio', 'version_name': version_name,
    'version_code': version_code, 'abi': 'arm64-v8a', 'core_version': '3.0.25',
    'native_sha256': native, 'updates_previous_personal_build': True,
    'static_plugin_count': len(plugin_manifest),
    'static_plugin_manifest_sha256': hashlib.sha256(plugin_manifest_path.read_bytes()).hexdigest(),
    'purpose': 'VLC 4 shared output clock and native AudioTrack position reports on VLC 3 stable core',
    'diagnostic_window_seconds': 5, 'max_samples_per_resume_per_stage': 32,
    'max_audio_correction_events_per_resume': 48,
    'app_commit': 'e5740f713d995f6c8e7d92e64134abc3a3b6987d',
    'jni_commit': '0b8dc65efb203c86a0476bc337ddd99ecf1c0ef6',
    'core_commit_before_android_patches': '84177b2273abc5c4300234e6e18b55a4d2dd5a03',
    'core_source_commit': core_source_commit,
    'upstream_clock_commit': 'c6d1df39c7a52ff2caa582521ca61db081fa525e',
    'fork_branch': 'android/3.7.2-v4-clock',
    'checks': ['Release assembly and lint', 'APK signature v2', 'same signing certificate',
               'increased version code', '16KB ZIP alignment', 'arm64 ELF',
               'custom native library hashes', 'shared clock, audio feedback and video deadline markers',
               'static plugins refreshed from current build archives', 'unchanged medialibrary/runtime',
               'original AudioTrack source reproduces retained-queue stall', 'fixed writer resumes without new data: 20 runs, ASan/UBSan',
               'non-blocking AudioTrack write checks full internal buffer',
               'legacy compatibility path: resume recovery defers flush/silence; ordinary correction remains active',
               'vendor write stall cannot hold the AudioTrack producer lock',
               'partial staged AudioTrack write exits cleanly on stop',
               'Actual VLC 4 shared clock and input/audio lifecycle tests under ASan/UBSan',
               'Native AudioTrack timing: stale/unsent rejection, raw-head fallback and frame counter wrap',
               'Rate changes with queued PCM, pause and paused seek tests',
               'packaged logger service declares dataSync and specialUse', 'logger repair code in DEX'],
    'device_tested': False,
}
(artifacts / 'build-info.json').write_text(json.dumps(info, ensure_ascii=False, indent=2) + '\n')
(artifacts / 'static-plugin-manifest.json').write_bytes(plugin_manifest_path.read_bytes())
print(json.dumps(info, ensure_ascii=False, indent=2))
