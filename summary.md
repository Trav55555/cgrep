| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 98.19 | 121.90 | 1.24x faster | 41.86 | 51.24 | 1.22x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 97.56 | 122.04 | 1.25x faster | 40.11 | 51.29 | 1.28x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 54.49 | 74.43 | 1.37x faster | 21.76 | 26.07 | 1.20x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 55.03 | 75.68 | 1.38x faster | 20.90 | 27.02 | 1.29x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 41.93 | 46.65 | 1.11x faster | 15.78 | 19.38 | 1.23x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 42.55 | 47.47 | 1.12x faster | 16.04 | 19.84 | 1.24x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (c0ad374, c-utils 7750e1c) at c0ad374; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 9V45 96-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
