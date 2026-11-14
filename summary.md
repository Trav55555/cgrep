| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 117.93 | 149.88 | 1.27x faster | 48.78 | 59.19 | 1.21x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 117.52 | 151.83 | 1.29x faster | 48.50 | 59.80 | 1.23x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 65.14 | 91.31 | 1.40x faster | 25.22 | 31.22 | 1.24x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 65.09 | 92.49 | 1.42x faster | 25.33 | 31.61 | 1.25x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 56.20 | 64.18 | 1.14x faster | 20.99 | 23.90 | 1.14x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 57.68 | 66.24 | 1.15x faster | 21.84 | 24.63 | 1.13x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (0feed3e, c-utils f7cb1fc) at 0feed3e; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 9V74 80-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
