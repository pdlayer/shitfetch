# shitfetch

minimalist, linux-only fetch in C.

the point is simple code, fast startup, and enough config to not be annoying.

## What It Is

- linux-only (`musl` + `glibc`)
- pure C (`C11`)
- KDL config via `ckdl`
- fetch binary: `shitfetch`
- alias binary: `sf`

## Dependencies

- build:
  - C compiler
  - `make`
  - `ckdl` headers (`kdl/parser.h`, etc.)
- link/runtime:
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
./shitfetch --conf-gen
```

## Install

```sh
make install PREFIX=/usr/local
```

Installs:

- `/usr/local/bin/shitfetch`
- `/usr/local/bin/sf` (symlink)
- `/etc/shitfetch/config.kdl` (only if not already present)

## Config (KDL 2.0.0)

Read order:

1. `/etc/shitfetch/config.kdl`
2. `$XDG_CONFIG_HOME/shitfetch/config.kdl` (fallback: `~/.config/shitfetch/config.kdl`)

User config has priority.

Only block-style KDL is supported.

### Supported Nodes

- `logo "auto" | "none" | "<name>"`
- `header true|false`
- `ansi true|false`
- `separator "<text>"` (global key/value separator)
- `colors { ... }`:
  - `key "<color>"`
  - `value "<color>"`
  - `header "<color>"`
  - `border "<color>"`
  - `custom "<color>"`
  - `logo "<color>"`
- `modules { ... }` mixed list entries:
  - `<module> [enabled=<bool>] [key="<text>"] [key-color=<int|string>] [format="<text>"]`
  - `break`
  - `separator "<text>"`
  - `custom "<text>"` or `custom format="<text>"`
  - `colors`

Supported module names:

- `os`
- `kernel`
- `uptime`
- `init`
- `packages`
- `shell`
- `display`
- `dewm`
- `term`
- `cpu`
- `gpu`
- `memory`
- `swap`
- `disk`
- `host`

`custom` placeholders:

- `{user}`, `{host}`
- `{os}`, `{kernel}`, `{init}`, `{wm}` / `{dewm}`, `{term}`
- `{osid}` / `{os-id}`
- modifiers: `:lower` and fixed width `:<n>`; combine as `{term:lower:18}`

Module options:

- every module:
  - `enabled=<bool>`
  - `key="<text>"`
  - `key-color="<color>"` or `keycolor="<color>"`
  - `format="..."` with `{}` as module value placeholder
- disk module:
  - `all=<bool>`: show all real mounted filesystems (`true` by default)
  - `show-fs=<bool>`: append fs type (`true` by default)
  - string args as mount filters (exact mountpoints), for example `disk all=false "/" "/run/media/user/USB"`

### Minimal Example

```kdl
logo "auto"
ansi true
separator ": "
colors {
  key "logo"
  value "light-white"
  header "logo"
  border "light-gray"
  custom "orange"
}
modules {
  os key="OS"
  kernel format="v{}"
  uptime
  break
  separator "----"
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

Color format:

- names: `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`, `white`, `black`
- light/bright variants: `light-gray`, `light-red`, `bright-cyan`, etc.
- extra names: `orange`, `pink`, `purple`
- special: `logo` (use main logo color)
- optional advanced: `#RRGGBB`

### Full Example

See [`config.full.kdl`](./config.full.kdl).

Example module customization:

```kdl
modules {
  shell key="SH"
  memory key="RAM"
  disk key="Disk"
  host enabled=false
  break
  custom "-----"
  colors
}
```

Example disk customization:

```kdl
modules {
  disk key="Disk" all=true show-fs=true
}
```

Only selected mountpoints:

```kdl
modules {
  disk key="Disk" all=false "/" "/run/media/pdlayer/MYUSB"
}
```

## CLI

- `-h`, `--help`
- `-v`, `--version`
- `-g`, `--conf-gen`
- `-c <path>`, `--config <path>`, `--config=<path>`
- `-l`, `--logo NAME`
- `--logo=NAME`
