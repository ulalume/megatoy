#!/usr/bin/env python3
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
build = Path(sys.argv[1] if len(sys.argv) > 1 else 'build-cov').resolve()
if 'MEGATOY_COVERAGE:BOOL=ON' not in (build / 'CMakeCache.txt').read_text():
    sys.exit('Configure the build with MEGATOY_COVERAGE=ON')
profile_dir = build / 'coverage-profiles'
profile_dir.mkdir(exist_ok=True)
for profile in profile_dir.glob('*.profraw'):
    profile.unlink()
tool = ['xcrun'] if sys.platform == 'darwin' else []
empty = profile_dir / 'empty.proftext'
empty.write_text('dummy\n0\n1\n0\n')
merged = profile_dir / 'coverage.profdata'
subprocess.run(tool + ['llvm-profdata', 'merge', '-sparse', str(empty), '-o', str(merged)], check=True)
tests = json.loads(subprocess.check_output(['ctest', '--test-dir', str(build), '--show-only=json-v1']))['tests']
source = root / 'src'
lines = {}


def collect(binary):
    output = subprocess.check_output(tool + ['llvm-cov', 'export', '-format=lcov',
                                             '-instr-profile', str(merged), str(binary)], text=True)
    current = None
    for line in output.splitlines():
        if line.startswith('SF:'):
            path = Path(line[3:])
            current = lines.setdefault(str(path.relative_to(root)), {}) if path.is_relative_to(source) else None
        elif current is not None and line.startswith('DA:'):
            number, count, *_ = line[3:].split(',')
            number = int(number)
            current[number] = current.get(number, False) or int(count) > 0


app = build / ('megatoy.app/Contents/MacOS/megatoy' if sys.platform == 'darwin' else 'megatoy')
collect(app)
failures = 0
for index, test in enumerate(tests, 1):
    env = os.environ.copy()
    env['LLVM_PROFILE_FILE'] = str(profile_dir / f'{index}-%m-%p.profraw')
    result = subprocess.run(['ctest', '--test-dir', str(build), '--output-on-failure',
                             '-I', f'{index},{index}'], env=env)
    failures += result.returncode != 0
    command = test.get('command', [])
    if not command or Path(command[0]).parent != build:
        continue
    profiles = list(profile_dir.glob(f'{index}-*.profraw'))
    subprocess.run(tool + ['llvm-profdata', 'merge', '-sparse',
                           *(map(str, profiles) if profiles else [str(empty)]),
                           '-o', str(merged)], check=True)
    collect(command[0])

rows = [(name, len(coverage), sum(coverage.values())) for name, coverage in lines.items()]
rows.sort(key=lambda row: (-(row[1] - row[2]), row[0]))
print(f'\n{len(tests) - failures}/{len(tests)} tests passed')
print(f"{'File':65} {'Lines':>7} {'Missed':>7} {'Coverage':>9}")
for name, count, covered in rows:
    print(f'{name:65} {count:7} {count-covered:7} {100*covered/count if count else 0:8.2f}%')
count = sum(row[1] for row in rows)
covered = sum(row[2] for row in rows)
print(f'{"TOTAL":65} {count:7} {count-covered:7} {100*covered/count if count else 0:8.2f}%')
sys.exit(bool(failures))
