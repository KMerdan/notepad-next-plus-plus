#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a fresh deployed/signable plugin bundle without modifying the build app."""
import argparse
import hashlib
import importlib.util
import json
import plistlib
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

REPO=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('bundle_verifier',REPO/'scripts/verify-macos-bundle.py')
verifier=importlib.util.module_from_spec(spec);spec.loader.exec_module(verifier)
def sha(file):
    digest=hashlib.sha256()
    with file.open('rb') as source:
        for block in iter(lambda:source.read(1024*1024),b''):digest.update(block)
    return digest.hexdigest()
def checked_node(app,archive,arch):
    if not archive.is_file():raise ValueError('Provide the official Node archive with --node-archive / NOTEPADNEXT_MACOS_NODE_ARCHIVE')
    lock=json.loads((REPO/'packages/plugin-host/runtime-lock.json').read_text());name=f"node-{lock['version']}-darwin-{'x64' if arch=='x86_64' else 'arm64'}"
    if sha(archive)!=lock['archives'][name+'.tar.xz']:raise ValueError('Node archive does not match the controlled runtime lock')
    with tarfile.open(archive,'r:xz') as package:
        member=package.getmember(name+'/bin/node')
        if not member.isfile() or member.size>256*1024*1024:raise ValueError('Invalid Node archive member')
        digest=hashlib.sha256()
        with package.extractfile(member) as source:
            for block in iter(lambda:source.read(1024*1024),b''):digest.update(block)
    node=app/'Contents/Helpers/nnp-node'
    if sha(node)!=digest.hexdigest():raise ValueError('Bundled Node differs from the verified official archive')
    return {'archiveSha256':sha(archive),'originalBinarySha256':digest.hexdigest(),'version':lock['version']}
def notices(app,qt):
    destination=app/'Contents/Resources/licenses';destination.mkdir(exist_ok=False)
    for file,name in [(REPO/'LICENSE','NOTEPADNEXT-LICENSE'),(REPO/'thirdparty/scintilla/License.txt','SCINTILLA-LICENSE'),(REPO/'thirdparty/lexilla/License.txt','LEXILLA-LICENSE'),(REPO/'thirdparty/uchardet/COPYING','UCHARDET-COPYING')]:shutil.copyfile(file,destination/name)
    lua=(REPO/'thirdparty/lua/src/lua.h').read_text()
    start=lua.index('Copyright (C) 1994-2017 Lua.org, PUC-Rio.',lua.index('struct lua_Debug'))
    end=lua.index('******************************************************************************/',start)
    (destination/'LUA-LICENSE').write_text('\n'.join(line.removeprefix('* ').removeprefix('*') for line in lua[start:end].splitlines())+'\n')
    # The SDK's SPDX data is specific to this binary release, including the
    # extracted Chromium notices; a generic latest Qt web page cannot replace it.
    selected=[]
    for file in sorted((qt/'sbom').glob('*.spdx.json')):
        if file.stat().st_size>32*1024*1024:raise ValueError('Excessive Qt SBOM')
        selected.append(file);shutil.copyfile(file,destination/file.name)
    if not selected:raise ValueError('Qt release SBOM is required; full license/source review remains a separate release gate')
    lines=['Qt SDK attribution inventory. Exact installed SDK documents are preserved.','This inventory is not a declaration that all distribution license obligations are satisfied.','']
    for file in selected:
        document=json.loads(file.read_text());lines.append('Document: '+file.name)
        for package in document.get('packages',[]):
            lines.append('\n'.join(str(package.get(key,'')) for key in ['name','versionInfo','copyrightText','licenseDeclared','downloadLocation']))
        for item in document.get('hasExtractedLicensingInfos',[]):lines.append(item.get('licenseId','')+'\n'+item.get('extractedText',''))
    (destination/'QT-ATTRIBUTIONS.txt').write_text('\n\n'.join(lines)+'\n')
    return {'sdk':str(qt),'sbomDocuments':len(selected),'files':{file.name:sha(file) for file in destination.iterdir() if file.is_file()},'fullLicenseAndSourceGateComplete':False}
