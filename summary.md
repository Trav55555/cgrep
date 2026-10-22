| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 158.82 | 198.68 | 1.25x faster | 65.61 | 73.86 | 1.13x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 158.20 | 200.99 | 1.27x faster | 65.72 | 75.15 | 1.14x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 89.31 | 125.74 | 1.41x faster | 33.46 | 41.89 | 1.25x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 89.97 | 127.86 | 1.42x faster | 34.50 | 42.25 | 1.22x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 72.73 | 82.82 | 1.14x faster | 28.02 | 29.89 | 1.07x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 74.93 | 85.74 | 1.14x faster | 28.34 | 32.11 | 1.13x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (1df1e71, c-utils b74b8a9) at 1df1e71; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 9V74 80-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
