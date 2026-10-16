"""Offline provisioning and checked execution of the CI reference."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import stat
import subprocess
import sys
import tempfile


def unique_fields(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate manifest field: {key}')
        result[key] = value
    return result


def load_pin():
    pin = json.loads(Path(__file__).with_suffix('.json').read_text(),
                     object_pairs_hook=unique_fields)
    keys = {'schema', 'platform', 'name', 'sha256', 'version_first_line', 'provenance'}
    if (not isinstance(pin, dict) or set(pin) != keys
            or any(not isinstance(v, str) for v in pin.values())
            or pin['schema'] != 'cgrep.ci-reference/0'
            or pin['platform'] != 'linux-x86_64' or pin['name'] != 'rg'
            or not re.fullmatch('[0-9a-f]{64}', pin['sha256'])
            or not pin['version_first_line']
            or any(c in pin['version_first_line'] for c in '\r\n')
            or not pin['provenance'].strip()):
        raise ValueError('invalid reference manifest')
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        raise ValueError('reference requires Linux x86_64')
    return pin


def owned_directory(path, create=False):
    if create:
        path.mkdir(mode=0o700, parents=True, exist_ok=True)
    info = path.lstat()
    if (not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid()
            or info.st_mode & 0o022):
        raise ValueError(f'cache directory must be owned, non-symlink and private: {path}')


def target_path(root, pin, create=False):
    for component in [*reversed(root.parents), root]:
        if component.is_symlink():
            raise ValueError(f'symlink cache component: {component}')
    owned_directory(root, create)
    digest = root / pin['sha256']
    owned_directory(digest, create)
    owned_directory(digest / 'bin', create)
    return digest / 'bin' / 'rg'


def verify_file(path, pin):
    info = path.lstat()
    if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid()
            or info.st_mode & 0o022 or not info.st_mode & 0o111):
        raise ValueError(f'reference must be an owned regular executable: {path}')
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    if digest != pin['sha256']:
        raise ValueError(f'reference checksum mismatch: {path}')
    result = subprocess.run([str(path), '--version'], capture_output=True, text=True,
                            timeout=10, check=True)
    if not result.stdout.splitlines() or result.stdout.splitlines()[0] != pin['version_first_line']:
        raise ValueError(f'reference version mismatch: {path}')


def provision(source, target, pin):
    if target.exists() or target.is_symlink():
        verify_file(target, pin)
        return
    with tempfile.TemporaryDirectory(prefix='.stage-', dir=target.parent) as temporary:
        staged = Path(temporary) / 'rg'
        fd = os.open(source, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(fd, 'rb') as stream:
            if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                raise ValueError('source must be a regular file')
            with staged.open('xb') as output:
                shutil.copyfileobj(stream, output)
        staged.chmod(0o700)
        verify_file(staged, pin)
        try:
            # A hard link publishes complete bytes atomically without replacing any entry.
            os.link(staged, target)
        except FileExistsError:
            verify_file(target, pin)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    for action in ['provision', 'exec']:
        command = actions.add_parser(action)
        command.add_argument('--cache-root', type=Path)
        if action == 'provision':
            command.add_argument('--source', type=Path, required=True)
        else:
            command.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.action == 'exec' and (not args.command or args.command[0] != '--' or len(args.command) < 2):
        parser.error('exec requires -- COMMAND...')
    try:
        pin = load_pin()
        root = Path(os.path.abspath(args.cache_root or Path.home() / '.local/opt/cgrep-reference'))
        target = target_path(root, pin, create=args.action == 'provision')
        if args.action == 'provision':
            provision(args.source, target, pin)
            return 0
        verify_file(target, pin)
        print(f"CI reference: {pin['name']} {pin['version_first_line']} sha256={pin['sha256']} path={target}",
              file=sys.stderr, flush=True)
        env = dict(os.environ)
        env['PATH'] = str(target.parent) + os.pathsep + env.get('PATH', os.defpath)
        try:
            os.execvpe(args.command[1], args.command[1:], env)
        except FileNotFoundError:
            return 127
        except PermissionError:
            return 126
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'reference: {error}; provision offline with: python3 -I ci/reference.py provision --source PATH',
              file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