def sign(app,identity,log):
    node_profile=REPO/'deploy/macos/node.entitlements'
    profiles=app/'Contents/Resources/plugin-host/signing';profiles.mkdir();shutil.copyfile(node_profile,profiles/'node.entitlements')
    main_profile=None
    if identity=='-':
        # Ad-hoc signatures have no common Team ID. This development-only
        # exception permits the app to load its separately sealed Qt libraries.
        main_profile=profiles/'ad-hoc-main.entitlements'
        main_profile.write_bytes(plistlib.dumps({'com.apple.security.cs.disable-library-validation':True}))
    def command(path,entitlement=None):
        args=['/usr/bin/codesign','--force','--sign',identity,'--options','runtime','--timestamp=none' if identity=='-' else '--timestamp']
        if entitlement:args+=['--entitlements',str(entitlement)]
        subprocess.run([*args,str(path)],stdout=log,stderr=log,check=True)
    def helper_profile(path):
        root=path if path.suffix=='.app' else next((p for p in path.parents if p.name=='QtWebEngineProcess.app'),None)
        profile=root/'Contents/Resources/QtWebEngineProcess.entitlements' if root else None
        if not profile or not profile.is_file():raise ValueError('Missing SDK WebEngine entitlement profile')
        return profile
    for file in verifier.machos(app):
        profile=node_profile if file.name=='nnp-node' else helper_profile(file) if file.name=='QtWebEngineProcess' else None
        command(file,profile)
    bundles=[p for p in app.rglob('*') if p.is_dir() and not p.is_symlink() and p.suffix in ['.framework','.app']]
    for bundle in sorted(bundles,key=lambda p:len(p.parts),reverse=True):command(bundle,helper_profile(bundle) if bundle.name=='QtWebEngineProcess.app' else None)
    command(app,main_profile)
def sign_disk_image(path,identity,log):
    subprocess.run(['/usr/bin/codesign','--force','--sign',identity,'--timestamp=none' if identity=='-' else '--timestamp',str(path)],stdout=log,stderr=log,check=True)
    subprocess.run(['/usr/bin/codesign','--verify','--strict',str(path)],stdout=log,stderr=log,check=True)
def notarize(path,profile,log):
    result=subprocess.run(['xcrun','notarytool','submit',str(path),'--keychain-profile',profile,'--wait','--output-format','json'],capture_output=True,text=True)
    log.write(result.stdout+'\n'+result.stderr);log.flush();result.check_returncode()
    receipt=json.loads(result.stdout)
    if receipt.get('status')!='Accepted':raise ValueError('Apple notarization was not accepted; see notarization.log')
    subprocess.run(['xcrun','stapler','staple',str(path)],stdout=log,stderr=log,check=True)
    subprocess.run(['xcrun','stapler','validate',str(path)],stdout=log,stderr=log,check=True)
    return receipt
