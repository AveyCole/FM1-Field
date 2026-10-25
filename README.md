# FIELD

An independent field-recording radio for the **M-VAVE FM-1**, powered by Radio Aporee.
Browse the world, choose a country or state/province, and listen through a static globe UI.

**[Open the website](https://aveycole.github.io/FM1-Field/) ·
[USB web installer](https://aveycole.github.io/FM1-Field/install/) ·
[Download 0.1.6-alpha](docs/firmware/field-0.1.6-alpha.fwsc)**

## Status

This is experimental firmware. The original alpha was reported working on an FM-1,
and the playback-buffer update was reported to improve cutouts substantially.
The 0.1.6 update adds original startup artwork, a visible Near toggle on EDIT, and a sampler on ARP with a separate shared-memory effects mode on ENV. It retains the network throughput and playback reserve improvements, battery indicator and updated USB flasher. Software checks pass; this build still needs physical-device testing.
Back up your existing firmware's projects before installing. The installer includes
stock-firmware restore and crash-report controls. Use desktop Chrome or Edge with
the FM-1 connected directly by a USB data cable.

The header shows a three-bar battery level, or `USB` when powered by an active USB host.

## Controls

| Control | Action |
| --- | --- |
| OCT− / OCT+ | Another random recording, within the current region |
| REC | Save a favorite on the player / ENV; capture in ARP; remove in the favorites list |
| SEQ | Open favorites; press again to return |
| SAVE | Browse country → state/province → Play random here |
| SELECT / SEL | Move / open in the browser; SEL on the player shows author/licence details |
| PLAY | Play / stop; start the highlighted region or favorite |
| HOME | Up in the browser; worldwide random from the regional player |
| GLO | WiFi setup (2.4 GHz) |
| MASTER | Volume |
| Keys | Play the captured sample on the player and in ARP; hold white keys for FX in ENV |
| ARP | Sample editor: REC capture, PLAY audition, SEL loop; K1 start, K2 end, K3 pitch, K4 level |
| ENV | Separate live-effects mode; clears the sample to reuse its buffer. ENV / HOME returns |
| EDIT | Toggle Near on the player; fixed 50 km neighborhood with a visible Near indicator |

In favorites, turn SELECT to move, then press SEL or PLAY to replay that exact recording.
Favorites survive power-off. Up to 16 fit in a separate 3,840-byte flash object; long
URLs and credits can fill it sooner. A full list asks you to remove an entry.
These are online bookmarks: playback still requires WiFi and the recording to remain available.

The sampler holds one **2.728-second, 8 kHz mono ADPCM clip** (one playback voice).
It survives returning to the main screen, changing recordings, and reopening ARP.
REC again stops capture early; otherwise it stops at capacity. ARP's OCT buttons
transpose; keys use C4 as the original pitch. In loop mode, releasing the triggering
key stops playback. PLAY auditions/stops the clip. Leaving ARP finishes capture.
Clips are temporary: power-off, a new capture, or entering ENV clears them.
ENV reuses the same 12 KiB for effects history; sample playback and FX are exclusive.

Near is anchored when EDIT enables it and survives power-off. It does not drift
with each skip. Its first selection scans the location index, potentially taking
three minutes or longer; later skips use the nearby cache. EDIT disables it;
choosing a region or worldwide random from SAVE also disables it. It never widens
the radius silently if no locations match.

![FM1 FIELD startup](docs/images/field-startup.png)

First startup downloads Aporee's location index and takes about three minutes,
longer on a weak connection. Choosing a new region requires another index scan;
subsequent skips use a 32-location cache. Worldwide mode has its own preserved cache.

The catalogue contains **162 countries and 1,560 states/provinces** with recordings.
It uses a snapshot of Aporee coordinates and generalized Natural Earth boundaries.
Offshore/unclassified points and newly added locations remain available worldwide.
It is a browsing aid, not a precise border map. See [full notes and limitations](APOREE.md).

The device screen and USB product name are **FIELD**.

## Build

The platform is vendored in `base/`; no submodule setup is needed.
Use Linux x86-64, Python with Pillow, JieLi's pi32v2 toolchain, and AC79 SDK
`AC79NN_SDK_V1.2.1_2023-12-13`. SDK binaries are not vendored.

```sh
python3 -m pip install Pillow
AC79_SDK=/path/to/fw-AC79_AIoT_SDK JIELI_TOOLCHAIN=/path/to/toolchain \
  python3 base/tools/build.py app --release 0.1.6-alpha
```

This produces `app/build/field-0.1.6-alpha.fwsc`. Platform setup is documented
in [the upstream README](UPSTREAM-README.md) and [base](base/README.md).
The generated region catalogue is included; Shapely/NumPy and the boundary source
are needed only when rebuilding that catalogue.

## Validation

- Hardware build checks: image, RAM, pool headroom and register access.
- Every compressed geographical lookup compared with the generated mapping.
- Country/state filtering, one-location regions, and worldwide-cache restoration.
- Simulator menu navigation, playback completion, and controls.
- Simulated 1.5-second network pauses at 128/320 kbps without rebuffering;
  a seven-second pause causes one refill and resumes the same recording.

See [APOREE.md](APOREE.md) and `tests/` for details. Software tests do not establish
real-world WiFi reliability on every FM-1.

## Website

The main page flashes directly in place, with an inline confirmation and progress display. Recovery and crash-report tools are below it. The complete static landing page and USB flasher live in `docs/`. GitHub Pages
publishes `main` → `/docs`. All links are relative so the site works under this
repository's Pages path. Firmware downloads, checksums, and previous alphas are included.

## Credits and licences

Built on [fm1-radio](https://github.com/charlesvestal/fm1-radio) by Charles Vestal,
[fm1-base](https://github.com/charlesvestal/fm1-base), and
[felucca-bsp](https://github.com/hugelton/felucca-bsp), from Felucca by
Leo Kuroshita (Hügelton Instruments). See [source provenance](PROVENANCE.md).
Source is MPL-2.0 with third-party components under their own licences;
all notices are retained in [LICENSING.md](LICENSING.md) and `LICENSES/`.

Recordings come from [Radio Aporee](https://aporee.org/maps/) and retain their
contributors' individual licences. Geography is made with Natural Earth (public domain).
FIELD is independent of, and not endorsed by, Radio Aporee or M-VAVE.
