# FIELD — experimental 0.1.8-alpha

A standalone field-recording firmware for the M-VAVE FM-1, derived from
Charles Vestal's fm1-radio and its pinned fm1-base / felucca-bsp platform.
The original CREATURE firmware in the parent workspace is independent.

## Controls

- OCT− / OCT+: choose a new random recording worldwide. Both buttons shuffle.
- PLAY: play / stop. GLO: WiFi setup, scan, saved or hidden network, password entry.
- SEL: toggle location/coordinates and recording author/licence details.
- SELECT / knob 4: another random recording. MASTER: volume.
- Keys on the player and ARP play the same sample. ARP opens its editor; REC captures there.
- ENV opens the original live effects on white keys, clearing the sample to reuse its memory.
- EDIT toggles a fixed 50 km Near mode on the player; the header shows Near.
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

## Playback scheduling and battery (0.1.3)

The compressed ring is still 80 KiB and PCM reserve 6,912 stereo frames.
This release addresses CPU stalls rather than claiming that more buffering
alone fixes them: a continuously readable HTTPS file could monopolize the
cooperative network task, while the scheduler ran eight rounds before returning
to the decoder. A 16 KiB TLS record also had no yields during AES/GHASH work.

AES-CTR now uses BearSSL's constant-time implementation in 512-byte slices;
GHASH is sliced at block boundaries too. Both yield between slices. Record
authentication remains intact. The stream task also yields after each read.
The WiFi scheduler returns after a 4 ms soft budget, checked after each task
returns/yields, and rotates its starting slot so later tasks are not starved.
This is cooperative, not a hard preemption guarantee for arbitrary SDK tasks.
No HTTP downgrade, proxy, transcoding, or recording selection restriction is used.

`sh tests/run_stream_tests.sh` verifies CTR/GHASH/GCM compatibility for
AES-128/192/256, partial and full TLS records, authentication failure on altered
tags, and slice yields. A timed scheduler harness with 13 runnable tasks and
3 ms work slices reproduces a 312 ms old dispatch (longer than the 157 ms PCM
reserve); the new scheduler returns within 6 ms and services every task. These
are software regressions, not hardware throughput or drop-out measurements.
Existing 320 kbps/1.5-second pause, seven-second outage recovery, EOF, region
navigation, and captured Aporee API checks also pass.

The header shows three battery bars using the platform's stock voltage
thresholds and hysteresis. Empty is orange, an unread value shows `?`, and an
active USB host shows `USB`. There is no charger-status signal; `USB` indicates
host power, not confirmed charging. It does not invent a percentage. Cached
headers redraw only when their displayed content or battery state changes.

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
  python3 base/tools/build.py app --release 0.1.8-alpha
python3 base/tools/build.py app --host
mkdir -p app/build/ui-check
app/build/host/field_host tests/aporee.host app/build/ui-check
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

The 0.1.2 hardware build passed at image 575,280 bytes, RAM 130,496 / 131,072
bytes and pool 308,880 / 311,296 bytes. See the 0.1.3 release verification below
for the current image sizes.

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

## 0.1.3 release verification

image 576672 B; RAM .data+.bss 130496 B of 131072; pool 308880 B of 311296. The hardware image, RAM, pool-headroom and register-access
checks passed. The startup name and USB product are FIELD; package identity
remains in the compatible FM-1_84 family (`FM-1_84000103`).

## USB flasher handoff update

The browser updater now explicitly closes native MIDI input/output handles before
loader discovery, awaits those closes, ignores disconnected output endpoints, and
recognizes FIELD product names. It wakes on MIDI state changes and retries from
fresh endpoint lists, with cleanup after partial opens and failed installs.
Firmware 0.1.3-alpha is unchanged; refresh the main page to use the new updater.

Run `node tests/usb_handoff_test.mjs` for the simulated firmware-to-loader-to-firmware
transition that requires old handles to close before re-enumeration. It also checks
stale endpoints, partial-open failures and event-listener cleanup. Physical USB
behavior still depends on the browser and host driver and needs a device test.

## Network throughput and reserve (0.1.4)

The user confirmed the screen displays Buffering during cutouts, indicating
compressed-input starvation. The inherited 6-segment TCP receive window was
7,200 bytes (MTU 1,240; MSS 1,200). Its window/RTT ceiling at 200 ms is only
288 kbps, below a 320 kbps MP3. The window is now 12 segments / 14,400 bytes.
The existing 16-pbuf pool is unchanged, leaving four buffers beyond a full
window. This changes the amount of in-flight TCP data, not the audio ring size.

