// SPDX-License-Identifier: GPL-3.0-or-later
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'../..');
test('SDK pin records the actual reviewed submodule version and commit',()=>{
  const {execFileSync}=require('node:child_process');
  const pin=JSON.parse(fs.readFileSync(path.join(root,'packages/plugin-sdk-lock.json'),'utf8'));
  const pkg=JSON.parse(fs.readFileSync(path.join(root,'packages/plugin-sdk/package.json'),'utf8'));
  assert.equal(pin.version,pkg.version);
  assert.equal(pin.commit,execFileSync('git',['-C',path.join(root,'packages/plugin-sdk'),'rev-parse','HEAD'],{encoding:'utf8'}).trim());
  assert.equal(pin.developmentPackage,`https://github.com/KMerdan/notepadnext-plugin-sdk/releases/download/v${pkg.version}/notepadnext-plugin-sdk-${pkg.version}.tgz`);
});
test('application documentation schemas equal its pinned SDK contract',()=>{
  for(const name of ['manifest.schema.json','release-checksums.schema.json'])assert.equal(fs.readFileSync(path.join(root,'doc/plugin-system',name),'utf8'),fs.readFileSync(path.join(root,'packages/plugin-sdk/schemas',name),'utf8'),'Run scripts/sync-plugin-sdk-contracts.py after updating the SDK pin.');
});

test('application wire contracts match the reviewed SDK and have no native dependency on the VS Code experiment',()=>{
  for(const name of ['wire.schema.json','wire-contract.json'])assert.equal(fs.readFileSync(path.join(root,'packages/plugin-host',name),'utf8'),fs.readFileSync(path.join(root,'packages/plugin-sdk/schemas',name),'utf8'));
  assert.doesNotMatch(fs.readFileSync(path.join(root,'packages/plugin-host/client.cjs'),'utf8'),/require\(['"]\.\/vscode/);
});
