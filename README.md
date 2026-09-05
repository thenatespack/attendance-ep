# Attendance EP

A fullscreen, non-interactive kiosk display for classroom check-in. Built with
[Dear ImGui](https://github.com/ocornut/imgui) on SDL2 + OpenGL.

The screen shows three things and nothing is clickable:

- **Schedule** — today's classes on the left.
- **Check-in QR code** — a QR code with the same code printed as text below it, on the right.
- **Countdown bar** — a bar that drains from 10s to 0s; when it hits zero the code and QR rotate to a new one.

## Dependencies

- A C++17 compiler (Apple clang on macOS, GCC on Linux — whatever `make` finds by default)
- [SDL2](https://www.libsdl.org/)
- [libcurl](https://curl.se/libcurl/) (used for the AttendanceApi calls)
- OpenGL dev headers (system-provided on macOS; a `libgl`/`mesa` dev package on Linux)

macOS (Homebrew):

```sh
brew install sdl2 curl
```

Debian/Ubuntu:

```sh
sudo apt install build-essential libsdl2-dev libcurl4-openssl-dev libgl-dev pkg-config
```

Fedora:

```sh
sudo dnf install gcc-c++ SDL2-devel libcurl-devel mesa-libGL-devel pkgconf-pkg-config
```

Dear ImGui (`imgui/`), the QR code generator (`qrcodegen/`,
[Nayuki's QR Code generator library](https://github.com/nayuki/QR-Code-generator), MIT licensed),
and the JSON library (`json/`, [nlohmann/json](https://github.com/nlohmann/json), MIT licensed)
are vendored directly in this repo — no package manager needed for those.

## Build

```sh
make
```

This compiles everything into object files and links the `imgui-test` binary.

## Run

```sh
make run
```

or just run the binary directly after building:

```sh
./imgui-test
```

## Clean

```sh
make clean
```

Removes the `build/` directory and the binary.

## Configuration

Copy `.env.example` to `.env` and fill in the AttendanceApi's URL plus whichever auth method
applies (device API key, user token, email/password, or the dev-token fallback for local
development) — see the comments in `.env.example` for details. If the API is unreachable or
unconfigured, the app falls back to built-in offline demo data instead of failing.

## Versioning

The header bar shows a version string built at compile time from two pieces (see the
`APP_VERSION` rule in `Makefile`):

- `VERSION` — a plain semver string, bumped by hand for releases.
- The current commit's short hash (and a `-dirty` suffix if the working tree has uncommitted
  changes), read from git.

So every build is traceable back to the exact commit (and any local edits) it came from, e.g.
`1.0.0+68ef60b` or `1.0.0+68ef60b-dirty`. This is regenerated on every `make` invocation, and
only triggers a rebuild of `main.cpp` when the string actually changes.

## Notes

- `compile_flags.txt` is provided so editors using `clangd` resolve the same include paths as
  the Makefile.
