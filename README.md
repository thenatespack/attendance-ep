# Attendance EP

A fullscreen, non-interactive kiosk display for classroom check-in. Built with
[Dear ImGui](https://github.com/ocornut/imgui) on SDL2 + OpenGL.

The screen shows three things and nothing is clickable:

- **Schedule** — today's classes on the left.
- **Check-in QR code** — a QR code with the same code printed as text below it, on the right.
- **Countdown bar** — a bar that drains from 10s to 0s; when it hits zero the code and QR rotate to a new one.

## Dependencies

- A C++17 compiler (`clang++` by default)
- [SDL2](https://www.libsdl.org/) — install via Homebrew: `brew install sdl2`
- OpenGL (system-provided; `OpenGL.framework` on macOS, `libGL` on Linux)

Dear ImGui (`imgui/`) and the QR code generator (`qrcodegen/`,
[Nayuki's QR Code generator library](https://github.com/nayuki/QR-Code-generator), MIT licensed)
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

Removes the binary and all object files.

## Notes

- The schedule shown is placeholder sample data hardcoded in `main.cpp` (`kSchedule`). Swap it
  for a real data source (file/API) when one is available.
- The check-in code is a random 6-character code (ambiguous characters like `0`/`O`, `1`/`I`
  excluded) regenerated every 10 seconds, encoded directly into the QR code.
- `compile_flags.txt` is provided so editors using `clangd` resolve the same include paths as
  the Makefile.
