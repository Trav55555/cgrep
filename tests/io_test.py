"""Native streaming checks: memory budgets, pipes, limits, and failed output."""
import argparse
import os
import resource
import subprocess
import tempfile
import threading
from pathlib import Path


def memory_limit(mib):
    def apply():
        resource.setrlimit(resource.RLIMIT_AS, (mib * 1024 * 1024, mib * 1024 * 1024))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    return apply


def pipe_input(command, path, cwd):
    errors = []
    with path.open('rb') as source:
        child = subprocess.Popen(command, cwd=cwd, stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 preexec_fn=memory_limit(32))
        sink = child.stdin
        child.stdin = None
        def feed():
            try:
                while block := source.read(65536):
                    sink.write(block)
            except BrokenPipeError:
                pass
            except Exception as error:
                errors.append(error)
            finally:
                try:
                    sink.close()
                except BrokenPipeError:
                    pass
        writer = threading.Thread(target=feed, daemon=True)
        writer.start()
        try:
            stdout, stderr = child.communicate(timeout=20)
        except BaseException:
            child.kill()
            child.communicate()
            raise
        finally:
            writer.join(timeout=5)
        if writer.is_alive() or errors:
            raise RuntimeError(f'pipe producer failed: {errors!r}')
    return subprocess.CompletedProcess(command, child.returncode, stdout, stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cgrep')
    args = parser.parse_args()
    binary = str(Path(args.cgrep).resolve())
    with tempfile.TemporaryDirectory(prefix='cgrep-io-') as directory:
        root = Path(directory)
        block = (b'x' * 127 + b'\n') * 512
        with (root / 'large').open('wb') as stream:
            for _ in range(1024):
                stream.write(block)
        (root / 'match').write_bytes(b'alpha\n')
        result = subprocess.run([binary, 'alpha', 'large', 'match'], cwd=root,
                                capture_output=True, timeout=20, preexec_fn=memory_limit(32))
        if (result.returncode, result.stdout, result.stderr) != (0, b'match:alpha\n', b''):
            raise RuntimeError(f'large file exceeded its memory budget: {result!r}')
        print('PASS streaming file: 64 MiB input under a 32 MiB address-space limit')
        result = subprocess.run([binary, '-j8', 'alpha', *(['large'] * 7), 'match'], cwd=root,
                                capture_output=True, timeout=20, preexec_fn=memory_limit(32))
        if (result.returncode, result.stdout, result.stderr) != (0, b'match:alpha\n', b''):
            raise RuntimeError(f'eight-worker memory budget failed: {result!r}')
        print('PASS eight workers: 448 MiB scanned under a 32 MiB address-space limit')
        result = pipe_input([binary, 'alpha', '-', 'match'], root / 'large', root)
        if (result.returncode, result.stdout, result.stderr) != (0, b'match:alpha\n', b''):
            raise RuntimeError(f'large pipe exceeded its memory budget: {result!r}')
        print('PASS streaming pipe: 64 MiB input under a 32 MiB address-space limit')

        with (root / 'long').open('wb') as stream:
            stream.write(b'x' * (8 * 1024 * 1024 + 1))
        result = subprocess.run([binary, 'alpha', 'long', 'match'], cwd=root,
                                capture_output=True, timeout=10, preexec_fn=memory_limit(32))
        if result.returncode != 2 or result.stdout != b'match:alpha\n' or b'line exceeds 8 MiB' not in result.stderr:
            raise RuntimeError(f'line limit or continued search failed: {result!r}')
        print('PASS line limit: status 2, later input still searched')
        result = subprocess.run([binary, 'alpha', 'long', 'match'], cwd=root,
                                capture_output=True, timeout=10, preexec_fn=memory_limit(12))
        if result.returncode != 2 or result.stdout != b'match:alpha\n' or b'out of memory' not in result.stderr:
            raise RuntimeError(f'allocation refusal or buffer recovery failed: {result!r}')
        print('PASS allocation refusal: failed buffer discarded, later input still searched')
        for limit, reason in [(32, b'line exceeds 8 MiB'), (12, b'out of memory')]:
            result = subprocess.run([binary, '-j8', 'alpha', 'long', 'match'], cwd=root,
                                    capture_output=True, timeout=10, preexec_fn=memory_limit(limit))
            if result.returncode != 2 or result.stdout != b'match:alpha\n' or reason not in result.stderr:
                raise RuntimeError(f'worker input failure/continuation failed: {result!r}')
        print('PASS worker line/allocator limits: status 2 with ordered later match')

        if Path('/dev/full').exists():
            with open('/dev/full', 'wb') as output:
                result = subprocess.run([binary, 'alpha', 'match'], cwd=root,
                                        stdout=output, stderr=subprocess.PIPE, timeout=10)
            if result.returncode != 2:
                raise RuntimeError(f'output error was not reported: {result!r}')
            print('PASS output device failure: status 2')
            with open('/dev/full', 'wb') as output:
                result = subprocess.run([binary, '-j8', 'alpha', *(['match'] * 8)], cwd=root,
                                        stdout=output, stderr=subprocess.PIPE, timeout=10)
            if result.returncode != 2:
                raise RuntimeError(f'worker output failure/join failed: {result!r}')
            print('PASS worker output device failure: status 2 and joined scope')
        else:
            print('SKIP output device failure: /dev/full unavailable')
        if hasattr(os, 'mkfifo'):
            os.mkfifo(root / 'blocked')
            with (root / 'match').open('rb') as output:
                result = subprocess.run([binary, 'alpha', 'match', 'blocked'], cwd=root,
                                        stdout=output, stderr=subprocess.PIPE, timeout=3)
            if result.returncode != 2 or b'output error' not in result.stderr:
                raise RuntimeError(f'failed output did not stop later input: {result!r}')
            print('PASS output failure: later blocking input was never opened')
            with (root / 'match').open('rb') as output:
                result = subprocess.run([binary, '-j8', 'alpha', 'match', 'blocked'], cwd=root,
                                        stdout=output, stderr=subprocess.PIPE, timeout=3)
            if result.returncode != 2 or b'output error' not in result.stderr:
                raise RuntimeError(f'non-regular serial fallback failed: {result!r}')
            print('PASS requested workers: non-regular fallback stops before blocked FIFO')
        else:
            print('SKIP blocking-input check: mkfifo unavailable')


if __name__ == '__main__':
    main()
