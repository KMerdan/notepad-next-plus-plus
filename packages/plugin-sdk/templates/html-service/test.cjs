// SPDX-License-Identifier: GPL-3.0-or-later
const {test}=require('node:test'),assert=require('node:assert/strict');
test('built entry exposes an activation function',()=>assert.equal(typeof require('./dist/index.js').activate,'function'));
