// SPDX-License-Identifier: GPL-3.0-or-later
const fs=require('node:fs');fs.mkdirSync('dist',{recursive:true});fs.copyFileSync('src/index.py','dist/index.py');
