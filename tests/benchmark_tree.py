#!/usr/bin/env python3
"""Validate and time literal searches including the directory walk."""
import argparse
import json
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile
import time

WORD = 'cgrepneedle'
SHAPES = ('wide', 'manydirs', 'mixed')
QUERIES = ('files', 'lines')
TOOLS = ('cgrep-single', 'rg-single', 'cgrep-default', 'rg-default')


def fixtures(root, quick=False):
    """Create byte-for-byte reproducible trees; return their measured parameters."""
    result = {}
    for shape in SHAPES:
        tree = root / shape
        tree.mkdir(parents=True)
        paths = []
        if shape == 'wide':
            paths = [(tree / f'f{i:05}.txt', 64) for i in range(200 if quick else 20000)]
        elif shape == 'manydirs':
            dirs = [tree]
            frontier = [tree]
            for _ in range(2 if quick else 4):
                frontier = [parent / f'd{i}' for parent in frontier for i in range(6)]
                dirs.extend(frontier)
            paths = [(d / f'f{i}.txt', 64) for d in dirs for i in range(6)]
        else:
            sizes = (1, 64, 1024, 8192, 65536, 262144)
            paths = [(tree / f'pkg{i % 50:02}' / f'f{i:05}.txt', sizes[i % len(sizes)])
                     for i in range(200 if quick else 5000)]
            paths += [(tree / f'large{i}.txt', 1048576 if quick else 4194304) for i in range(3)]
            paths += [(tree / 'excluded' / f'f{i}.txt', 64) for i in range(12)]
            (tree / '.gitignore').write_text('excluded/\n*.tmp\n')
            paths.append((tree / 'scratch.tmp', 64))
        planted = 0
        for index, (path, size) in enumerate(paths):
            path.parent.mkdir(parents=True, exist_ok=True)
            match = index % 11 == 0 and size >= len(WORD) + 1
            prefix = (WORD + '\n').encode() if match else b''
            # Bounded line lengths keep line-output workloads representative.
            filler = b'ordinary text without the word\n'
            data = prefix + (filler * (size // len(filler) + 1))[:size - len(prefix)]
            path.write_bytes(data)
            planted += match
        result[shape] = dict(files=len(paths), directories=1 + sum(p.is_dir() for p in tree.rglob('*')),
                             bytes=sum(p.stat().st_size for p in tree.rglob('*') if p.is_file()),
                             planted_files=planted)
    return dict(shapes=result, quick=quick, word=WORD, fanout=6,
                levels=2 if quick else 4, files_per_directory=6)


def commands(cgrep, rg, query):
    args = (['-l'] if query == 'files' else []) + [WORD, '.']
    return {'cgrep-single': [cgrep, '-j1'] + args,
            'cgrep-default': [cgrep] + args,
            'rg-single': [rg, '--no-config', '-j1', '--sort', 'path'] + args,
            'rg-default': [rg, '--no-config'] + args}


def run(command, cwd, output):
    with open(output, 'wb') as stream:
        start = time.perf_counter_ns()
        proc = subprocess.run(command, cwd=cwd, stdout=stream, stderr=subprocess.PIPE)
        elapsed = time.perf_counter_ns() - start
    if proc.returncode != 0 or proc.stderr:
        raise ValueError(f'{command}: exit {proc.returncode}, stderr {proc.stderr!r}')
    return elapsed


def validate(configs, cwd, scratch):
    """Validate every config before any timing; only rg defaults may reorder lines."""
    outputs = {}
    for tool, command in configs.items():
        output = scratch / (tool + '.out')
        run(command, cwd, output)
        outputs[tool] = output.read_bytes()
    expected = outputs['rg-single']
    if not expected:
        raise ValueError('reference produced no matches')
    for tool, actual in outputs.items():
        equal = (sorted(actual.splitlines(keepends=True)) == sorted(expected.splitlines(keepends=True))
                 if tool == 'rg-default' else actual == expected)
        if not equal:
            raise ValueError(f'output mismatch: {cwd.name}: {tool} vs rg-single')


def version(command):
    return subprocess.check_output([command, '--version'], text=True).splitlines()[0]


def cpu_model():
    try:
        for line in Path('/proc/cpuinfo').read_text().splitlines():
            if line.startswith('model name'):
                return line.split(':', 1)[1].strip()
    except OSError:
        pass
    return 'unknown CPU'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cgrep')
    parser.add_argument('--rg', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repeats', type=int, default=15)
    parser.add_argument('--quick', action='store_true')
    args = parser.parse_args(argv)
    if args.repeats < 1:
        parser.error('--repeats must be positive')
    cgrep, rg = (str(Path(shutil.which(p) or p).resolve()) for p in (args.cgrep, args.rg))
    report = dict(status='SUCCESS', requested=dict(repeats=args.repeats),
                  tools={k: dict(version=version(v)) for k, v in [('cgrep', cgrep), ('rg', rg)]},
                  cpu=cpu_model(), medians={})
    with tempfile.TemporaryDirectory(prefix='cgrep-tree-') as tmp:
        root = Path(tmp)
        report['fixture'] = fixtures(root, args.quick)
        cells = [(shape, query, commands(cgrep, rg, query)) for shape in SHAPES for query in QUERIES]
        for shape, query, configs in cells:
            validate(configs, root / shape, root)
        for shape, query, configs in cells:
            samples = {tool: [] for tool in TOOLS}
            for tool in TOOLS:
                run(configs[tool], root / shape, root / 'timing.out')
            for repeat in range(args.repeats):
                order = TOOLS[repeat % 4:] + TOOLS[:repeat % 4]
                for tool in order:
                    samples[tool].append(run(configs[tool], root / shape, root / 'timing.out'))
            for tool in TOOLS:
                report['medians'][f'{shape}-{query}-{tool}'] = dict(elapsed_ns=statistics.median(samples[tool]))
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('SUCCESS:', args.output / 'report.json')


if __name__ == '__main__':
    main()
