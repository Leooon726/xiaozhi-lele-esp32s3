#!/usr/bin/env python3
"""macOS host-test adapter for Apple Clang's C++23 draft spelling."""
import os
import shutil
import subprocess
import sys
compiler = os.environ.get('LELE_HOST_CXX', shutil.which('clang++') or 'c++')
version = subprocess.check_output([compiler, '--version'], text=True)
args = sys.argv[1:]
if 'Apple clang version' in version:
    args = [{'-std=c++23': '-std=c++2b', '-std=gnu++23': '-std=gnu++2b'}.get(a, a) for a in args]
os.execvp(compiler, [compiler, *args])