Startup and refill now wait for 77,824 compressed bytes, rather than estimating
2–3 seconds from the current MP3 frame's bitrate. This uses the existing 80 KiB
ring, handles VBR more conservatively and gives about 4.9 seconds at 128 kbps or
1.9 seconds at 320 kbps. Short completed files still start without filling it.
Expect a longer initial wait on low-bitrate recordings.

A host bandwidth-delay model (48 KB/s link, 200 ms round trip, 320 kbps MP3)
reproduces a refill with the old window and no refill over 26 simulated seconds
with the new window. This is not a real TCP, retransmission or WiFi simulation,
and does not establish actual hardware throughput or guarantee zero buffering.
The seven-second outage still pauses once and resumes the same recording.
Real FM-1 playback on the user's connection remains to be tested.

Hardware build: image 576592 B; RAM .data+.bss 130496 B of 131072; pool 308880 B of 311296. RAM and pool allocation are unchanged from 0.1.3;
image, memory headroom and register-access checks pass. USB identity remains
FIELD; package identity is `FM-1_84000104`. The browser USB handoff fix is retained.

## USB discovery revision 2

One discovery path now accepts either the running app or its already-running
loader, waits through enumeration and refreshes MIDI access during retries.
The one-shot loader probe was removed: it could miss a late loader, then reject
it as not-running-firmware. Discovery handles Windows-prefixed FIELD names and
unequal input/output labels. An existing loader proceeds directly to writing,
then the updater checks the final firmware identity. Failed discovery prints
actual MIDI port names, states, connection states and probe/open errors locally.

Tests cover delayed loader appearance, a fresh MIDI-access object, unequal
labels, multiple outputs and Windows-prefixed names. Firmware 0.1.4-alpha is
unchanged. These tests do not establish actual host-driver behavior.

## Recording favorites (0.1.5)

REC on the player saves the current exact recording, with its URL, sound ID,
coordinates, title, place, author and licence. SEQ opens the saved list; SELECT
moves, SEL or PLAY recalls the highlighted sound, REC removes it, and SEQ or HOME
returns. The player labels a recalled sound FAVORITE. OCT skips retain the current
random region. WiFi is still required; audio files are not downloaded to flash.

Bookmarks use the existing OBJ_FAV sector pair with a new validated format,
independent of WiFi and radio settings. Up to 16 entries share 3,840 bytes of
variable-length storage; long metadata can reach the byte limit sooner. URLs and
credits are never truncated. Duplicate saves avoid flash writes. Full storage,
allocation failure, and flash write failure give feedback; a failed removal
reloads the previously saved list. Malformed or legacy station-favorite objects
are ignored. The 3,840-byte list is allocated only during a save or while the list
is open and released on leaving it; permanent RAM grows by 32 bytes.

An in-flight random metadata request is allowed to finish but cannot replace a
recalled bookmark or overlap its audio network task. The existing compressed and
PCM buffers, streaming scheduling, battery indicator and USB discovery fixes are
retained.

Validation: packed-storage unit tests cover exact URL and credit round trips,
duplicates, both capacity limits, malformed storage and removal. Host UI tests
cover navigation, exact replay, failed writes, reboot persistence, empty lists,
and recall during a delayed metadata request. Region/player, streaming crypto,
cooperative scheduling and USB handoff regressions pass. Hardware compilation
and memory/register checks pass: image 578752 B; RAM .data+.bss 130528 B of 131072; pool 308880 B of 311296. Physical FM-1 testing remains necessary.

Run the new tests with `PYTHON=python3 sh tests/run_field_favorites.sh` (requires
the host build dependencies), plus `sh tests/run_stream_tests.sh` and
`node tests/usb_handoff_test.mjs`.

## Startup, Near, sampler and ENV effects (0.1.6)

The new original startup artwork combines a geometric FIELD wordmark, FM1,
terrain contours, a waveform and a globe, with THE WORLD, RECORDED. It uses six
40-row strips of the existing canvas: no image buffer, extra font, animation or
artificial delay. The host `splash` command renders exactly the same function.

