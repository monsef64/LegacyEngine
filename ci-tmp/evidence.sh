#!/bin/bash
# TEMPORARY (test run of the release workflow on a real runner).
#
# evidence.sh <image> <readme|sdl3>
#
# Follows the Build section of the README in a container of a clean system:
#   readme: with the packages the README lists (SDL gets downloaded)
#   sdl3:   with the distribution's SDL3 package instead (it has to be picked up)
# Annotations are the only output of a run that can be read back from where the
# workflow was written, so the result is reported as a notice (4096 characters).
set -u

image=$1
mode=$2
out=$RUNNER_TEMP/evidence-$mode-$(printf '%s' "$image" | tr ':/' '--')
mkdir -p "$out"

# The commands exactly as the README gives them.
python3 - "$out" <<'PY'
import re
import sys

out = sys.argv[1]
readme = open("README.md").read()
build = readme.split("\n## Build\n", 1)[1].split("\n## ", 1)[0]
blocks = re.findall(r"```sh\n(.*?)```", build, re.S)
open(out + "/readme-apt.sh", "w").write(blocks[0])
open(out + "/readme-build.sh", "w").write(blocks[1])
PY
git archive --format=tar HEAD > "$out/src.tar"

result=0
# shellcheck disable=SC2016  # the script is for the shell inside the container
timeout 1500 docker run --rm -v "$out:/t:ro" -e MODE="$mode" "$image" bash -c '
  echo "APT::Get::Assume-Yes \"true\";" > /etc/apt/apt.conf.d/90yes
  echo "debconf debconf/frontend select Noninteractive" | debconf-set-selections
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq > /dev/null 2>&1
  apt-get install -qq sudo xvfb xauth libgl1-mesa-dri > /tmp/pre.log 2>&1 || { echo "preparation failed"; tail -n 5 /tmp/pre.log; exit 90; }
  mkdir /work && cd /work && tar -xf /t/src.tar
  . /etc/os-release
  echo "$PRETTY_NAME, $(nproc) CPUs"

  start=$SECONDS
  if [ "$MODE" = readme ]; then
    bash -e /t/readme-apt.sh > /tmp/apt.log 2>&1 || { echo "the apt command of the README failed"; tail -n 8 /tmp/apt.log; exit 91; }
    echo "the apt command of the README: ok in $((SECONDS - start)) s"
  else
    apt-get install -qq build-essential cmake libsdl3-dev > /tmp/apt.log 2>&1 || { echo "apt failed"; tail -n 8 /tmp/apt.log; exit 91; }
    echo "build-essential cmake libsdl3-dev: ok in $((SECONDS - start)) s, libsdl3-dev $(dpkg-query -W -f="\${Version}" libsdl3-dev)"
  fi
  echo "$(cmake --version | head -n 1), $(g++ --version | head -n 1)"

  for n in 1 2; do
    command=$(sed -n "${n}p" /t/readme-build.sh)
    start=$SECONDS
    bash -e -c "$command" > "/tmp/step$n.log" 2>&1 || { echo "$command: FAILED"; tail -n 12 "/tmp/step$n.log"; exit 92; }
    echo "$command: ok in $((SECONDS - start)) s"
  done
  grep -h "LegacyEngine:" /tmp/step1.log
  echo "compiler warnings: $(grep -c "warning:" /tmp/step2.log), of which in src/main.cpp: $(grep -c "main.cpp.*warning:" /tmp/step2.log)"
  echo "SDL libraries the executable loads: $(ldd build/LegacyEngine | grep -i sdl | tr -s " \t" " " || true)"

  command=$(sed -n 3p /t/readme-build.sh)
  status=0
  # shellcheck disable=SC2086
  xvfb-run -a -s "-screen 0 1280x720x24" timeout -k 5 8 $command || status=$?
  echo "$command: exit status $status when stopped after 8 s (124 = was still running)"
' > "$out/log" 2>&1 || result=$?

printf '::notice title=%s::%s\n' "build on $(printf '%s' "$image" | tr ':/' '--') ($mode)" \
  "$(printf 'docker exit status: %s\n%s' "$result" "$(grep -vE '^(Unable to find image|[0-9a-f]{12}: |Digest: |Status: |.*: Pulling from )' "$out/log" | tail -n 30)" | head -c 3900 | sed -z 's/%/%25/g; s/\r/%0D/g; s/\n/%0A/g')"
exit 0
