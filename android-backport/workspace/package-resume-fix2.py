#!/usr/bin/env python3
"""Sign and verify AudioTrack resume fix 2, retaining earlier artifacts."""
import hashlib
import json
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
artifacts = root / 'artifacts/resume-fix2'
artifacts.mkdir(parents=True, exist_ok=True)
source = root / 'app-stable/application/app/build/outputs/apk/release/VLC-Android-3.7.2-resume-fix2-all.apk'
assert source.is_file(), source
tool = root / 'toolchain'
sdk = tool / 'android-sdk/build-tools/36.0.0'
aligned = artifacts / 'VLC-3.7.2-resume-fix2-arm64-v8a-unsigned-aligned.apk'
apk = artifacts / 'VLC-3.7.2-resume-fix2-arm64-v8a.apk'
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
assert "versionCode='13070204'" in badging
assert "versionName='3.7.2-resume-fix2'" in badging
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
                   b'Android AAudio output (VLC 3 backport)'):
        assert marker in core, marker
    previous = json.loads((root / 'artifacts/resume-fix1/build-info.json').read_text())
    assert native['libvlc.so'] != previous['native_sha256']['libvlc.so']
    # Stable codec, medialibrary and C++ runtime payloads stay identical.
    assert native['libmla.so'] == previous['native_sha256']['libmla.so']
    assert native['libc++_shared.so'] == previous['native_sha256']['libc++_shared.so']
    diagnostic_previous = json.loads((root / 'artifacts/pause-diag1/build-info.json').read_text())
    assert native['libvlc.so'] != diagnostic_previous['native_sha256']['libvlc.so']
    assert b'[pause-fix] resume_notify' in core
    assert b'[pause-fix] writer_resumed' in core
    assert b'[pause-fix] nonblocking_write blocked_us=' in core
    assert b'audiotrack position is ahead after resume' in core
    dex_files = [package.read(name) for name in package.namelist() if name.endswith('.dex')]
    for marker in (b'Could not start diagnostic foreground service', b'DebugLogSnapshot', b'pause-diagnostics'):
        assert any(marker in dex for dex in dex_files), marker

info = {
    'apk': apk.name, 'bytes': apk.stat().st_size,
    'sha256': hashlib.sha256(apk.read_bytes()).hexdigest(),
    'package': 'org.videolan.vlc.aaudio', 'version_name': '3.7.2-resume-fix2',
    'version_code': 13070204, 'abi': 'arm64-v8a', 'core_version': '3.0.25',
    'native_sha256': native, 'updates_previous_personal_build': True,
    'purpose': 'Avoid vendor AudioTrack blocking writes on resume; retain normal drift correction',
    'diagnostic_window_seconds': 5, 'max_samples_per_resume_per_stage': 32,
    'max_audio_correction_events_per_resume': 48,
    'app_commit': 'e5740f713d995f6c8e7d92e64134abc3a3b6987d',
    'jni_commit': '0b8dc65efb203c86a0476bc337ddd99ecf1c0ef6',
    'core_commit_before_android_patches': '84177b2273abc5c4300234e6e18b55a4d2dd5a03',
    'core_source_commit': core_source_commit,
    'fork_branch': 'android/3.7.2-resume-fix',
    'checks': ['Release assembly and lint', 'APK signature v2', 'same signing certificate',
               'increased version code', '16KB ZIP alignment', 'arm64 ELF',
               'custom native library hashes', 'diagnostic and resume fix markers', 'unchanged medialibrary/runtime',
               'original AudioTrack source reproduces retained-queue stall', 'fixed writer resumes without new data: 20 runs, ASan/UBSan',
               'non-blocking AudioTrack write checks full internal buffer',
               'phone log proved a 383 ms write call blocked after Bluetooth resume',
               'packaged logger service declares dataSync and specialUse', 'logger repair code in DEX'],
    'device_tested': False,
}
(artifacts / 'build-info.json').write_text(json.dumps(info, ensure_ascii=False, indent=2) + '\n')
print(json.dumps(info, ensure_ascii=False, indent=2))
