#!/bin/bash
# Regenerate the Korean built-in font headers (upstream's convert-builtin-fonts.sh
# covers NotoSerif/NotoSans/Ubuntu; this covers the two this fork actually links,
# see builtinFonts/all.h).
#
# Requires freetype-py:  pip install freetype-py
#
# Usage:  lib/EpdFont/scripts/convert-korean-builtin-fonts.sh

set -e

cd "$(dirname "$0")"

PY="${PYTHON:-python3}"

# Pretendard 14pt — the EPUB reader font. Full Hangul syllable block; Pretendard
# ships no Hanja, so books with Hanja need an SD font. KoPub Batang (the reader
# font up to 1.6.5-ko.3) is published as a release .epdfont for that; see
# .github/workflows/release.yml.
READER_INTERVALS=(
  --additional-intervals 0x1100,0x11FF  # Hangul Jamo
  --additional-intervals 0x3000,0x303F  # CJK symbols and punctuation
  --additional-intervals 0x3130,0x318F  # Hangul compatibility Jamo
  --additional-intervals 0xAC00,0xD7A3  # Hangul syllables
)

echo "Generating pretendard_14_regular.h ..."
$PY fontconvert.py pretendard_14_regular 14 \
  ../builtinFonts/source/Pretendard/Pretendard-Regular.ttf \
  --2bit "${READER_INTERVALS[@]}" > ../builtinFonts/pretendard_14_regular.h

# Pretendard 10pt — the UI font. Restricted to the KS X 1001 Hangul set (2,350
# syllables instead of 11,172), which is what keeps firmware.bin inside the
# 6.25 MB stock OTA app partition; see docs/firmware-size-budget.md. Syllables
# outside that set do not render in the UI unless the user picks an SD-card
# system font. A full-coverage Pretendard .epdfont is published with each
# release for exactly that.
echo "Generating pretendard_10_regular.h (KS X 1001 Hangul) ..."
# Unquoted on purpose: bash word-splits the ~3,200 tokens into array elements.
# (Not mapfile -- macOS still ships bash 3.2.)
KSX_INTERVALS=($($PY ks_x_1001_intervals.py))
$PY fontconvert.py pretendard_10_regular 10 \
  ../builtinFonts/source/Pretendard/Pretendard-Regular.ttf \
  --2bit \
  --additional-intervals 0x1100,0x11FF \
  --additional-intervals 0x3130,0x318F \
  "${KSX_INTERVALS[@]}" > ../builtinFonts/pretendard_10_regular.h

# fontconvert.py records its whole argv in the header comment. For Pretendard that
# is ~56 KB once the 1,600-odd KS X 1001 intervals expand, so both headers get a
# comment pointing at this script instead.
$PY - <<'PYEOF'
import re

NOTES = {
    "../builtinFonts/pretendard_14_regular.h":
        " * Command used: lib/EpdFont/scripts/convert-korean-builtin-fonts.sh\n"
        " * Hangul coverage: full block; no Hanja (Pretendard ships none).",
    "../builtinFonts/pretendard_10_regular.h":
        " * Command used: lib/EpdFont/scripts/convert-korean-builtin-fonts.sh\n"
        " * Hangul coverage: KS X 1001 (2,350 syllables) via ks_x_1001_intervals.py,\n"
        " * not the full 11,172 — see docs/firmware-size-budget.md.",
}

for path, note in NOTES.items():
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    text = re.sub(r"^ \* Command used: .*$", note, text, count=1, flags=re.M)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
PYEOF

echo ""
echo "Done. Rebuild and confirm gh_release firmware.bin is still under 6,553,600 bytes."
