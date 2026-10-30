| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 125.44 | 156.62 | 1.25x faster | 59.21 | 68.40 | 1.16x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 125.62 | 158.63 | 1.26x faster | 59.10 | 68.90 | 1.17x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 69.83 | 97.20 | 1.39x faster | 29.95 | 41.22 | 1.38x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 69.63 | 97.66 | 1.40x faster | 29.74 | 41.69 | 1.40x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 60.72 | 69.03 | 1.14x faster | 26.00 | 28.14 | 1.08x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 62.98 | 71.47 | 1.13x faster | 27.48 | 28.49 | 1.04x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (93460c7, c-utils 7750e1c) at 93460c7; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
