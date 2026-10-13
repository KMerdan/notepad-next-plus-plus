#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate plugin runtime closure and stage independently named binary packages."""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def version():
    return re.search(r'\bVERSION\s+(\d+\.\d+\.\d+)', (ROOT/'CMakeLists.txt').read_text()).group(1)

def require(file):
    if not file.is_file() or file.stat().st_size == 0:
        raise ValueError(f'Required package member is missing: {file}')

def one(pattern):
    files = list(ROOT.glob(pattern))
    if len(files) != 1:
        raise ValueError(f'Expected exactly one artifact for {pattern}, got {len(files)}')
    require(files[0])
    return files[0]

def stage(platform, arch, output):
    output.mkdir()
    cpu = 'x86_64' if arch == 'x64' else arch
    prefix = f'NotepadNextPlusPlus-{version()}'
    if platform == 'darwin':
        record = json.loads(one('build/plugin-packages/*/package-record.json').read_text())
        if record['architecture'] != cpu or not record['dmg'] or not record['signedNodeSha256']:
            raise ValueError('macOS package architecture/runtime validation failed')
        image = Path(record['dmg']['path'])
        require(image)
        if hashlib.sha256(image.read_bytes()).hexdigest() != record['dmg']['sha256']:
            raise ValueError('Disk image differs from its validated package record')
        shutil.copyfile(image, output/f'{prefix}-macos-{cpu}.dmg')
    else:
        package = ROOT/('build/package' if platform == 'win' else 'build/AppDir/usr/bin')
        require(package/'plugins/runtime'/('node.exe' if platform == 'win' else 'node'))
        for name in ['host.cjs','client.cjs','wire.schema.json','runtime-lock.json','NODE-LICENSE','LICENSE']:
            require(package/'plugins/host'/name)
        engines = list(package.rglob('QtWebEngineProcess*')) if platform == 'win' else list((ROOT/'build/AppDir').rglob('QtWebEngineProcess*'))
        if not any(p.is_file() for p in engines):
            raise ValueError('Qt WebEngine subprocess is missing from the package')
        if platform == 'win':
            shutil.copyfile(one('build/NotepadNextPlusPlus-*.zip'),output/f'{prefix}-windows-{cpu}.zip')
            shutil.copyfile(one('installer/NotepadNextPlusPlus-*.exe'),output/f'{prefix}-windows-{cpu}-installer.exe')
        else:
            shutil.copyfile(one('build/NotepadNextPlusPlus-*.AppImage'),output/f'{prefix}-linux-{cpu}.AppImage')

def finalize(directory, tag):
    if tag != 'v'+version():
        raise ValueError('Release tag must match the application version')
    prefix = f'NotepadNextPlusPlus-{version()}'
    expected = {f'{prefix}-macos-arm64.dmg',f'{prefix}-macos-x86_64.dmg',f'{prefix}-windows-x86_64.zip',f'{prefix}-windows-x86_64-installer.exe',f'{prefix}-linux-x86_64.AppImage'}
    if {p.name for p in directory.iterdir()} != expected:
        raise ValueError('Release must contain exactly all five platform packages')
    checks=[]
    for name in sorted(expected):
        file=directory/name;require(file)
        digest=hashlib.file_digest(file.open('rb'),'sha256').hexdigest()
        checks.append(f'{digest}  {name}')
    (directory/'SHA256SUMS').write_text('\n'.join(checks)+'\n')

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform',choices=['darwin','win','linux'])
    parser.add_argument('--arch',choices=['arm64','x64'])
    parser.add_argument('--output',type=Path)
    parser.add_argument('--finalize',type=Path)
    parser.add_argument('--tag')
    args=parser.parse_args()
    if args.finalize:
        finalize(args.finalize,args.tag)
    elif args.platform and args.arch and args.output:
        stage(args.platform,args.arch,args.output)
    else:
        parser.error('Supply platform/arch/output or finalize/tag')
