#!/usr/bin/env python3
import pathlib
import sys
p=pathlib.Path(sys.argv[1]);out=pathlib.Path(sys.argv[2]);data=p.read_bytes()
with out.open('w') as f:
 f.write('#include "shellui_blob.hpp"\nconst std::uint8_t commonfps_shellui_elf[]={\n')
 for offset in range(0,len(data),24):f.write(','.join(str(b) for b in data[offset:offset+24])+',\n')
 f.write('};\nconst std::size_t commonfps_shellui_elf_size=sizeof(commonfps_shellui_elf);\n')
