# shitfetch

minimal linux fetch

`shitfetch` is a Linux-only fetch utility written in C.

The goal is to stay tiny and readable, while still being configurable enough
for day-to-day use.

## What It Is

- fetch binary: `shitfetch`
- alias binary: `sf`
- runtime data collection in C
- KDL-based config (`config.kdl`)

## Dependencies

- build-time:
  - C compiler
  - `make`
  - `ckdl` headers (`kdl/parser.h`, etc.)
- runtime:
  - `libkdl.so`

## Build

```sh
make
```

## Run

```sh
./shitfetch
./shitfetch --logo arch
./shitfetch --logo none
./shitfetch --config ./config.kdl
```

Install/uninstall:

```sh
make install PREFIX=/usr/local
make uninstall PREFIX=/usr/local
```

Install places:

- `/usr/local/bin/shitfetch`
- `/usr/local/bin/sf` (symlink)
- `/etc/shitfetch/config.kdl` (if not already present)

## Configuration

Config load order:

1. `/etc/shitfetch/config.kdl`
2. `$XDG_CONFIG_HOME/shitfetch/config.kdl` (fallback: `~/.config/shitfetch/config.kdl`)

User config overrides system config.

Top-level nodes:

- `logo "auto" | "none" | "<name>"`
- `header true|false`
- `ansi true|false`
- `separator "<text>"`
- `colors { ... }`
- `modules { ... }`

`colors` keys:

- `key`
- `value`
- `header`
- `border`
- `custom`
- `logo`

Supported modules:

- `os`, `kernel`, `uptime`, `init`, `packages`, `shell`, `display`, `dewm`, `term`, `cpu`, `gpu`, `memory`, `swap`, `disk`, `host`

Module properties:

- `enabled=<bool>`
- `key="<text>"`
- `key-color="<color>"` / `keycolor="<color>"`
- `format="...{}..."`

Special `modules` entries:

- `break`
- `separator "<text>"`
- `custom "<text>"`
- `colors`

`custom` placeholders:

- `{user}`, `{host}`
- `{os}`, `{osid}`
- `{kernel}`, `{init}`, `{dewm}`, `{term}`

Disk options:

- `all=<bool>` (default `true`)
- `show-fs=<bool>` (default `true`)
- string args as exact mount filters when `all=false`

Color format:

- ANSI codes (`36`, `38;5;208`, `38;2;R;G;B`)
- named colors (`red`, `cyan`, `light-gray`, `bright-blue`, ...)
- hex (`#RRGGBB`)
- `logo` for the detected main logo accent color

Minimal config:

```kdl
logo "auto"
ansi true
colors {
  key "logo"
  value 39
}
modules {
  os
  kernel
  uptime
  packages
  shell
  display
  dewm
  term
  cpu
  gpu
  memory
  swap
  disk
}
```

## CLI

- `-h`, `--help`
- `-v`, `--version`
- `-g`, `--conf-gen`
- `-c`, `--config <path>`
- `-l`, `--logo <name>`

## Notes

- Linux-only project.
- Intended to stay small and easy to hack.
- Defaults aim for accent-colored keys and neutral values.

## License

ISC, see [LICENSE](./LICENSE).
