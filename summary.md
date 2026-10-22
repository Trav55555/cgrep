| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 76.66 | 107.72 | 1.41x faster | 45.32 | 56.02 | 1.24x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 76.41 | 108.60 | 1.42x faster | 45.37 | 56.17 | 1.24x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 43.61 | 65.53 | 1.50x faster | 31.20 | 32.82 | 1.05x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 43.72 | 66.23 | 1.51x faster | 31.11 | 32.82 | 1.05x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 57.85 | 71.21 | 1.23x faster | 27.05 | 31.89 | 1.18x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 60.23 | 74.46 | 1.24x faster | 28.36 | 32.92 | 1.16x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (805650d, c-utils b74b8a9) at 805650d; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, INTEL(R) XEON(R) PLATINUM 8573C. Shared CI runners are noisy: compare ratios, not absolute times.
