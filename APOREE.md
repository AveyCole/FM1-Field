# APOREE FM-1 — experimental 0.1.2-alpha

A standalone field-recording firmware for the M-VAVE FM-1, derived from
Charles Vestal's fm1-radio and its pinned fm1-base / felucca-bsp platform.
The original CREATURE firmware in the parent workspace is independent.

## Controls

- OCT− / OCT+: choose a new random recording worldwide. Both buttons shuffle.
- PLAY: play / stop. GLO: WiFi setup, scan, saved or hidden network, password entry.
- SEL: toggle location/coordinates and recording author/licence details.
- SELECT / knob 4: another random recording. MASTER: volume.
- Hold a white key: the reference radio's effects. ARP: its optional synth.
- Hold HOME: controls. OCT− and OCT+ held together retain the platform's update entry.
- SAVE: browse countries, then states/provinces. SELECT moves, SEL opens,
  PLAY starts random playback in the highlighted region. A country has a
  "Play whole country" choice. HOME moves up in the browser.
- In regional playback, OCT− / OCT+ stay in that region. HOME on the regional
  player returns to worldwide random. GLO still handles WiFi.

0.1.2 uses a forest green screen with warm cream text and muted orange highlights.

The globe jumps directly to the selected recording's actual latitude and longitude,
with a coral marker. It is drawn once per location or page return, with no animation.
Unchanged text rows are also left alone. Long titles and credits scroll.
When a file finishes, another location is selected after the decoded audio drains.

## Data and startup

The FM-1 fetches `https://aporee.org/api/maps/getAllLocations` with the same
`X-Requested-With: XMLHttpRequest` and Referer headers as Aporee's map.
The index was 8.55 MB / 68,361 locations when tested. It is streamed through a
bounded parser, never held in RAM. At the inherited safe receive pacing of
48 KB/s, first startup takes about three minutes, possibly longer on weak WiFi.
The display shows received KB. Keep it powered while it loads.

A reservoir samples 32 locations uniformly from the complete index. Each skip
uses a different location from that batch and fetches its `getPlaylist?locids[]=ID`
response. A recording is randomly selected from that location's playable MP3s.
After 32 selections the index is fetched again. The cache is in RAM and resets
after a reboot. Selection is uniform by location, then by recording within it;
it is not uniform over all recordings or over Earth's surface. Random coordinates
without recordings are never invented.

## Regions

The built-in catalogue covers 162 countries and 1,560 states/provinces with
recordings, using a snapshot of Aporee's public location coordinates and Natural
Earth's public-domain 10m administrative boundaries. Of 68,361 locations in that
snapshot, 63,680 intersect a named administrative area. The remaining 4,681
(including offshore and unmapped points) remain available in worldwide mode.
Natural Earth's generalized boundaries are a browsing aid, not precise borders.
New recordings remain available worldwide; their region assignments need a future
catalogue rebuild. Region names are transliterated for the device's ASCII font.

The complete location-to-region lookup is compressed into the firmware; no
geocoding service, proxy, or extra account is used. A new region requires a full
live Aporee index scan (about three minutes) to filter and sample its locations.
Later skips use that region's cache. The remaining worldwide cache is preserved
when entering a region, so HOME can reuse it. Counts are snapshot location counts,
not numbers of recordings. Very small regions may repeat a location.

`app/tools/build_regions.py` regenerates the catalogue with Shapely and NumPy
from `tmp-locations.json` and Natural Earth's
`geojson/ne_10m_admin_1_states_provinces.geojson`, saved as
`geography/states.geojson`. The boundary source and SHA-256 are recorded in
`geography/report.json`. Natural Earth: https://www.naturalearthdata.com/about/terms-of-use/

No audio is bundled or uploaded. The region-assignment snapshot is bundled in
the firmware; recording metadata and playback streams come directly from
Aporee. Author and licence metadata travel with each selected track. Aporee is
not affiliated with this firmware. Upstream HTTPS transport does not validate
server certificates; that existing limitation is retained.

## Build and validation

0.1.1 removes the unused AAC decoder and its permanently reserved arena. The MP3
network buffer grows from 20 KiB to 80 KiB, and the decoded buffer from 4,608 to
6,912 stereo frames (~157 ms at 44.1 kHz). Large buffers are allocated at boot,
before WiFi's allocations can fragment the heap. Startup builds roughly two
seconds of compressed audio; after a network underrun it builds roughly three,
capped below the ring capacity for high-bitrate files. Completed short files play
without waiting for that threshold. Buffering progress appears on the screen.
The previous one-second globe-animation delay before fetching audio is removed.

These reserves absorb temporary interruptions, but cannot compensate indefinitely
if the connection's sustained download rate is below the recording's bitrate.
The first location-index download is unchanged. MP3 is the only supported codec.

Linux x86-64, Python with Pillow, JieLi pi32v2 toolchain, and AC79 SDK tag
`AC79NN_SDK_V1.2.1_2023-12-13` are required. See upstream README for setup.

```sh
AC79_SDK=/path/to/sdk JIELI_TOOLCHAIN=/path/to/toolchain \
  python3 base/tools/build.py app --release 0.1.2-alpha
python3 base/tools/build.py app --host
mkdir -p app/build/ui-check
app/build/host/aporee_host tests/aporee.host app/build/ui-check
cc -std=c99 -Wall -Wextra -Werror tests/aporee_net_test.c -o app/build/aporee_net_test
app/build/aporee_net_test tmp-locations.json tmp-playlist.json
```

The host network deliberately supplies labelled simulated metadata. It checks
device screen layout and controls, not WiFi, TLS or real audio output.
Network/parser tests use captured public Aporee JSON with the real selection
code and a filesystem transport. The real MP3 decoder passed short-file EOF,
automatic-next and stop tests. A paced network fixture survived a 1.5-second
pause without rebuffering at both 128 and 320 kbps. A seven-second pause at
128 kbps caused one refill, then resumed the same recording. The static globe
was drawn just twice (empty, then selected) across an 18-second run.
The original 0.1.0 alpha was reported working on an FM-1, with frequent buffering.
This 0.1.1 update still needs physical testing. Simulator timing does not reproduce
the FM-1's CPU, radio conditions, TLS performance or actual server throughput.

The region tests verify all 74,066 compressed ID slots against the uncompressed
mapping, country and state filtering, one-location regions, worldwide-cache
restoration, and menu navigation. The 0.1.2 region browser still needs device testing.

The hardware build passes: image 575,280 bytes, RAM 130,496 / 131,072 bytes,
pool 308,880 / 311,296 bytes (2,416 bytes spare, above the required 2,048).

## Files and provenance

- `app/aporee_ui.c`: globe, controls, metadata and playback state machine.
- `app/net/aporee_net.c`: location reservoir, playlists and API tasks.
- `app/regions.c`, `regions_data.h`: compressed region lookup and catalogue.
- `app/aporee_json.h`: bounded incremental metadata reader.
- `app/radio.c`: MP3 decoder/effects, finite-file EOF handling and buffering policy.
- `app/third_party/pvaac/getbits.h`: static inline fix for modern C99 host compilers.
- `tools_convert.py`: one-time conversion record; do not rerun on edited sources.
- `update_playback_011.py`: one-time 0.1.1 migration record; do not rerun.

Upstream MPL-2.0 notices, third-party notices, and Apache-2.0 SDK attribution are
retained. See `LICENSING.md`, `LICENSE` and `LICENSES/`. The installer reuses
fm1-base's package checks, USB-MIDI transfer, stock restore and crash-report UI.
