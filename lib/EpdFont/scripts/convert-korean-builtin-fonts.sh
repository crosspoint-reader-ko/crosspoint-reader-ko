#!/bin/bash
# Regenerate the Korean built-in font headers (upstream's convert-builtin-fonts.sh
# covers NotoSerif/NotoSans/Ubuntu; this covers the one this fork actually links,
# see builtinFonts/all.h).
#
# Requires freetype-py:  pip install freetype-py
#
# Usage:  lib/EpdFont/scripts/convert-korean-builtin-fonts.sh

set -e

cd "$(dirname "$0")"

PY="${PYTHON:-python3}"

# Pretendard 10pt — the UI font and the default EPUB reader font. Pretendard first,
# KoPub Batang Light for every code point Pretendard lacks (the 4,620 Hanja and some
# symbols): all 11,172 Hangul syllables plus the ranges the KoPub built-in carried up
# to 1.6.5-ko.3. Larger reading sizes come from SD-card fonts, e.g. the release asset
# KoPubBatang-14.epdfont (.github/workflows/release.yml).
READER_INTERVALS=(
  --additional-intervals 0x1100,0x11FF  # Hangul Jamo
  --additional-intervals 0x3000,0x303F  # CJK symbols and punctuation
  --additional-intervals 0x3130,0x318F  # Hangul compatibility Jamo
  --additional-intervals 0x4E00,0x9FFF  # CJK unified ideographs (Hanja)
  --additional-intervals 0xAC00,0xD7A3  # Hangul syllables
)

echo "Generating pretendard_10_regular.h ..."
$PY fontconvert.py pretendard_10_regular 10 \
  ../builtinFonts/source/Pretendard/Pretendard-Regular.ttf \
  "../builtinFonts/source/KoPub-Batang/KoPub Batang Light.ttf" \
  --2bit "${READER_INTERVALS[@]}" > ../builtinFonts/pretendard_10_regular.h

# fontconvert.py records its whole argv in the header comment. For Pretendard that
# is long once the intervals expand, so the header gets a comment pointing at this
# script instead.
$PY - <<'PYEOF'
import re

NOTES = {
    "../builtinFonts/pretendard_10_regular.h":
        " * Command used: lib/EpdFont/scripts/convert-korean-builtin-fonts.sh\n"
        " * Pretendard first, KoPub Batang Light for what Pretendard lacks (Hanja): every code point\n"
        " * the KoPub built-in carried up to 1.6.5-ko.3.",
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
