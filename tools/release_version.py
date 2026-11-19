#!/usr/bin/env python3
"""Resolve CI build identity and validate release tags before expensive builds."""

import os
import re
import subprocess
from pathlib import Path

# Stable releases and SemVer prereleases; build metadata is intentionally omitted.
CORE = r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
SEMVER = re.compile(CORE + r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?")


def validate(version):
    match = SEMVER.fullmatch(version)
    if not match:
        raise SystemExit("Version must be MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-prerelease")
    if match[4] and any(part.isdigit() and len(part) > 1 and part[0] == "0"
                        for part in match[4].split(".")):
        raise SystemExit("Numeric prerelease identifiers cannot have leading zeroes")
    return version


requested = os.environ.get("RELEASE_VERSION", "").strip()
ref_type = os.environ.get("GITHUB_REF_TYPE", "")
ref_name = os.environ.get("GITHUB_REF_NAME", "")
event = os.environ.get("GITHUB_EVENT_NAME", "")
sha = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
tag = ""
# The checkbox only controls manual runs; pushing a version tag still publishes.
publish = event != "workflow_dispatch" or os.environ.get("PUBLISH_RELEASE", "true") != "false"
if event == "workflow_dispatch" and os.environ.get("PUBLISH_RELEASE") == "true" and not requested and ref_type != "tag":
    raise SystemExit("Enter a version to publish, or uncheck Publish GitHub Release for a build-only run")
if publish and ref_type == "tag":
    if not ref_name.startswith("v"):
        raise SystemExit("Release tags must begin with v")
    version = validate(ref_name[1:])
    tag = ref_name
elif publish and event == "workflow_dispatch" and requested:
    version = validate(requested.removeprefix("v"))
    tag = "v" + version
else:
    version = validate(Path("VERSION").read_text().strip()) + "-dev." + sha[:12]

if tag:
    # Prevent reruns/manual dispatches from attaching a different binary to a tag.
    existing = subprocess.run(["git", "rev-parse", "--verify", f"refs/tags/{tag}^{{commit}}"],
                              text=True, capture_output=True)
    if existing.returncode == 0 and existing.stdout.strip() != sha:
        raise SystemExit(f"{tag} already points to another commit; choose a new version")

values = {"version": version, "tag": tag, "prerelease": str(bool(tag and "-" in version)).lower()}
for key, value in values.items():
    print(f"{key}={value}")
if os.environ.get("GITHUB_OUTPUT"):
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        for key, value in values.items():
            output.write(f"{key}={value}\n")
