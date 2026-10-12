// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const fs=require('node:fs'),crypto=require('node:crypto'),path=require('node:path'),{spawnSync}=require('node:child_process');
const compiler=process.env.NNP_TYPESCRIPT??path.resolve(__dirname,'../markdown-preview/node_modules/typescript/lib/tsc.js');
for(const record of JSON.parse(fs.readFileSync(path.join(__dirname,'provenance.json'))).files)if(crypto.createHash('sha256').update(fs.readFileSync(path.join(__dirname,record.path))).digest('hex')!==record.sha256)throw new Error('Sample file checksum changed: '+record.path);
if(JSON.parse(fs.readFileSync(path.resolve(compiler,'../../package.json'))).version!=='5.9.3')throw new Error('This acceptance sample requires TypeScript 5.9.3');
const result=spawnSync(process.execPath,[compiler,'--strict','--target','ES2022','--module','commonjs','--lib','ES2022,DOM','--skipLibCheck','--outDir','out','src/extension.ts','../vscode-hello-world/types/vscode.d.ts'],{cwd:__dirname,stdio:'inherit'});
if(result.error)throw result.error;process.exitCode=result.status??1;
