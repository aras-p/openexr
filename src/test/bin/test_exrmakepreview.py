#!/usr/bin/env python

# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) Contributors to the OpenEXR Project.

import sys, os, tempfile, atexit
from subprocess import PIPE, run

print(f"testing exrmakepreview: {sys.argv}")

exrmakepreview = sys.argv[1]
exrinfo = sys.argv[2]
image_dir = sys.argv[3]

assert(os.path.isfile(exrmakepreview))
assert(os.path.isfile(exrinfo))
assert(os.path.isdir(image_dir))

# no args = usage message
result = run ([exrmakepreview], stdout=PIPE, stderr=PIPE, universal_newlines=True)
print(" ".join(result.args))
assert(result.returncode == 1)
assert(result.stderr.startswith ("Usage: "))

# -h = usage message
result = run ([exrmakepreview, "-h"], stdout=PIPE, stderr=PIPE, universal_newlines=True)
print(" ".join(result.args))
assert(result.returncode == 1)
assert(result.stderr.startswith ("Usage: "))

def find_line(keyword, lines):
    for line in lines:
        if line.startswith(keyword):
            return line
    return None

fd, outimage = tempfile.mkstemp(".exr")
os.close(fd)

def cleanup():
    print(f"deleting {outimage}")
atexit.register(cleanup)

image = f"{image_dir}/TestImages/GrayRampsHorizontal.exr"
result = run ([exrmakepreview, "-w", "50", "-e", "1", "-v", image, outimage], stdout=PIPE, stderr=PIPE, universal_newlines=True)
print(" ".join(result.args))
assert(result.returncode == 0)

result = run ([exrinfo, "-v", outimage], stdout=PIPE, stderr=PIPE, universal_newlines=True)
print(" ".join(result.args))
assert(result.returncode == 0)
output = result.stdout.split('\n')
assert("preview 50 x 50" in find_line("  preview", output))

print("success")









