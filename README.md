# JVMA — JVM Analyzer

Real-time terminal monitor for JVM processes on Linux. It reads only `/proc`:
no JMX agent, no ncurses, no dependencies beyond a C compiler and libm.

> Portuguese documentation (full reference): [README.pt-BR.md](README.pt-BR.md)

## Features

- CPU %, RSS / PSS / swap, estimated heap (vs `-Xmx` when present), threads by state, open FDs vs limit, GC collector type
- 60-sample sparklines with min–max scale and trend (▲ rising, ▼ falling, = flat)
- Threshold alerts (CPU, threads, zombies, FDs, swap)
- Auto-discovery of running JVMs, or attach by PID
- JSON snapshot (`e`) and CSV append (`c`) export
- Adaptive layout (single or two columns), flicker-free rendering
- `NO_COLOR` support and `--ascii` mode for limited terminals

## Build & install

```bash
make                # builds ./jvma
make check          # unit + golden tests
make asan           # same tests under ASAN/UBSAN
sudo make install   # PREFIX=/usr/local by default; DESTDIR supported
```

## Usage

```bash
./jvma                    # pick a JVM from a menu
./jvma 12345              # attach to PID 12345
./jvma 12345 /var/log/jvma   # custom export directory (default ./exports)
./jvma --ascii 12345      # ASCII glyphs (or JVMA_ASCII=1)
NO_COLOR=1 ./jvma 12345   # no colors
```

| Key | Action |
|-----|--------|
| `q` | quit |
| `p` | pause / resume |
| `e` / `c` | export JSON / append CSV |
| `+` / `-` | faster / slower refresh (500 ms steps) |
| `h` | help and alert thresholds |

Minimum terminal size: 70x18.

## What is real and what is not

Everything comes from `/proc`, except:

- **GC pause is not measured.** The TUI shows `n/a` and exports `gc_pause_ms = 0.0`.
  `--sim-gc` (or `JVMA_SIM_GC=1`) enables a random walk for demos only, labelled "simulated".
- **Heap is an estimate** derived from large anonymous mappings in `/proc/<pid>/smaps`.
- **Daemon thread count is unavailable** in `/proc` (shown as `n/a`, always 0 in exports).

To read another user's JVM (FDs, smaps) run JVMA as the same user or with enough privileges.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Licensed under the [MIT License](LICENSE).
