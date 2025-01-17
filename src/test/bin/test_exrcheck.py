#!/usr/bin/env python

# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) Contributors to the OpenEXR Project.

import sys, os
from subprocess import PIPE, run

print(f"testing exrcheck: {sys.argv}")
      
exrcheck = sys.argv[1]
image_dir = sys.argv[2]

for exr_file in sys.argv[3:]:

    exr_path = f"{image_dir}/{exr_file}"

    result = run ([exrcheck, exr_path], stdout=PIPE, stderr=PIPE, universal_newlines=True)
    assert(result.returncode == 0)

    result = run ([exrcheck, "-m", exr_path], stdout=PIPE, stderr=PIPE, universal_newlines=True)
    assert(result.returncode == 0)

    result = run ([exrcheck, "-t", exr_path], stdout=PIPE, stderr=PIPE, universal_newlines=True)
    assert(result.returncode == 0)

    result = run ([exrcheck, "-s", exr_path], stdout=PIPE, stderr=PIPE, universal_newlines=True)
    assert(result.returncode == 0)

    result = run ([exrcheck, "-c", exr_path], stdout=PIPE, stderr=PIPE, universal_newlines=True)
    assert(result.returncode == 0)


print("success.")

