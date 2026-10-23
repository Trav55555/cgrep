| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 132.13 | 161.37 | 1.22x faster | 60.68 | 68.01 | 1.12x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 132.86 | 165.73 | 1.25x faster | 60.87 | 68.95 | 1.13x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 75.93 | 106.36 | 1.40x faster | 31.68 | 40.99 | 1.29x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 75.02 | 106.11 | 1.41x faster | 31.32 | 40.75 | 1.30x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 62.24 | 69.95 | 1.12x faster | 26.10 | 27.79 | 1.06x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 63.70 | 72.10 | 1.13x faster | 26.84 | 28.76 | 1.07x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (312f5f8, c-utils b74b8a9) at 312f5f8; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
