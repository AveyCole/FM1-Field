# FIELD — experimental 0.1.12-alpha

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
  python3 base/tools/build.py app --release 0.1.12-alpha
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

## River filter and selectable slice count (0.1.9)

LFO opens an independent stereo state-variable filter after the mixed live
recording and sampler voice. It activates on first opening and remains enabled
across navigation; ALGORITHM left bypasses, right enables. No sample/FX history
is borrowed, and entering it preserves the clip. Capture precedes the filter.
Settings are temporary and the filter starts bypassed after boot.

K1 cutoff spans 80–6,000 Hz, K2 resonance, K3 triangle LFO depth up to ±3 octaves,
and K4/SELECT speed. A zero depth is a manual filter. SEL cycles low-pass,
high-pass, band-pass; EDIT toggles free 0.05–8 Hz and synced periods of 8, 4, 2,
1, 1/2 or 1/4 beats. FX tapping on the page enables sync and resets LFO phase;
all tempo taps update the shared beat. The page shows BPM, filter type, bypass,
free/sync, a static original river/forest scene and four compact knob readouts.
The sampler's artwork stays intact; shared drawing code avoids duplicate assets.
HOME/LFO returns to the player without bypassing. The sample keyboard, region
skip on OCT, favorites on REC/SEQ, ARP and ENV remain accessible.

Coefficients update every 64 samples; cutoff/resonance are smoothed there and
bypass crossfades over 220 audio samples. The cutoff ceiling and damping range
are bounded inside the Chamberlin stability region. The mixed output is bounded
before conversion to the audio word. No PCM or network buffer is reduced.

In ARP Slice, PRESETS selects 4, 8, 16 or 27 contiguous equal portions of the
trimmed clip. Fewer sections give longer samples. The header shows the count;
keys beyond the count repeat the sequence. Count changes stop the current voice
and preserve capture, trim and tuning. Default is 27 after boot. Other sampler
modes retain their behavior, and PRESETS has no new meaning outside Slice.

Tests cover LP/HP frequency separation, band-pass response, independent stereo
state, exact bypass, modulation/tempo scaling and 96 corner combinations of
type, cutoff, resonance, depth, sync and speed for finite bounded state/output.
Sampler tests verify every slice boundary covers the trimmed range exactly and
all 27 keys map within bounds at each count. Host tests cover page controls,
preserved samples, navigation, bypass, 80 BPM sync, all slice counts and wraps.
The actual viewer decoder reconstructs every filter-screen pixel, alongside
player/sampler/FX screens. Existing sampler, monitor, favorites, USB updater,
stream crypto and scheduler checks pass. Loader simulation checks the exact
package, writes only permitted flash windows, preserves the flash head and
refuses damage. Build checks: image 581328 B; RAM .data+.bss 130688 B of 131072; pool 308884 B of 311296.
Physical-device audio, CPU timing and USB behavior still require testing.

## One persistent sample slot (0.1.10)

SAVE in ARP stores the current ADPCM clip and trim start/end, level, tuning,
octave, playback mode and slice count. During capture it first finishes the
partial recording; an empty/too-short clip reports Capture a sample first.
The display reports Sample saved only after verified programming and commit.
The saved clip is restored at boot and when entering ARP with an empty clip,
including after ENV used its RAM for live FX. FX is disabled before restoration
touches that shared memory. New unsaved captures leave the saved slot unchanged.
The stored clip is dry source audio, as before; filter/monitor settings are not
part of it. A save replaces one logical slot and briefly interrupts sound.

The slot uses two 16 KiB banks at 0x97000 and 0x9B000, ending at 0x9F000, in
the unused former Radio Garden table space. FIELD's region catalogue is compiled
into its code. This does not overlap settings, favorites or OTA staging. Only
the inactive bank is erased, followed by payload programming/readback verification
and a final CRC-protected header commit. Both header and audio CRCs, lengths,
all edit bounds and ADPCM index bounds are validated on load. The newest valid
sequence wins, with wrap handling and fallback to the earlier bank. A failed or
interrupted replacement retains the previous version. Normal FIELD loader
installs exclude this entire data window, including bytes resembling AT records.
Other firmware and explicit user-data erases may use or clear this space.

No new audio bank or permanent RAM reserve is allocated. To fit the new code in
the original flash slot, bitmap font data omits transparent rows at the bottom
of each glyph. Per-glyph row counts retain the original advance, line height,
and every visible pixel; custom fonts without row counts keep their old behavior.
All three used font faces pass exact raster/advance checks, and the filter PNG
is byte-identical before/after this font packing change.

Tests cover exact audio and settings restore, replacement, unsaved captures,
reboot, restoring after FX, replay, erase/payload cuts, every incomplete header
length, damaged-newest fallback, invalid-bank bounds and sequence wrap. Loader
tests seed both sample banks, including AT-like bytes, and verify preservation
through install and reinstall. Existing sampler, monitor, filter, favorites,
regions, pixel mirror, USB recovery, streaming crypto and scheduler checks pass.
Build checks: image 578912 B; RAM .data+.bss 130688 B of 131072; pool 308884 B of 311296. Physical saving/reboot/playback still needs a device test.

## Echo + Room, home FX hold and preset BPM (0.1.11)

