# shitfetch

minimal linux fetch

`shitfetch` is a Linux-only fetch utility written in C.

The goal is to stay tiny and readable, while still being configurable enough
for day-to-day use.

## What It Is

- fetch binary: `shitfetch`
- alias binary: `sf`
- runtime data collection in C

## Dependencies

- build-time:
  - C compiler
  - `make`

## Build

```sh
make
```

## Run

```sh
./shitfetch
./shitfetch --logo arch
./shitfetch --logo none
./shitfetch --template mini
```

Install/uninstall:

```sh
make install PREFIX=/usr/local
make uninstall PREFIX=/usr/local
```

Install places:

- `/usr/local/bin/shitfetch`
- `/usr/local/bin/sf` (symlink)

## CLI

- `-h`, `--help`
- `-v`, `--version`
- `-l`, `--logo <name>`
- `-t`, `--template <default|mini>`

`mini` uses small `pfetch`-style logos and shows `os`, `kernel`, `init`,
`wm/de`, `pkgs`, `shell`, and `memory`.

## Notes

- Linux-only project.
- Intended to stay small and easy to hack.
- Defaults aim for accent-colored keys and neutral values.

## License

ISC, see [LICENSE](./LICENSE).
