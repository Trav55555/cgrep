| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 132.29 | 162.67 | 1.23x faster | 60.69 | 68.89 | 1.14x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 131.66 | 164.40 | 1.25x faster | 60.90 | 68.99 | 1.13x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 74.09 | 104.69 | 1.41x faster | 31.05 | 40.53 | 1.31x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 73.51 | 105.04 | 1.43x faster | 30.81 | 40.91 | 1.33x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 61.99 | 70.10 | 1.13x faster | 26.25 | 28.54 | 1.09x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 62.87 | 72.18 | 1.15x faster | 26.87 | 28.64 | 1.07x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (5a44692, c-utils 7750e1c) at 5a44692; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
