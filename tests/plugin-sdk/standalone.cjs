#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const path=require('node:path'),{spawnSync}=require('node:child_process');
const test=path.resolve(__dirname,'../../packages/plugin-sdk/tests/standalone.cjs');
const result=spawnSync(process.execPath,[test,process.argv[2]||'build/plugin-sdk-validation'],{stdio:'inherit',env:process.env});
if(result.error)throw result.error;process.exitCode=result.status??1;
