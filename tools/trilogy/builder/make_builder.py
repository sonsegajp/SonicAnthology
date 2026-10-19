"""Package AnthologyBuilder.exe (PyInstaller): the setup app plus the converters
(tools/trilogy/*.py) and their address tables (tools/trilogy/data/), so players
need no Python.

  python tools/trilogy/builder/make_builder.py [out dir, default build/Release]

Run tools/trilogy/export_syms.py first whenever the converters' symbols change.
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
ROOT = TOOLS.parents[1]
# the converters the bank build uses (dev-only tools stay out)
MODULES = ['build_bank', 'act3_numeral', 'knux_walldash', 'mdcodecs', 'tilepack', 'previews',
           's1_objects', 's1_music', 's2_level', 's2_objects', 's2_music', 's2_sfx', 's2_dac',
           's2_ending', 's2_special', 's3k_driver', 'scd_iso', 'scd_mmd', 'scd_level', 'scd_sfx', 'scd_special',
           'rsdk', 'origins_pack']


def main():
    out = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build' / 'Release'
    work = ROOT / 'build' / 'builder'
    sep = ';' if sys.platform == 'win32' else ':'
    cmd = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--onefile', '--windowed',
           '--name', 'AnthologyBuilder', '--distpath', str(out), '--workpath', str(work),
           '--specpath', str(work)]
    for m in MODULES:
        cmd += ['--add-data', f'{TOOLS / (m + ".py")}{sep}trilogy']
    cmd += ['--add-data', f'{TOOLS / "data"}{sep}trilogy/data']
    for h in ('PIL.Image', 'argparse', 'struct', 'zlib', 're', 'tempfile', 'subprocess', 'json', 'hashlib'):
        cmd += ['--hidden-import', h]
    cmd.append(str(HERE / 'trilogy_builder.py'))
    subprocess.run(cmd, check=True)
    print(f'{out / "AnthologyBuilder.exe"}')


if __name__ == '__main__':
    main()
