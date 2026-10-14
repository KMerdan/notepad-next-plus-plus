#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Inspect the actual bundle, not the development SDK or build manifest."""
import argparse
import configparser
import json
import os
import plistlib
import re
import subprocess
from pathlib import Path

MAGIC={b'\xfe\xed\xfa\xce',b'\xce\xfa\xed\xfe',b'\xfe\xed\xfa\xcf',b'\xcf\xfa\xed\xfe',b'\xca\xfe\xba\xbe',b'\xbe\xba\xfe\xca',b'\xca\xfe\xba\xbf',b'\xbf\xba\xfe\xca'}
def run(args):
    return subprocess.run(args,check=True,capture_output=True,text=True).stdout
def node_environment():
    return {key:value for key,value in os.environ.items() if not key.startswith(('NODE_','DYLD_'))}
def version(value):
    if not isinstance(value,str) or not re.fullmatch(r'\d+\.\d+(?:\.\d+)?',value):
        raise ValueError('Invalid deployment version: '+str(value))
    return tuple(int(part) for part in value.split('.'))+(0,)*(3-len(value.split('.')))
def contained(path,root):
    return path.resolve().is_relative_to(root.resolve())
def machos(app):
    result=[]
    for path in app.rglob('*'):
        if path.is_symlink():
            if not path.exists() or not contained(path,app):raise ValueError('Escaping/broken bundle symlink: '+str(path))
        elif path.is_file():
            with path.open('rb') as file:magic=file.read(4)
            if magic in MAGIC:result.append(path)
    return sorted(result)
def metadata(path,arch):
    architectures=run(['/usr/bin/lipo','-archs',str(path)]).strip().split()
    if arch not in architectures:raise ValueError('Missing '+arch+' in '+str(path))
    load=run(['/usr/bin/otool','-arch',arch,'-l',str(path)])
    paths=re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset',load)
    target=re.search(r'cmd LC_BUILD_VERSION\s+cmdsize \d+\s+platform \d+\s+minos (\S+)',load)
    if not target:target=re.search(r'cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version (\S+)',load)
    if not target:raise ValueError('Missing macOS deployment command: '+str(path))
    links=run(['/usr/bin/otool','-arch',arch,'-L',str(path)])
    dependencies=re.findall(r'^\s+(.+?) \(compatibility version',links,re.M)
    return {'architectures':architectures,'minimumOS':target.group(1),'rpaths':paths,'dependencies':dependencies}
def expand(value,loader,executable):
    if value.startswith('@loader_path/'):return loader/value[13:]
    if value.startswith('@executable_path/'):return executable/value[17:]
    if value.startswith('/'):return Path(value)
    raise ValueError('Unsupported load path: '+value)
def entitlements(path):
    # Current codesign displays DER-backed entitlements as a typed dictionary;
    # --xml can return nothing for a valid DER signature with no legacy XML.
    result=subprocess.run(['/usr/bin/codesign','-d','--entitlements','-',str(path)],check=True,capture_output=True,text=True)
    # Older supported macOS releases emit XML instead of the typed display.
    if result.stdout.lstrip().startswith('<?xml'):
        return plistlib.loads(result.stdout.encode())
    text=result.stdout+'\n'+result.stderr
    return {name:value=='true' for name,value in re.findall(r'\[Key\]\s+(\S+)\s+\[Value\]\s+\[Bool\]\s+(true|false)',text)}
def qt_config(app):
    config=configparser.ConfigParser(interpolation=None)
    config.read_string((app/'Contents/Resources/qt.conf').read_text())
    for section in config.sections():
        if section.lower() not in ['paths','effectivepaths']:continue
        prefix=app/'Contents'/config[section].get('prefix','.')
        for key,value in config[section].items():
            for entry in value.split(','):
                path=Path(entry.strip())
                if path.is_absolute():raise ValueError('Machine-specific qt.conf path: '+key)
                candidate=app/'Contents'/path if key=='prefix' else prefix/path
                if not contained(candidate,app):raise ValueError('Escaping qt.conf path: '+key)
        if not (prefix/config[section].get('plugins','PlugIns')).is_dir():raise ValueError('Missing configured Qt plugin directory')
    if 'Paths' not in config:raise ValueError('Missing Qt Paths configuration')
    return {section:dict(config[section]) for section in config.sections()}
