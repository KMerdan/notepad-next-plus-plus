#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Refresh application documentation snapshots from its reviewed SDK submodule."""
from pathlib import Path
import json, shutil, subprocess
root=Path(__file__).resolve().parents[1]
sdk=root/'packages/plugin-sdk'
if subprocess.check_output(['git','-C',str(sdk),'status','--porcelain'],text=True).strip():
    raise SystemExit('SDK checkout is dirty; commit and review it before updating the application pin.')
commit=subprocess.check_output(['git','-C',str(sdk),'rev-parse','HEAD'],text=True).strip()
package=json.loads((sdk/'package.json').read_text())
for name in ['manifest.schema.json','release-checksums.schema.json']:
    shutil.copyfile(sdk/'schemas'/name,root/'doc/plugin-system'/name)
for name in ['wire.schema.json','wire-contract.json']:
    shutil.copyfile(sdk/'schemas'/name,root/'packages/plugin-host'/name)
record={'repository':'https://github.com/KMerdan/notepadnext-plugin-sdk','version':package['version'],'commit':commit,'tag':'v'+package['version'],'developmentPackage':'https://github.com/KMerdan/notepadnext-plugin-sdk/releases/download/v'+package['version']+'/notepadnext-plugin-sdk-'+package['version']+'.tgz','schemaOwner':'SDK repository; application documentation snapshots are generated'}
(root/'packages/plugin-sdk-lock.json').write_text(json.dumps(record,indent=2)+'\n')
print('SDK contracts synchronized:',package['version'],commit)