EDIT on the player toggles Near, visibly shown in the header and NEAR / 50 KM
label. The current recording becomes a fixed anchor. A 50 km great-circle
haversine filter is applied to streamed Aporee coordinates, independent of
political borders. It handles longitude wrapping and poles. The nearby reservoir
uses the existing region cache, preserving the world cache; changing the anchor
invalidates the nearby cache. The first selection requires a full location-index
scan. It never falls back to a more distant location. The flag and coordinates
are saved in OBJ_RADIO; old settings load with Near off and synth settings intact.
Choosing a region/world from SAVE turns Near off. EDIT does not interrupt the
current sound; the next shuffle applies the selection mode.

ARP replaces the synth editor with a single-clip sampler. REC captures up to
21,824 mono samples at 8 kHz (2.728 seconds), using IMA ADPCM in 64-sample blocks
with predictor/index headers. The bank is the existing 12,288-byte FX history,
with a 128-byte decoded-block cache for pitched access. Capture averages source
frames before keyboard mixing; network stalls do not add silence. REC again
finishes early, as do leaving ARP or changing/stopping the source. K1/K2 trim,
K3 transposes, K4/SELECT set level, OCT transposes by octaves, SEL toggles loop,
PLAY auditions/stops, and both the player and ARP keyboards play the same clip.
C4 is original pitch. Playback is monophonic; one shots continue after key release,
loops release when their triggering key is lifted. Short fades soften boundaries.
As of 0.1.8, the live radio is monitored in ARP as well as on the player.
ALGORITHM left mutes its monitor in ARP; right restores it. Capture and sample
playback remain available. The former optional synth is no longer in the UI.

ENV opens a separate live-effects page. Its white keys retain the original 16
effects; FX taps tempo. Entering ENV explicitly clears any temporary clip and
zeros the shared history; the screen reports Sample cleared for FX. Sampler and
effects never own the buffer simultaneously. ENV/HOME returns to the player,
and ARP returns to an empty sampler. Samples are not saved to flash and disappear
on power-off. REC still saves favorites on the player and in ENV, and SEQ opens
favorites. The compressed ring, PCM ring, TCP window and WiFi configuration are
unchanged. USB discovery changes from earlier releases are retained, but the
user's intermittent hardware/browser detection issue remains unconfirmed.

Validation: capture length and guard bytes, full/partial capture, paused input,
decode fidelity (35.7 dB SNR for the test sine), trim safety, keyboard replay on
both screens, controls and exclusive FX handoff pass. Near tests use captured
public Aporee responses and check cache restoration, date line, poles, and the
radius boundary. Persistent toggle and failed-write behavior pass host tests.
Existing favorites, regional browsing, TLS crypto, scheduler and USB updater
regressions pass. The FM-1 build passes memory and register-access checks:
image 579744 B; RAM .data+.bss 130592 B of 131072; pool 308880 B of 311296. Real-device playback, sampler CPU load and USB detection still need testing.

Run `PYTHON=python3 APOREE_TEST_STREAM=/path/to/long.mp3 sh tests/run_field_modes.sh`.
The real API fixture test is `cc -O1 -Ibase/include tests/aporee_net_test.c -o
/tmp/aporee_test && /tmp/aporee_test tmp-locations.json tmp-playlist.json`.

## USB recovery, loop latch, slices and contextual OCT (0.1.7)

The browser still accepts both running FIELD and a loader, and the normal path
requests update mode automatically. Initial discovery and the loader handoff now
wait up to 45 seconds. Native MIDI opens are bounded at four seconds, closes at
one second, and access refresh at 1.5 seconds. Timed-out opens are skipped while
pending and late handles are closed. Recently enumerated endpoints are preferred
over retained ghosts. Probe passes honor their outer deadline. This prevents a
hung native MIDI operation from blocking all subsequent discovery indefinitely.

The new loader explicitly disconnects USB for 350 ms before its startup screen,
and the application-to-loader handoff holds D+ off for 350 ms. While idle, a lack
of MIDI RX traffic for 12 seconds triggers a 350 ms soft detach, transport queue
reset, and reattachment. This recovers a live controller with stalled host
enumeration; the inherited usb_retry only retries when the controller itself is
down. The idle policy runs outside ldr_session, never during firmware transfer
or flash writes. Failed sessions receive a fresh idle grace period. This loader
is staged from the new package when an install starts from running firmware; a
loader already started from an older package does not gain this code magically.
Physical Windows/Chrome behavior remains to be confirmed; the changes are not
a guarantee against an OS driver fault or another app holding the MIDI device.

