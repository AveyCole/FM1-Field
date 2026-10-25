# Source provenance

FM1 Field is derived from Charles Vestal's fm1-radio (MPL-2.0).

Vendored sources are included directly so a normal clone contains the build platform:

- fm1-radio: https://github.com/charlesvestal/fm1-radio at `c9113b5eceb9d6c295e32c86e5d769ed8efa67d6`
- fm1-base: https://github.com/charlesvestal/fm1-base at `aa2b3bc8f7900cd1820947cb93ca0e8ecc4189e7`
- felucca-bsp: https://github.com/hugelton/felucca-bsp at `d6b8f771795be04d77e15d72a79f474a723796d7`

FM1 Field's additions include Radio Aporee playback, the static globe, country/state
browsing, the compressed region catalogue, buffering changes, and the forest/cream/orange UI.
`APOREE.md` documents these changes and validation. The on-device and USB identity
in the published alpha remains APOREE / APOREE FM-1.

Natural Earth administrative boundaries are public domain. `geography/report.json`
records the source fingerprint and coverage. Aporee recording metadata and audio
remain the work of their respective contributors; author and licence information
is shown on the device. No audio recordings are distributed here.

All upstream source copyright notices and licence texts are retained. See
`LICENSE`, `LICENSING.md`, `LICENSES/`, and third-party notices.
