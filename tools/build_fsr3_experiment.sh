#!/usr/bin/env bash
# Reproduce the *standalone* Linux FSR 3.1 SDK feasibility build. This does
# not enable FSR in LegacyEngine. See docs/fsr3-integration.md before use.
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <new-work-directory>" >&2
    exit 2
fi
if [[ $(uname -s) != Linux ]]; then
    echo "The experimental SDK port only supports Linux." >&2
    exit 2
fi
for tool in curl sha256sum tar patch cmake glslangValidator python3; do
    command -v "$tool" >/dev/null || { echo "Missing required tool: $tool" >&2; exit 2; }
done

# Do not overwrite a user's sources or silently reuse a partially patched SDK.
work=$1
if [[ -e "$work" ]]; then
    echo "Work directory already exists: $work (choose a new directory)" >&2
    exit 2
fi
mkdir -p "$work"
work=$(cd "$work" && pwd)

port_commit=fa51577c9bc2c3477e64dfdd358796aebb9ca45b
sdk_commit=c6efa6bf7f2027b3ec94f28578bb5965eabb9e55
port_hash=4b0ee0501fb12218f5556beefa28c373e9efd91415491ec04e86ef9344043dc4
sdk_hash=eff7e8e371b2d461b980f8c75583133f771e940ed7aa00779dbbaf6dd2f88746
curl -fL --retry 3 "https://codeload.github.com/JonathanOrr/xplane-fsr3/tar.gz/$port_commit" -o "$work/port.tar.gz"
curl -fL --retry 3 "https://codeload.github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tar.gz/$sdk_commit" -o "$work/sdk.tar.gz"
printf '%s  %s\n%s  %s\n' "$port_hash" "$work/port.tar.gz" "$sdk_hash" "$work/sdk.tar.gz" | sha256sum -c -
mkdir "$work/port" "$work/sdk"
tar -xzf "$work/port.tar.gz" --strip-components=1 -C "$work/port"
tar -xzf "$work/sdk.tar.gz" --strip-components=1 -C "$work/sdk"
for patch in fidelityfx-sdk-v1.1.4-linux.patch fsr-3.1.5-backport.patch; do
    (cd "$work/sdk" && patch --batch --forward -p1 -i "$work/port/fsr3-linux/patches/$patch")
done

# The original generator uses all available hardware threads per permutation;
# bound memory and CPU use. The only change is its concurrency argument.
shader_jobs=${FSR3_SHADER_JOBS:-2}
[[ "$shader_jobs" =~ ^[1-9][0-9]*$ ]] || { echo "Invalid FSR3_SHADER_JOBS" >&2; exit 2; }
python3 - "$work/port/fsr3-linux/gen_shaders.sh" "$shader_jobs" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
text = path.read_text()
old = 'BASE=(-reflection '
if text.count(old) != 1:
    raise SystemExit('Unexpected shader generator; refusing to modify it')
path.write_text(text.replace(old, f'BASE=(-num-threads={sys.argv[2]} -reflection '))
PY
# The port invokes glslangValidator from PATH and requires SPIR-V optimization.
cat > "$work/probe.comp" <<'EOF'
#version 450
layout(local_size_x=1) in;
void main() {}
EOF
glslangValidator -V --target-env vulkan1.2 -Os "$work/probe.comp" -o "$work/probe.spv" >/dev/null
cmake -S "$work/port/fsr3-linux" -B "$work/build" \
    -DCMAKE_BUILD_TYPE=Release -DFFX_SDK="$work/sdk/sdk"
cmake --build "$work/build" --parallel "${FSR3_BUILD_JOBS:-2}" --target fsr3_selftest
printf '\nStandalone test built: %s/build/fsr3_selftest\n' "$work"
printf 'Run on a Vulkan device: FSR_DEVICE=<name-substring> %s/build/fsr3_selftest\n' "$work"
printf 'This standalone test does not test or enable FSR in LegacyEngine.\n'