ARP SEL cycles One shot, Loop hold, Loop latch, Slice, then One shot. Held loops
stop on key release; latched loops keep playing across screens and stop with
their key again or PLAY in ARP. Slice maps the trimmed sample across all 27 keys
as equal, contiguous, non-looping ranges at the same base pitch; manual tuning
and octave transposition still apply. PLAY auditions the selected slice. No
additional audio bank is allocated. ENV still clears the clip for its shared FX
history. REC remains capture in ARP and favorites on the player / ENV.

OCT skips in place on the player, regions, favorites, WiFi/network list, effects
and help pages, retaining the current Near/region scope. Existing local functions
take precedence: ARP transposes sample octaves, and WiFi name/password entry
uses OCT as its character-layer modifiers. The current page stays open.

Tests cover running FIELD and already-running loader installs, late enumeration,
Windows names, never-resolving native opens/closes and cleanup of late handles.
Loader idle policy tests cover host traffic, active-session suppression and timer
wrap. The real loader transfer test verifies app writes, preserved flash head,
update-record cleanup, no-op reinstall and damaged-image refusal. Sampler tests
cover latched playback across navigation, stop/retrigger controls, all 27 slices,
trimmed coverage, bounds and existing capture behavior. Contextual OCT, favorites,
streaming crypto and scheduler regressions pass. Build checks pass: image 580032 B; RAM .data+.bss 130592 B of 131072; pool 308880 B of 311296.
Hardware USB recovery and playback still need a physical-device test.

## Sampler monitoring, quieter screen, tap tempo and USB display (0.1.8)

ARP now retains the live recording by default. ALGORITHM left mutes only its
monitor; right restores it, with a 220-sample gain ramp. Capture taps the real
decoded source before this gain, so recording still works while muted. Sample
playback remains audible. The mute is scoped to ARP, remembered when reopening
the editor, and reset on boot. The UI replaces the key activity display and
large knob cells with static original forest/river artwork, a monitor status
line and four compact knob readouts. Keyboard playing and all SEL modes remain.

FX taps work on every page. ENV shows BPM permanently, HOME briefly shows it
after taps, and the FX LED pulses on the beat. The stutter history window is
recomputed when tempo changes while its key stays down; previously it retained
the old timing until retriggered.

The optional docs/mirror viewer subscribes with F0 7D 57 60 OP F7 (0 stop,
1 subscribe/heartbeat, 2 refresh). Device output uses packed-7-bit 00 59 pixel
messages: 50 hello, 51 RGB565 runs, 52 fill. Queue and scratch are allocated as
one 3,928-byte heap object only after a viewer connects, retaining at least
8 KiB spare heap. A 2 KiB low-priority USB queue never blocks the main loop;
row-progress caches retry incomplete strips. Disconnect, link changes and a
six-second heartbeat timeout free the object. Firmware update entry frees it
too. The original larger platform mirror remains disabled: this custom
pixel-only path enables only its USB queue transport. Screen contents are
read-only. The browser validates bounds, packet length and checksum before
applying pixels. Its Disconnect, back link and page exit release MIDI ports;
the installer also requests release with a same-origin BroadcastChannel.

This requires a computer to supply monitor video or OBS window capture, not a
USB-C video cable directly from the FM-1. It is not a UVC webcam. Physical USB
throughput, driver behavior and playback still require a real-device test.
No network or PCM buffer is reduced. The firmware remains in its original
flash slot. Reducing transparent font padding from two pixels to one saves
flash without altering a visible glyph pixel or its advance: the padding test
checks every printable glyph in all three used font faces.

Tests verify live audio with a ready sample, monitor silence, audible sample
playback and successful capture while muted, restoring the live stream on exit,
held stutter timing and BPM on player/ENV, and existing sampler controls. The
USB stream is decoded by the actual viewer decoder and compared with all
57,600 LCD pixels on player, sampler and effects pages, including queue-full
retries, memory refusal and heartbeat timeout. Viewer tests cover subscription,
rendering and releasing native ports for installation. Streaming crypto and
scheduler, updater recovery and real-loader flash safety regressions pass.
Build checks: image 578560 B; RAM .data+.bss 130656 B of 131072; pool 308884 B of 311296. Hardware behavior is not yet confirmed.
