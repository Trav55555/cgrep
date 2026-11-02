| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 119.13 | 150.35 | 1.26x faster | 49.07 | 59.53 | 1.21x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 118.18 | 152.02 | 1.29x faster | 48.60 | 60.75 | 1.25x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 65.88 | 91.99 | 1.40x faster | 25.49 | 31.89 | 1.25x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 66.43 | 93.19 | 1.40x faster | 24.99 | 32.47 | 1.30x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 57.69 | 65.69 | 1.14x faster | 21.70 | 24.21 | 1.12x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 59.02 | 67.24 | 1.14x faster | 22.42 | 25.02 | 1.12x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (0f78815, c-utils 7750e1c) at 0f78815; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 9V74 80-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
