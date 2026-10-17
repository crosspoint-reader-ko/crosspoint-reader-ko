# Firmware Size Budget

The released `gh_release` firmware must install onto a locked X4 in a single OTA
step. The OTA path writes the new image into the inactive app partition of the
device's current (stock Crosspoint) layout, so the firmware has to fit that
partition.

## The constraint

Stock Crosspoint 1.3.0 app partition (`upstream/release/1.3.0:partitions.csv`):

```
app0, app, ota_0, 0x10000,  0x640000   ; 6,553,600 bytes = 6.25 MB
app1, app, ota_1, 0x650000, 0x640000
```

`gh_release` `firmware.bin` must stay at or below **6,553,600 bytes**. If it is
larger, the OTA unlocker cannot flash KO directly and the user is forced through
the two-step path (OTA to Crosspoint, then SD-card flash to KO — see issue #15).

This repo's own `partitions.csv` keeps the larger `0x6A0000` (6.625 MB) app
partitions for the full-flash / SD-update path; that is independent of the OTA
size limit above.

## What gets us under the limit

Korean fonts dominate the image. Since 1.6.5-ko.4 the built-in reader font is
Pretendard 14 (full Hangul, no Hanja, 1.8 MB). KoPub Batang 14 (3.13 MB with its
4,620 Hanja) was the reader font until 1.6.5-ko.3; it moved out of the image to make
room for the Bluetooth page turner (BLE controller and NimBLE, ~400 KB) and is
published with every release as `KoPubBatang-14.epdfont` for SD-card use. Pretendard
10 (the UI font) stays cut to KS X 1001, because a UI font only needs the syllables
that appear in menus, titles and filenames.

| Cut | Where | Saving |
|-----|-------|--------|
| KoPub Batang reader font replaced by Pretendard 14 | `convert-korean-builtin-fonts.sh`, release `.epdfont` | **~1.2 MB** |
| Pretendard UI font restricted to KS X 1001 Hangul | `convert-korean-builtin-fonts.sh` | **828 KB** |
| Drop Latin/Cyrillic hyphenation tries | `-DCP_HYPHENATION_LANGS=0` | ~310 KB |
| Ship only Korean + English UI | only `english.yaml` + `korean.yaml` | ~184 KB |
| Omit serial logging | no `-DENABLE_SERIAL_LOG` (`gh_release`) | ~27 KB |
| Drop upstream's `.cpfont` SD font engine | deleted in the 1.5.0 merge | ~7 KB |

### The Pretendard cut

Pretendard 10pt carried all 11,172 precomposed Hangul syllables for 1,070 KB.
Restricting it to the 2,350 syllables of KS X 1001 (the classic wansung set)
leaves 222 KB of syllables plus 20 KB of interval table -- a net 828 KB. The
syllables are scattered through U+AC00..U+D7A3, so they collapse into 1,613 short
runs rather than one range; `ks_x_1001_intervals.py` generates them.

KS X 1001 covers modern Korean orthography. What drops out is archaic and
onomatopoeic combinations (`뷁`, `똠`), which effectively never appear in UI
labels but *can* appear in book titles and filenames. Those glyphs do not render
unless the user selects an SD-card system font -- there is no built-in fallback
for them, which is a deliberate trade (see the release notes for 1.5.0-ko.1). Full-coverage
Pretendard 10 `.epdfont` files were attached to the 1.5.0-ko.1 and 1.5.0-ko.2 releases for users who
want them back:

| File | Size |
|------|------|
| `pretendard_10_regular.epdfont` | 1.06 MB |
| `pretendard_10_bold.epdfont` | 1.10 MB |

Korean text is not Liang-hyphenated, so dropping the hyphenation tries has no
effect on Korean reading. `CP_HYPHENATION_LANGS` defaults to `1` (all languages)
for upstream parity; individual languages toggle with
`CP_HYPHEN_LANG_<EN|FR|DE|RU|ES|IT|UK|SV|PL|FI>`. See
`lib/Epub/Epub/hyphenation/LanguageRegistry.cpp`.

## Measured

| Build | `firmware.bin` | vs 6.25 MB |
|-------|---------------|------------|
| 1.3.0-ko.1 | 6,461,088 | 92,512 under |
| 1.5.0-ko.0 (upstream 1.5.0 merged) | 6,869,392 | **315,792 over** |
| 1.5.0-ko.1 (Pretendard cut to KS X 1001) | 6,041,152 | 512,448 under |
| 1.6.5-ko.3 | 6,183,456 | 370,144 under |
| 1.6.5-ko.3 + Bluetooth page turner, KoPub built in | 6,573,152 | **19,552 over** |
| 1.6.5-ko.4 (Pretendard 14 reader font) | 5,367,312 | 1,186,288 under |

1.5.0-ko.0 shipped over the limit. That does not just force the two-step install
for new users: **it breaks OTA for existing ones.** OTA and the SD-card update
both write only the app partition and never rewrite the partition table, so a
device that reached KO through OTA still has the stock 6.25 MB table and rejects
an oversized image with `TOO_LARGE` (see `FirmwareFlasher.cpp`). Only a full
flash changes the table, and some units have USB disabled, leaving them with no
route at all. Hence 1.5.0-ko.1.

Headroom is now ~1.18 MB on X3/X4 and over 1.08 MB on every S3 board. Before adding flash-resident data, rebuild `gh_release`
and check `firmware.bin` against 6,553,600 bytes. Remaining levers, largest
first: dropping the Pretendard 10 UI font and drawing the UI with Pretendard 14
(~282 KB), excluding
X3-only code (tilt/gyro/NTP, currently always linked), and trimming the wolfSSL
cipher set (~195 KB total, most of it needed for HTTPS).
