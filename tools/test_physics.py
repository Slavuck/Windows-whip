"""Compile and replay only the portable physics core (MSVC or GNU C++20).

Examples:
  python tools/test_physics.py --label revised --check
  python tools/test_physics.py --source-dir build/physics-baseline --label baseline
  CXX=g++-13 python3 tools/test_physics.py --label linux --check

No application process, overlay, audio engine, input hook, or desktop click is used.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(command, **kwargs):
    if os.name == 'nt':
        kwargs.setdefault('creationflags', subprocess.CREATE_NO_WINDOW)
    return subprocess.run(command, check=True, **kwargs)


def compiler_environment():
    env = {key.upper(): value for key, value in os.environ.items()} if os.name == 'nt' else os.environ.copy()
    compiler = env.get('CXX')
    if compiler:
        resolved = shutil.which(compiler) or compiler
        return resolved, env, Path(resolved).name.lower() in ('cl', 'cl.exe')
    for name in ('g++-13', 'g++', 'clang++'):
        if resolved := shutil.which(name):
            return resolved, env, False
    if resolved := shutil.which('cl'):
        return resolved, env, True
    if os.name != 'nt':
        raise SystemExit('Install g++-13 (or set CXX to a C++20 compiler).')
    vswhere = Path(env.get('PROGRAMFILES(X86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    installation = run([str(vswhere), '-latest', '-prerelease', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], capture_output=True, text=True).stdout.strip()
    if not installation:
        raise SystemExit('MSVC x64 workload not found; no compiler was installed by this script.')
    setup = Path(installation) / 'Common7/Tools/VsDevCmd.bat'
    # Paths are supplied by the local VS installer; the command only imports its environment.
    command = f'"{env.get("COMSPEC", "cmd.exe")}" /d /c call "{setup}" -no_logo -arch=x64 -host_arch=x64 >nul && set'
    result = run(command, capture_output=True, text=True)
    for line in result.stdout.splitlines():
        if '=' in line and not line.startswith('='):
            key, value = line.split('=', 1)
            env[key.upper()] = value
    compiler = shutil.which('cl.exe', path=env.get('PATH'))
    if not compiler:
        raise SystemExit('MSVC environment did not expose cl.exe.')
    return compiler, env, True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=ROOT / 'src', help='Directory containing core.cpp and core.hpp; use a saved snapshot for baseline replay.')
    parser.add_argument('--label', default='current')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/physics-headless')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+', args.label):
        parser.error('label must contain only letters, digits, underscore or hyphen')
    source = args.source_dir.resolve()
    for name in ('core.cpp', 'core.hpp'):
        if not (source / name).is_file():
            parser.error(f'{source / name} does not exist')
    directory = args.build_dir.resolve() / args.label
    directory.mkdir(parents=True, exist_ok=True)
    # Compile an immutable, hashable snapshot so edits made during a long
    # comparison cannot silently change the version represented by a report.
    original_source = source
    source = directory / 'source'
    source.mkdir(exist_ok=True)
    for name in ('core.cpp', 'core.hpp'):
        (source / name).write_bytes((original_source / name).read_bytes())
    executable = directory / ('physics_tests.exe' if os.name == 'nt' else 'physics_tests')
    compiler, env, msvc = compiler_environment()
    harness = ROOT / 'tests/physics_tests.cpp'
    if msvc:
        command = [compiler, '/nologo', '/std:c++20', '/O2', '/EHsc', '/W4', '/utf-8', '/MT', f'/I{source}', str(source / 'core.cpp'), str(harness), f'/Fe:{executable}', f'/Fo:{directory}{os.sep}']
    else:
        command = [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-pedantic', '-I', str(source), str(source / 'core.cpp'), str(harness), '-o', str(executable)]
    run(command, env=env, cwd=directory)
    metrics = directory / 'metrics.csv'
    replay = [str(executable), '--output', str(metrics), '--trace-dir', str(directory / 'traces')]
    if args.check:
        replay.append('--check')
    result = subprocess.run(replay, env=env, cwd=directory, capture_output=True, text=True, **({'creationflags': subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {}))
    sys.stderr.write(result.stderr)
    metadata = {'label': args.label, 'compiler': compiler, 'source_dir': str(original_source), 'compiled_snapshot': str(source), 'sources': {name: hashlib.sha256((source / name).read_bytes()).hexdigest() for name in ('core.cpp', 'core.hpp')}, 'harness_sha256': hashlib.sha256(harness.read_bytes()).hexdigest(), 'check_exit_code': result.returncode, 'metrics': str(metrics)}
    (directory / 'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    if metrics.exists():
        with metrics.open(newline='', encoding='utf-8') as file:
            for row in csv.DictReader(file):
                print(f"{row['scenario']} @ {row['fps']} Hz: tip peak {float(row['peak_tip_speed']):.1f}, late HF {float(row['late_tip_high_frequency_rms']):.1f} px/s, local tail joint HF {float(row['fixed_late_tail_joint_high_frequency_rms']):.2f} rad/s @ {row['fixed_joint_sample_hz']} Hz, stretch {float(row['max_relative_stretch']):.2e}")
    print(f'Report: {metrics}')
    raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
