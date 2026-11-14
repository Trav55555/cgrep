| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 123.73 | 154.86 | 1.25x faster | 57.57 | 66.21 | 1.15x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 124.71 | 157.80 | 1.27x faster | 58.41 | 67.78 | 1.16x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 68.27 | 94.91 | 1.39x faster | 29.32 | 41.27 | 1.41x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 68.23 | 95.58 | 1.40x faster | 29.14 | 41.32 | 1.42x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 58.21 | 66.13 | 1.14x faster | 24.64 | 26.84 | 1.09x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 59.54 | 67.89 | 1.14x faster | 25.45 | 27.73 | 1.09x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (c706ac5, c-utils 9e8fc89) at c706ac5; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
