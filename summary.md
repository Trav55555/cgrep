| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 107.58 | 128.61 | 1.20x faster | 45.04 | 53.05 | 1.18x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 107.27 | 131.68 | 1.23x faster | 44.32 | 54.16 | 1.22x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 63.13 | 86.45 | 1.37x faster | 24.09 | 29.74 | 1.23x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 62.92 | 86.52 | 1.38x faster | 23.72 | 29.89 | 1.26x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 45.81 | 50.57 | 1.10x faster | 18.48 | 20.82 | 1.13x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 46.28 | 51.29 | 1.11x faster | 18.10 | 21.37 | 1.18x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (7998dd4, c-utils b74b8a9) at 7998dd4; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 9V45 96-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
