#!/usr/bin/env python3
"""Refresh packaged notices from vendored sources and a configured CMake build."""
import argparse
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=Path, help='Configured CMake build containing _deps')
args = parser.parse_args()
out = ROOT / 'data/license'
out.mkdir(parents=True, exist_ok=True)


def copy(source, target):
    destination = out / target
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def comments(source, target):
    text = source.read_text(encoding='utf-8', errors='replace')
    blocks = [block for block in re.findall(r'/\*.*?\*/', text, re.S)
              if any(term in block.lower() for term in
                     ('permission is hereby granted', 'redistribution and use',
                      'anyone is free to copy', "this software is provided 'as-is'",
                      'alternative a - mit license'))]
    if not blocks:
        raise RuntimeError(f'License comment not found in {source}')
    destination = out / target
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n\n'.join(dict.fromkeys(blocks)) + '\n', encoding='utf-8')


for folder, filename, target in [
    ('fontawesome', 'LICENSE.txt', 'FontAwesome.txt'),
    ('json', 'LICENSE.MIT', 'nlohmann-json.txt'), ('miniaudio', 'LICENSE', 'miniaudio.txt'),
    ('roboto', 'LICENSE', 'Roboto.txt'), ('simdjson', 'LICENSE', 'simdjson.txt'),
    ('stb', 'LICENSE', 'stb.txt')]:
    copy(ROOT / 'third_party' / folder / filename, target)
# simdjson distributes the complete Apache-2.0 text.
copy(ROOT / 'third_party/simdjson/LICENSE', 'Apache-2.0.txt')
for folder, filename, target in [
    ('sdl3', 'LICENSE.txt', 'SDL3.txt'), ('reactphysics3d', 'LICENSE', 'ReactPhysics3D.txt'),
    ('imgui', 'LICENSE.txt', 'DearImGui.txt'), ('imguizmo', 'LICENSE', 'ImGuizmo.txt'),
    ('assimp', 'LICENSE', 'assimp.txt')]:
    copy(args.build / '_deps' / (folder + '-src') / filename, target)
comments(args.build / '_deps/lua-src/lua.h', 'Lua.txt')
for dependency, subtree in [('assimp', 'contrib'), ('sdl3', 'src')]:
    source = args.build / '_deps' / (dependency + '-src') / subtree
    for path in sorted(source.rglob('*')):
        if path.is_file() and path.name.lower().startswith(('license', 'unlicense', 'copying', 'notice')):
            copy(path, Path(dependency + '-bundled') / path.relative_to(source))
for source, target in [
    ('pugixml/src/pugixml.hpp', 'pugixml.txt'), ('Open3DGC/o3dgcCommon.h', 'Open3DGC.txt'),
    ('Open3DGC/o3dgcArithmeticCodec.cpp', 'Open3DGC-ArithmeticCodec.txt'),
    ('unzip/unzip.h', 'MiniZip.txt'), ('zip/src/miniz.h', 'miniz.txt'),
    ('stb/stb_image.h', 'stb_image.txt')]:
    comments(args.build / '_deps/assimp-src/contrib' / source, 'assimp-bundled/' + target)
print(f'Refreshed notices in {out}')