def inspect(app,arch,require_signatures=True,require_node=True):
    app=app.resolve(strict=True);info=plistlib.loads((app/'Contents/Info.plist').read_bytes())
    executable=info['CFBundleExecutable']
    icon=info.get('CFBundleIconFile','')
    if not re.fullmatch(r'[A-Za-z0-9_.-]+',icon):raise ValueError('Missing or unsafe bundle icon declaration')
    icon_file=app/'Contents/Resources'/icon
    if not icon_file.is_file() or icon_file.read_bytes()[:4]!=b'icns':raise ValueError('Declared application icon is missing or invalid')
    if not re.fullmatch(r'[A-Za-z0-9_.-]+',executable):raise ValueError('Unsafe executable name')
    main=app/'Contents/MacOS'/executable;minimum=info['LSMinimumSystemVersion'];version(minimum)
    if not info.get('CFBundleShortVersionString') or not info.get('CFBundleVersion'):raise ValueError('Missing bundle version')
    if require_node and version(minimum)<version('13.5'):raise ValueError('Node profile needs macOS >=13.5')
    binaries=machos(app);all_info={file:metadata(file,arch) for file in binaries};main_info=all_info[main]
    main_keys={};node_keys={};helper_keys=[]
    if require_signatures:
        signature=subprocess.run(['/usr/bin/codesign','-d','--verbose=4',str(main)],check=True,capture_output=True,text=True)
        if not re.search(r'flags=.*\([^\n]*runtime',signature.stderr):raise ValueError('Main executable has no hardened runtime signature')
        main_keys=entitlements(main)
        if main_keys.get('com.apple.security.get-task-allow') or main_keys.get('com.apple.security.cs.allow-dyld-environment-variables'):raise ValueError('Unsafe main executable entitlements')
    for file,data in all_info.items():
        if version(data['minimumOS'])>version(minimum):raise ValueError('Bundle declares an OS lower than '+str(file))
        nearest=next((p for p in file.parents if p.suffix=='.app'),app)
        entry_dir=nearest/'Contents/MacOS'
        roots=[expand(p,file.parent,entry_dir).resolve() for p in data['rpaths']]
        roots+=[expand(p,main.parent,main.parent).resolve() for p in main_info['rpaths']]
        if any(not contained(root,app) for root in roots):raise ValueError('External runtime search path: '+str(file))
        for dependency in data['dependencies']:
            if dependency.startswith(('/usr/lib/','/System/Library/')):continue
            candidates=[root/dependency[7:] for root in roots] if dependency.startswith('@rpath/') else [expand(dependency,file.parent,entry_dir)]
            if not any(candidate.is_file() and contained(candidate,app) for candidate in candidates):raise ValueError('Unresolved/external dependency: '+dependency+' in '+str(file))
        if require_signatures:subprocess.run(['/usr/bin/codesign','--verify','--strict',str(file)],check=True,capture_output=True)
    node=app/'Contents/Helpers/nnp-node'
    if require_node:
        if node not in binaries:raise ValueError('Missing controlled Node helper')
        lock=json.loads((app/'Contents/Resources/plugin-host/runtime-lock.json').read_text())
        if subprocess.run([str(node),'--version'],env=node_environment(),check=True,capture_output=True,text=True).stdout.strip()!=lock['version']:raise ValueError('Wrong Node version')
        node_keys=entitlements(node) if require_signatures else {}
        expected=plistlib.loads((Path(__file__).resolve().parents[1]/'deploy/macos/node.entitlements').read_bytes())
        if require_signatures and node_keys!=expected:raise ValueError('Invalid production Node entitlements')
    webengine=app/'Contents/Frameworks/QtWebEngineCore.framework'
    if webengine.exists():
        helpers=sorted({path.resolve() for path in webengine.glob('Versions/*/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess')})
        if not helpers:raise ValueError('Missing WebEngine helper')
        resources=webengine/'Resources'
        for name in ['icudtl.dat','qtwebengine_resources.pak','qtwebengine_resources_100p.pak','qtwebengine_resources_200p.pak',f'v8_context_snapshot.{arch}.bin','qtwebengine_locales/en-US.pak']:
            if not (resources/name).is_file():raise ValueError('Missing WebEngine resource: '+name)
        for helper in helpers:
            if require_signatures:
                keys=entitlements(helper)
                if not keys.get('com.apple.security.cs.allow-jit') or keys.get('com.apple.security.get-task-allow') or keys.get('com.apple.security.cs.allow-dyld-environment-variables'):raise ValueError('Invalid WebEngine helper entitlements')
                helper_keys.append(keys)
    if not (app/'Contents/PlugIns/platforms/libqcocoa.dylib').is_file():raise ValueError('Missing Cocoa plugin')
    config=qt_config(app)
    if require_signatures:subprocess.run(['/usr/bin/codesign','--verify','--deep','--strict',str(app)],check=True,capture_output=True)
    return {'bundle':str(app),'architecture':arch,'version':info['CFBundleShortVersionString'],'declaredMinimumOS':minimum,'machoFiles':len(binaries),'signaturesVerified':require_signatures,'allDependenciesContainedOrSystem':True,'nodeRequired':require_node,'webengineIncluded':webengine.exists(),'qtConfig':config,'mainHardenedRuntime':require_signatures,'mainEntitlements':main_keys,'nodeEntitlements':node_keys,'webEngineHelperEntitlements':helper_keys,'machos':{str(file.relative_to(app)):data for file,data in all_info.items()}}
def main():
    parser=argparse.ArgumentParser();parser.add_argument('app',type=Path);parser.add_argument('--arch',choices=['arm64','x86_64'],required=True);parser.add_argument('--output',type=Path);parser.add_argument('--skip-signatures',action='store_true');args=parser.parse_args()
    record=inspect(args.app,args.arch,not args.skip_signatures)
    text=json.dumps(record,indent=2)
    if args.output:args.output.write_text(text+'\n')
    print(json.dumps({name:value for name,value in record.items() if name!='machos'}))
if __name__=='__main__':main()
