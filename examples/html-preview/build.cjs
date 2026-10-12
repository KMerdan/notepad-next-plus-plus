// SPDX-License-Identifier: GPL-3.0-or-later
const fs=require('node:fs');
fs.mkdirSync('dist',{recursive:true});fs.copyFileSync('src/index.js','dist/index.js');
fs.copyFileSync(require.resolve('@notepadnext/plugin-sdk/local-service'),'dist/local-service.cjs');
fs.cpSync('public','dist/public',{recursive:true});
