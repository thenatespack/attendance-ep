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
- OpenGL dev headers: desktop GL on macOS, **GLES2** on Linux (see note below)

macOS (Homebrew):

```sh
brew install sdl2 curl
```

Debian/Ubuntu:

```sh
sudo apt install build-essential libsdl2-dev libcurl4-openssl-dev libgles2-mesa-dev pkg-config
```

Fedora:

```sh
sudo dnf install gcc-c++ SDL2-devel libcurl-devel mesa-libGLES-devel pkgconf-pkg-config
```

> **Why GLES2 on Linux, not desktop GL?** This app targets Raspberry Pi as a real deployment
> device, and its Mesa V3D driver only reliably hands out GLES-capable EGL configs — requesting
> any desktop OpenGL Core context (even as low as 3.0) fails with `EGL_BAD_MATCH`. `main.cpp`
> requests a GLES2 context on all non-Apple platforms accordingly, and the Makefile links
> `-lGLESv2` with `-DIMGUI_IMPL_OPENGL_ES2` there. If you're targeting a desktop Linux box with
> a proper desktop GL driver instead, you can switch this back — see the `#ifdef __APPLE__`
> block in `main.cpp` and the `ifeq ($(UNAME_S),Darwin)` block in `Makefile`.

Dear ImGui (`imgui/`), the QR code generator (`qrcodegen/`,
[Nayuki's QR Code generator library](https://github.com/nayuki/QR-Code-generator), MIT licensed),
and the JSON library (`json/`, [nlohmann/json](https://github.com/nlohmann/json), MIT licensed)
are vendored directly in this repo — no package manager needed for those.

## Build

```sh
make
```

This compiles everything into object files and links the `imgui-test` binary.

## Cross-building for Linux (e.g. Raspberry Pi) from macOS

```sh
make linux-arm64   # -> dist/imgui-test-linux-arm64
make linux-amd64   # -> dist/imgui-test-linux-amd64
make linux-all     # both
```

Builds inside a Debian Bookworm container via Docker buildx. Debian Bookworm is what Raspberry
Pi OS (64-bit) is built on, so it links against the same SDL2/libcurl/libGL SONAMEs already on
the device; just copy the binary over and run it alongside your `.env`.

Requires Docker (with buildx, bundled in Docker Desktop and OrbStack) running locally. On
Apple Silicon, `linux-arm64` runs natively (no QEMU) while `linux-amd64` runs emulated (slower);
on an x86_64 host it's the other way around. For a 32-bit Raspberry Pi OS, edit the
`--platform` flag in the Makefile target (or `Dockerfile.linux`'s comment) to `linux/arm/v7`.

## Releases (prebuilt binaries)

Pushing a version tag whose commit is on `main` builds Linux `arm64` and `amd64` binaries in CI
(`.github/workflows/release.yml`) and publishes them as a GitHub Release — no need to
clone/build on the Pi at all:

```sh
git tag v1.0.1
git push origin v1.0.1
```

Then on the Pi:

```sh
curl -L -o imgui-test https://github.com/thenatespack/attendance-ep/releases/latest/download/imgui-test-linux-arm64
chmod +x imgui-test
```

`releases/latest/download/...` always points at the newest (non-prerelease) release, so that
URL never changes. You can also trigger the same build without cutting a release from the
Actions tab (`workflow_dispatch`) — that run's binaries show up as workflow artifacts instead.

Tags pushed from a commit that isn't on `main` are intentionally skipped by CI (see the `gate`
job in the workflow) so they don't spend Actions minutes — use a local/beta release instead.

### Local / beta releases

To cut a release without CI at all — e.g. a beta build from a feature branch — build both
binaries locally and publish them yourself with the [`gh` CLI](https://cli.github.com/):

```sh
make linux-all
git tag v1.1.0-beta.1
git push origin v1.1.0-beta.1
gh release create v1.1.0-beta.1 \
    dist/imgui-test-linux-arm64 dist/imgui-test-linux-amd64 \
    --prerelease --generate-notes
```

Drop `--prerelease` for a real release cut this way instead of through CI. Either way this
never touches GitHub Actions, so it costs nothing beyond the Docker build time on your laptop.

## Self-updating deployment on the Pi

The Pi can keep itself up to date on its own: a systemd timer checks GitHub every 5 minutes
(adjust `OnUnitActiveSec=` in `deploy/systemd/attendance-update.timer` to change the cadence),
and if the latest release differs from what's installed, downloads the matching binary, swaps
it in, and restarts the app. If the new binary doesn't survive a short grace period after
restart, it's automatically rolled back to the previous one — see `deploy/bin/update.py` for
the full logic. Because `GET .../releases/latest` excludes prereleases by definition, a beta
release published with `--prerelease` (above) never reaches the kiosk this way.

One-time setup on a fresh Pi:

```sh
sudo mkdir -p /opt/attendance-ep/bin /opt/attendance-ep/state

# Place the initial binary (matching this Pi's arch) and your .env:
sudo cp imgui-test-linux-arm64 /opt/attendance-ep/imgui-test
sudo chmod +x /opt/attendance-ep/imgui-test
sudo cp .env /opt/attendance-ep/.env

# Seed the installed-version marker with the tag that matches the binary
# above, so the first check doesn't treat it as an update to apply:
echo "v1.0.1" | sudo tee /opt/attendance-ep/state/installed_version

# Install the updater script and systemd units:
sudo cp deploy/bin/update.py /opt/attendance-ep/bin/update.py
sudo chmod +x /opt/attendance-ep/bin/update.py
sudo cp deploy/systemd/attendance-kiosk.service /etc/systemd/system/
sudo cp deploy/systemd/attendance-update.service /etc/systemd/system/
sudo cp deploy/systemd/attendance-update.timer /etc/systemd/system/
sudo systemctl daemon-reload

sudo systemctl enable --now attendance-kiosk.service
sudo systemctl enable --now attendance-update.timer
```

> **Verify the GUI environment before walking away.** `attendance-kiosk.service` assumes
> desktop autologin as user `endpoint` on `DISPLAY :0` (see the
> `User=`/`Environment=DISPLAY=`/`Environment=XAUTHORITY=` lines in
> `deploy/systemd/attendance-kiosk.service`). If your Pi autologs in as a different user, runs a
> Wayland-only compositor without XWayland, or uses a different display number, adjust those
> lines — otherwise SDL fails to find a display and the service crash-loops (systemd logs this
> as `Failed to determine user credentials: No such process` if the `User=` name doesn't exist
> at all). Check the logged-in username with `whoami` and the display with `echo $DISPLAY` in
> the Pi's actual desktop session, then `sudo systemctl daemon-reload && sudo systemctl restart
> attendance-kiosk` after editing.

Operator commands:

```sh
systemctl status attendance-kiosk          # is the app running?
systemctl status attendance-update.timer   # is the update timer scheduled?
journalctl -u attendance-kiosk -f          # live app logs
journalctl -u attendance-update -e         # the last update check's result
sudo systemctl start attendance-update.service   # force an immediate check
cat /opt/attendance-ep/state/installed_version   # what's currently installed
```

This trusts anyone with release-publish permission on the repo, protected only by HTTPS —
there's no separate binary-signing/checksum infrastructure on top of that.

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

## NFC auto-checkin (RC522)

Under `ATTENDANCE_API_KEY` (endpoint) auth, tapping a card on an RC522 reader auto-checks the
student in via `POST /api/ClassSessions/auto-checkin` — no QR/code needed. See
`ATTENDANCE_NFC_SPI_DEVICE` in `.env.example` and the wiring/protocol notes at the top of
`nfc_reader.h`. Setup on the Pi:

1. Wire the RC522: SDA/CS→SPI CE0 (pin 24), SCK→SPI SCLK (pin 23), MOSI→SPI MOSI (pin 19),
   MISO→SPI MISO (pin 21), RST→3.3V (pin 17, *not* a GPIO — the app soft-resets the chip over
   SPI instead), 3.3V→3.3V, GND→GND. IRQ is unused.
2. Enable SPI: `sudo raspi-config` → Interface Options → SPI → Enable (or add
   `dtparam=spi=on` to `/boot/firmware/config.txt` and reboot). This creates `/dev/spidev0.0`.
3. Register each card's UID with a student via the AttendanceApi (outside the scope of this
   app) — auto-checkin only resolves cards the server already knows about.

Only single-size (4-byte) UIDs are supported, which covers the Mifare Classic cards/keyfobs
bundled with most RC522 starter kits. If no reader responds on `/dev/spidev0.0` (unplugged, SPI
disabled, wrong wiring, or just not present — e.g. running the app on a dev machine),
auto-checkin silently stays disabled; check `journalctl -u attendance-kiosk` for
`NfcCheckInWorker`/`NfcReader` log lines if a card tap isn't registering.

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
