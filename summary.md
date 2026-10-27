| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 134.76 | 166.79 | 1.24x faster | 62.45 | 70.09 | 1.12x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 133.94 | 169.32 | 1.26x faster | 62.66 | 70.74 | 1.13x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 76.47 | 108.40 | 1.42x faster | 32.64 | 41.87 | 1.28x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 76.29 | 109.91 | 1.44x faster | 32.44 | 41.89 | 1.29x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 67.24 | 76.57 | 1.14x faster | 27.63 | 30.70 | 1.11x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 69.12 | 78.94 | 1.14x faster | 28.69 | 30.68 | 1.07x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (5c3b07d, c-utils 7750e1c) at 5c3b07d; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
