#!/usr/bin/env python3
"""Native Windows implementation of validate.sh's isolated firmware stage.

ESP-IDF Kconfig requires an ASCII staging path on this Windows toolchain.
The source checkout, local sdkconfig, device, and existing artifacts are preserved.
"""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(command, cwd=ROOT, env=None):
    subprocess.run([str(v) for v in command], cwd=cwd, env=env, check=True)


def main():
    if os.name != 'nt':
        raise SystemExit('This stage is for native Windows; use validate.sh elsewhere.')
    idf_root = Path(os.environ.get('IDF_PATH', ''))
    python_root = Path(os.environ.get('IDF_PYTHON_ENV_PATH', ''))
    idf = idf_root / 'tools' / 'idf.py'
    python = python_root / 'Scripts' / 'python.exe'
    if not idf.is_file() or not python.is_file():
        raise SystemExit('Activate ESP-IDF 5.5.3 before running the firmware gate.')
    env = os.environ.copy()
    # This helper launches native Windows tools, even when the gate uses Git Bash.
    env.pop('MSYSTEM', None)
    env['PYTHONUTF8'] = '1'
    version = subprocess.check_output([str(python), str(idf), '--version'], text=True, env=env).strip()
    if version != 'ESP-IDF v5.5.3':
        raise SystemExit('Expected ESP-IDF v5.5.3, got '+version)
    # Fixed, explicit ASCII staging root. TemporaryDirectory deletes only its own child.
    staging_root = Path(os.environ.get('SystemDrive', 'C:') + '/ai-passport-validation').resolve()
    staging_root.mkdir(parents=True, exist_ok=True)
    if not str(staging_root).isascii():
        raise SystemExit('The Windows staging root must be ASCII.')
    archive_root = ROOT / 'build' / 'firmware'
    output = ROOT / 'build'
    output.mkdir(exist_ok=True)
    env['IDF_CCACHE_ENABLE'] = '1'
    # A native UTF-8 path for Python is still unsuitable for some compiler subtools.
    env['TMP'] = env['TEMP'] = str(staging_root)
    with tempfile.TemporaryDirectory(prefix='firmware-', dir=staging_root) as directory:
        stage = Path(directory).resolve()
        if stage.parent != staging_root or not stage.name.startswith('firmware-'):
            raise RuntimeError('Unexpected staging location')
        source_hashes = {}
        for name in ('CMakeLists.txt', 'sdkconfig.defaults', 'partitions.csv', 'dependencies.lock'):
            shutil.copy2(ROOT/name, stage/name)
            source_hashes[name] = hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
        for name in ('main', 'components', 'assets'):
            shutil.copytree(ROOT/name, stage/name, ignore=shutil.ignore_patterns('__pycache__', '.git'))
            for file in (stage/name).rglob('*'):
                if file.is_file():
                    source_hashes[file.relative_to(stage).as_posix()] = hashlib.sha256(file.read_bytes()).hexdigest()
        if (ROOT/'managed_components').is_dir():
            shutil.copytree(ROOT/'managed_components', stage/'managed_components')
        build = stage/'build'
        project_version = subprocess.check_output(['git', 'describe', '--always', '--dirty', '--tags'], cwd=ROOT, text=True).strip()
        print('Windows isolated firmware build:', stage, flush=True)
        run([python, idf, '-B', build, '-D', f'SDKCONFIG={build.as_posix()}/sdkconfig',
             '-D', f'PROJECT_VER={project_version}', 'build'], stage, env)
        if (stage/'dependencies.lock').read_bytes() != (ROOT/'dependencies.lock').read_bytes():
            raise RuntimeError('Managed dependency resolution changed: review dependencies.lock in the source checkout first')
        run([python, idf, '-B', build, 'size'], stage, env)
        run([python, idf, '-B', build, 'merge-bin', '-o', build/'FoloToy-AI-Passport-full.bin'], stage, env)
        run([sys.executable, ROOT/'tools/verify_firmware.py', build])
        run([sys.executable, ROOT/'tools/archive_firmware.py', 'create', build, '--archive-root', archive_root])
        image = build/'FoloToy-AI-Passport-full.bin'
        digest = hashlib.sha256(image.read_bytes()).hexdigest()
        archive = archive_root/digest
        run([sys.executable, ROOT/'tools/archive_firmware.py', 'verify', archive])
        source_record = {'project_version': project_version, 'idf_version': version,
                         'full_image_sha256': digest, 'source_sha256': source_hashes}
        (output/'claude-source-manifest.json').write_text(json.dumps(source_record, indent=2)+'\n', encoding='utf-8')
        shutil.copy2(image, output/image.name)
        print('Firmware build: PASS', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
