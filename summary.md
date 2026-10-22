| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 124.64 | 156.56 | 1.26x faster | 58.21 | 68.07 | 1.17x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 124.25 | 158.46 | 1.28x faster | 58.68 | 68.19 | 1.16x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 68.78 | 96.24 | 1.40x faster | 28.97 | 41.15 | 1.42x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 67.92 | 96.30 | 1.42x faster | 28.35 | 41.18 | 1.45x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 59.82 | 68.46 | 1.14x faster | 25.91 | 27.40 | 1.06x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 62.09 | 70.69 | 1.14x faster | 26.76 | 28.28 | 1.06x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (64eee59, c-utils b74b8a9) at 64eee59; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
