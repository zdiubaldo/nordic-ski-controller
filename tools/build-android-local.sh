#!/usr/bin/env bash
# Build a standalone, locally signed trial APK. No Expo login or cloud service.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
project_dir=$PWD
trial_architectures="${NORDIC_ANDROID_ARCHITECTURES:-arm64-v8a,armeabi-v7a}"
trial_apk="${NORDIC_ANDROID_APK_NAME:-nordic-ski-trial.apk}"
[[ "$trial_apk" =~ ^[a-zA-Z0-9._-]+\.apk$ ]] || { echo "Invalid APK filename" >&2; exit 1; }
export EXPO_NO_TELEMETRY=1
export EXPO_OFFLINE=1
export JAVA_HOME="${JAVA_HOME:-$(find "$project_dir/.local/android-tools/java" -mindepth 1 -maxdepth 1 -type d -print -quit)}"
export ANDROID_HOME="${ANDROID_HOME:-$project_dir/.local/android-tools/sdk}"
export GRADLE_USER_HOME="$project_dir/.local/android-tools/gradle"
if ! command -v node >/dev/null 2>&1; then
  export PATH="$HOME/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin:$PATH"
fi
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$PATH"
[[ -x "$JAVA_HOME/bin/javac" && -d "$ANDROID_HOME/platforms/android-36" ]] || {
  echo 'Install a JDK and Android SDK first; see docs/mobile-trial.md.' >&2; exit 1;
}
node mobile/scripts/bundle-web.cjs
(cd mobile && node node_modules/expo/bin/cli prebuild --platform android --no-install)
mkdir -p .local/android-signing .local/artifacts
chmod 700 .local/android-signing
export NORDIC_TRIAL_KEYSTORE="$project_dir/.local/android-signing/trial.jks"
export NORDIC_TRIAL_PASSWORD_FILE="$project_dir/.local/android-signing/password"
if [[ ! -f "$NORDIC_TRIAL_KEYSTORE" ]]; then
  python3 - <<'PY'
import os, secrets
path = os.environ['NORDIC_TRIAL_PASSWORD_FILE']
with open(path, 'w') as f: f.write(secrets.token_hex(24))
os.chmod(path, 0o600)
PY
  keytool -genkeypair -keystore "$NORDIC_TRIAL_KEYSTORE" -storepass:file "$NORDIC_TRIAL_PASSWORD_FILE" \
    -keypass:file "$NORDIC_TRIAL_PASSWORD_FILE" -alias nordic-trial -keyalg RSA -keysize 3072 \
    -validity 3650 -dname 'CN=Nordic Ski Local Trial' >/dev/null 2>&1
fi
python3 - <<'PY'
from pathlib import Path
p = Path('mobile/android/app/build.gradle')
s = p.read_text()
if 'NORDIC_TRIAL_KEYSTORE' not in s:
    marker = '    signingConfigs {'
    assert marker in s
    s = s.replace(marker, marker + '''
        localTrial {
            storeFile file(System.getenv('NORDIC_TRIAL_KEYSTORE'))
            storePassword new File(System.getenv('NORDIC_TRIAL_PASSWORD_FILE')).text.trim()
            keyAlias 'nordic-trial'
            keyPassword new File(System.getenv('NORDIC_TRIAL_PASSWORD_FILE')).text.trim()
        }
''', 1)
    release = s.index('        release {')
    s = s[:release] + s[release:].replace('signingConfig signingConfigs.debug', 'signingConfig signingConfigs.localTrial', 1)
    p.write_text(s)
PY
# Default to physical phones; the emulator script requests x86_64 separately.
(cd mobile/android && ./gradlew --no-daemon --max-workers=2 -Dorg.gradle.jvmargs="-Xmx3072m -XX:MaxMetaspaceSize=1024m" -PreactNativeArchitectures="$trial_architectures" :app:assembleRelease)
cp mobile/android/app/build/outputs/apk/release/app-release.apk ".local/artifacts/$trial_apk"
"$ANDROID_HOME/build-tools/36.0.0/apksigner" verify ".local/artifacts/$trial_apk"
sha256sum ".local/artifacts/$trial_apk"
