"""Compile the checked-in, fully patched Qin 2 Pro DTS without regenerating it.

Windows uses Ubuntu-20.04's dtc; WSL runs that same dtc directly. Historical
BSP-to-Qin transformations are kept in make_dtb_legacy.py for reference.
"""
from pathlib import Path
import argparse
import os
import re
import subprocess

HERE = Path(__file__).resolve().parent
DTC = '/home/kevin/los/out/host/linux-x86/bin/dtc'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=HERE / 'merged-qin414.dts')
parser.add_argument('--output', type=Path, default=HERE / 'merged-qin414.dtb')
args = parser.parse_args()
source, output = args.source.resolve(), args.output.resolve()
text = source.read_text(encoding='utf-8')
flash_nodes = re.findall(r'flash-ic@63\s*\{([^{}]*)\}', text)
if len(flash_nodes) != 1 or not re.search(r'status\s*=\s*"disabled"', flash_nodes[0]):
    raise SystemExit('Refusing DT: stock-disabled OCP8137 node must remain disabled.')
output.parent.mkdir(parents=True, exist_ok=True)

def linux_path(path):
    if os.name != 'nt':
        return str(path)
    if not path.drive or len(path.drive) != 2:
        raise SystemExit('Windows DT source/output must be on a local drive.')
    return '/mnt/' + path.drive[0].lower() + path.as_posix()[2:]

command = [DTC, '-@', '-I', 'dts', '-O', 'dtb', '-o', linux_path(output), linux_path(source)]
if os.name == 'nt':
    command = ['wsl.exe', '-d', 'Ubuntu-20.04', '--', *command]
subprocess.run(command, check=True)
print(f'Compiled {source} -> {output} ({output.stat().st_size} bytes)')
