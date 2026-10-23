| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 99.13 | 119.45 | 1.20x faster | 40.97 | 49.35 | 1.20x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 99.10 | 121.68 | 1.23x faster | 41.30 | 49.64 | 1.20x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 57.32 | 79.00 | 1.38x faster | 22.06 | 26.95 | 1.22x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 57.47 | 80.27 | 1.40x faster | 22.10 | 27.50 | 1.24x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 42.47 | 46.88 | 1.10x faster | 16.34 | 19.40 | 1.19x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 43.76 | 48.71 | 1.11x faster | 16.74 | 20.47 | 1.22x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (459af6a, c-utils 7750e1c) at 459af6a; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 9V45 96-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
