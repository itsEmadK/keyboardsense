#!/usr/bin/env python3
import hashlib
import pathlib
import sys

for argument in sys.argv[1:]:
    path = pathlib.Path(argument)
    print(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}')