Hold FX on HOME/the regional player for 450 ms to open the independent combo
page. A short press remains a tempo tap, registered on release using its press
timestamp, so different press lengths do not skew the beat. A hold is consumed
without a tap or release action. Leaving a player page cancels a pending hold.
Other pages still tap on FX press. PRESETS on ENV's FX page and Echo + Room
adjusts shared tempo from 40 to 240 BPM and clears old tap history.

K1 delay time, K2 feedback, K3 room decay and K4/SELECT mix accompany the same
static river artwork and compact readouts. EDIT toggles free 20–360 ms / synced
1/8, 1/4, 1/2 and 1 beat; values exceeding the buffer's 360 ms working range are
clamped and explicitly marked SYNC / LIMIT. Actual milliseconds and BPM are
displayed. ALGORITHM bypasses/enables with a smooth wet-gain ramp. HOME leaves
the combo running; PLAY stops/restarts the source with natural decaying tails.
LFO can process the mixed output. OCT, REC/SEQ and GLO retain their functions.

This uses the existing 6,144-int16 sample/FX bank: 4,096 echo samples, four
damped combs of 293/337/421/463 samples and independent stereo allpasses of
113/173 samples, totaling 5,896. The wet path runs at 11.025 kHz with input
averaging and output smoothing; live dry stereo remains at 44.1 kHz. Echo time
glides, echo feedback is bounded at 0.82, room feedback at 0.88, and writes and
output are bounded. There are no new audio allocations or reduced network/PCM
buffers. The combo, held-key FX and sampler are exclusive: entry clears an
unsaved clip, ARP restores a saved clip, and entering ARP or ENV clears the tail.
The saved flash slot is unaffected.

Tests cover short, long and threshold holds, no tempo change on hold/release,
accurate short-tap spacing, PRESETS BPM in both pages, parameter controls,
bypass, navigation, source skip, sample preservation and memory ownership.
DSP checks verify the quarter-second echo, stereo diffusion, decaying tails,
16 control corners, exact bypass, buffer guards and tempo/limit behavior.
Host audio confirms the tail continues with the source stopped and reaches
silence. The viewer reconstructs every combo-screen pixel. Existing sampler,
saved clip, filter, favorites, regions, USB updater, crypto and scheduler checks
pass; the real loader simulation preserves the saved-sample banks. Build:
image 581152 B; RAM .data+.bss 130752 B of 131072; pool 308884 B of 311296. Physical audio quality, timing and USB behavior still need device tests.


## Restart when changing firmware (0.1.11)

FIELD now stops audio DMA before detaching USB and entering the target update
loader. Its handoff uses the existing P33 full-chip reset instead of a CPU-only
reset, after the update record has been written and verified in flash. FIELD's
own loader also uses a full-chip reset after completing an installation. This
addresses retained peripheral state when switching firmware; a physical-device
test is still needed to confirm the reported black-screen restart failure.
Other firmware packages supply their own final loader/reset code.

Software update-session checks pass for FIELD and the official stock package,
including staged-loader integrity, flash/RAM update-record agreement, host
confirmation and failed-transfer cleanup. FIELD loader simulation also passes
install/reinstall and saved-sample preservation checks.

The first install from an older FIELD version still begins with that version's
old handoff code and may need a power-cycle if it stalls. Subsequent updates
from 0.1.11 use the new handoff. Never interrupt an active flash write.


## Simultaneous sampler and Echo + Room (0.1.12)

Hold FX in ARP, HOME or the regional player for 450 ms to open Echo + Room.
Short presses retain tap tempo. ARP returns to the sampler editor without
disabling the combo. Sample data, trim, slice settings and playback survive;
the keyboard also plays the sample on the combo page. Latched loops continue,
and held-loop key releases still stop the voice. The sampler's live-monitor
mute remains in effect while editing Echo + Room; leaving that workspace
restores the live monitor as before. REC retains its existing page behavior.

The wet processor now runs after the live-monitor gain and sampler mix, before
the LFO filter. It owns a separate on-demand 12,288-byte heap bank, leaving the
sample's ADPCM bank untouched. Entry requires room for that bank plus a 24 KiB
heap reserve, checks allocation failure and leaves audio/sample/page intact on
failure. ENV frees the combo bank and retains its existing exclusive held-key
FX behavior. Streaming's 80 KiB compressed reserve and 6,912-frame PCM buffer
are unchanged; no extra static audio bank and no allocation in the renderer.

The new host scenario tests unsaved-clip and latched-voice preservation, the
450 ms boundary, successful and low-memory entry, keyboard latch stop from
the FX page, return to ARP, slice editing and saving after using the combo.
With the live source muted, the captured sample produces a -38.9 dBFS FX tail
after its voice stops, then decays to silence. Existing DSP, capture, save,
filter, tempo and all-pixel display tests pass. Hardware build checks pass:
image 581328 B; RAM .data+.bss 130784 B of 131072; pool 308884 B of 311296.
Physical-device audio and available WiFi heap still need user testing.

Stock restore remains under investigation: the reported Windows/Chrome session
exposed USB-Midi input without a matching output after loader handoff, and no
stock write began. This release does not claim to fix that separate issue.
