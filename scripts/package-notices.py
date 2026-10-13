#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Preserve configured dependency notices and exact Qt release attribution data."""
import argparse
import importlib.util
import os
from pathlib import Path
spec=importlib.util.spec_from_file_location('nnp_packaging',Path(__file__).with_name('package-macos.py'))
packaging=importlib.util.module_from_spec(spec);spec.loader.exec_module(packaging)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--destination',type=Path,required=True)
parser.add_argument('--cache',type=Path,required=True)
parser.add_argument('--qt',type=Path,default=os.environ.get('QT_ROOT_DIR') or os.environ.get('QTDIR'))
args=parser.parse_args()
if args.qt is None:parser.error('Provide the configured Qt SDK prefix through --qt or QT_ROOT_DIR')
packaging.notices_to_directory(args.destination,args.qt,args.cache)
print('Distribution notices staged:',args.destination)
