| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 127.64 | 158.96 | 1.25x faster | 60.53 | 70.20 | 1.16x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 127.61 | 161.83 | 1.27x faster | 60.53 | 70.27 | 1.16x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 71.77 | 97.79 | 1.36x faster | 45.07 | 42.44 | 1.06x slower |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 72.09 | 98.57 | 1.37x faster | 43.49 | 42.04 | 1.03x slower |
| mixed: 5,016 files, 52 dirs | -l WORD | 62.32 | 70.90 | 1.14x faster | 26.97 | 29.06 | 1.08x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 63.75 | 72.24 | 1.13x faster | 27.04 | 29.20 | 1.08x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (cd5858e, c-utils b74b8a9) at cd5858e; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