def package(args):
    source=args.app.resolve(strict=True);qt=args.qt.resolve(strict=True)
    profile=json.loads((source/'Contents/Resources/plugin-host/build-profile.json').read_text())
    if not profile.get('nodeBundled') or profile.get('developerPythonConfigured'):
        raise ValueError('Portable Node packaging needs a bundled Node and no developer-only absolute Python interpreter; configure a separate release build')
    runtime=checked_node(source,args.node_archive,args.arch)
    notary_profile=getattr(args,'notary_profile','')
    if notary_profile and args.identity=='-':raise ValueError('Notarization requires a configured Developer ID Application identity')
    args.output_dir.mkdir(parents=True,exist_ok=True)
    root=Path(tempfile.mkdtemp(prefix='NotepadNext-'+args.arch+'-',dir=args.output_dir))
    payload=root/'payload';payload.mkdir();app=payload/'NotepadNextPlusPlus.app'
    subprocess.run(['/usr/bin/ditto',str(source),str(app)],check=True)
    with (root/'deploy.log').open('w') as log:
        subprocess.run([str(qt/'bin/macdeployqt'),str(app),'-no-strip','-verbose=2'],stdout=log,stderr=log,check=True)
        inventory=notices(app,qt)
        # Verify closure before signing. macdeployqt can return success while
        # an optional deployed plugin still refers to a missing framework.
        verifier.inspect(app,args.arch,require_signatures=False)
        sign(app,args.identity,log)
    app_notarization=None
    if notary_profile:
        identity=subprocess.run(['/usr/bin/codesign','-d','--verbose=4',str(app)],check=True,capture_output=True,text=True).stderr
        if 'Authority=Developer ID Application:' not in identity:raise ValueError('Notarization requires Developer ID Application signatures')
        archive=root/'notary-submission.zip'
        subprocess.run(['/usr/bin/ditto','-c','-k','--keepParent','--sequesterRsrc',str(app),str(archive)],check=True)
        # ZIPs are accepted submissions but cannot be stapled. Staple the
        # accepted application before creating the final disk image.
        with (root/'notarization.log').open('w') as log:
            result=subprocess.run(['xcrun','notarytool','submit',str(archive),'--keychain-profile',notary_profile,'--wait','--output-format','json'],capture_output=True,text=True)
            log.write(result.stdout+'\n'+result.stderr);log.flush();result.check_returncode();app_notarization=json.loads(result.stdout)
            if app_notarization.get('status')!='Accepted':raise ValueError('Apple application notarization was not accepted; see notarization.log')
            subprocess.run(['xcrun','stapler','staple',str(app)],stdout=log,stderr=log,check=True)
            subprocess.run(['xcrun','stapler','validate',str(app)],stdout=log,stderr=log,check=True)
    validation=verifier.inspect(app,args.arch)
    (root/'bundle-validation.json').write_text(json.dumps(validation,indent=2)+'\n')
    (payload/'Applications').symlink_to('/Applications',target_is_directory=True)
    record={'bundle':str(app),'architecture':args.arch,'runtime':runtime,'signedNodeSha256':sha(app/'Contents/Helpers/nnp-node'),'signing':'ad-hoc-development' if args.identity=='-' else 'configured-identity','notarized':bool(app_notarization),'applicationNotarization':app_notarization,'cleanMachineValidated':False,'attributions':inventory,'bundleValidation':'bundle-validation.json','appBytes':sum(file.stat().st_size for file in app.rglob('*') if file.is_file() and not file.is_symlink())}
    if args.dmg:
        image=root/f"NotepadNext-{validation['version']}-{args.arch}.dmg"
        with (root/'dmg.log').open('w') as log:subprocess.run(['/usr/bin/hdiutil','create','-volname','Notepad Next ++','-srcfolder',str(payload),'-format','UDZO',str(image)],stdout=log,stderr=log,check=True)
        with (root/'dmg.log').open('a') as log:sign_disk_image(image,args.identity,log)
        record['diskImageSigning']='ad-hoc-development' if args.identity=='-' else 'configured-identity'
        if notary_profile:
            with (root/'notarization.log').open('a') as log:record['diskImageNotarization']=notarize(image,notary_profile,log)
        record['dmg']={'path':str(image),'bytes':image.stat().st_size,'sha256':sha(image)}
    (root/'package-record.json').write_text(json.dumps(record,indent=2)+'\n');return record
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--app',type=Path,required=True);parser.add_argument('--qt',type=Path,required=True);parser.add_argument('--node-archive',type=Path,required=True);parser.add_argument('--arch',choices=['arm64','x86_64'],required=True);parser.add_argument('--output-dir',type=Path,required=True);parser.add_argument('--identity',default='-');parser.add_argument('--notary-profile',default='');parser.add_argument('--dmg',action='store_true');args=parser.parse_args()
    record=package(args);print(json.dumps({key:value for key,value in record.items() if key!='attributions'}))
if __name__=='__main__':main()
