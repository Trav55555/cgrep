| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 118.82 | 150.75 | 1.27x faster | 48.76 | 60.20 | 1.23x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 119.35 | 152.78 | 1.28x faster | 48.95 | 60.90 | 1.24x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 66.26 | 92.26 | 1.39x faster | 25.63 | 31.90 | 1.24x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 65.90 | 92.93 | 1.41x faster | 25.80 | 32.29 | 1.25x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 56.82 | 64.94 | 1.14x faster | 21.04 | 23.95 | 1.14x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 58.55 | 67.50 | 1.15x faster | 22.37 | 25.00 | 1.12x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (432a627, c-utils 7750e1c) at 432a627; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 9V74 80-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
