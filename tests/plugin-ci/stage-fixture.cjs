// SPDX-License-Identifier: GPL-3.0-or-later
// Copy an already executable test fixture. This is not an SDK packer:
// the production host, not this helper, validates the manifest and package.
const fs = require('node:fs'), path = require('node:path');
const [source, destination] = process.argv.slice(2);
if (!source || !destination) throw new Error('Usage: stage-fixture.cjs <fixture> <destination>');
fs.cpSync(source, destination, {recursive:true, filter: file => !['node_modules','.git'].includes(path.basename(file))});
