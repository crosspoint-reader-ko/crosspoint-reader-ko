#!/usr/bin/env python3
"""Build a StarDict dictionary for the reader from the 표준국어대사전 bulk XML.

Input: the XML ZIP from stdict.korean.go.kr ("내 정보 관리" -> "사전 내려받기" -> "전체 내려받기"),
its extracted folder, or the stdict/ folder of github.com/spellcheck-ko/korean-dict-nikl.

Output: <out>/<name>.ifo, .idx, .dict.dz (or .dict with --no-compress) and LICENSE.txt. Copy the
folder to /dictionaries/ on the SD card and pick it in the reader's dictionary setting.

What goes in:
  - Single words only (word_unit 단어). Phrases, idioms and proverbs cannot be selected as one
    word on the page. Affixes and endings (-ㄴ가, 개-) are left out for the same reason.
  - Headwords without the dictionary's markup: homograph numbers (사과01 -> 사과), and the - and ^
    that mark word parts and optional spaces (공부-하다 -> 공부하다).
  - Every homograph and sense of a headword in one entry: part of speech, Hanja and the
    definitions. Examples are left out: those quoted from books and newspapers are not under
    the dictionary's open license, and the entry stays short on a small screen.

Data: 국립국어원 표준국어대사전, CC BY-SA 2.0 KR. The built dictionary carries the same license.

Usage: python3 scripts/build_korean_dictionary.py <zip|folder> <out_dir> [--name stdict]
"""

import argparse
import datetime
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile
import zlib

DICTZIP_CHUNK = 58315  # dictzip's standard chunk length
HOMOGRAPH_NUMBER = re.compile(r"\d+$")


def xml_sources(path):
    """Yield (name, bytes) for every .xml in a zip or a folder, in name order."""
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as z:
            for name in sorted(n for n in z.namelist() if n.lower().endswith(".xml")):
                yield name, z.read(name)
        return
    for name in sorted(os.listdir(path)):
        full = os.path.join(path, name)
        if name.lower().endswith(".xml"):
            with open(full, "rb") as f:
                yield name, f.read()
        elif zipfile.is_zipfile(full):
            yield from xml_sources(full)


def clean_headword(word):
    """사과01 -> 사과, 공부-하다 -> 공부하다. None for affixes and anything with no Hangul."""
    word = word.strip()
    if word.startswith("-") or HOMOGRAPH_NUMBER.sub("", word).endswith("-"):
        return None, 0
    number = HOMOGRAPH_NUMBER.search(word)
    homograph = int(number.group()) if number else 0
    word = HOMOGRAPH_NUMBER.sub("", word).replace("-", "").replace("^", "")
    if not word or " " in word or not any("가" <= ch <= "힣" for ch in word):
        return None, 0
    return word, homograph


def text_of(element):
    return re.sub(r"\s+", " ", (element.text or "")).strip() if element is not None else ""


def entry_block(item):
    """One homograph: '「명사」 (沙果)' then numbered senses. Returns (headword, homograph, text)."""
    info = item.find("word_info")
    if info is None or text_of(info.find("word_unit")) != "단어":
        return None
    headword, homograph = clean_headword(text_of(info.find("word")))
    if not headword:
        return None

    hanja = [
        text_of(o.find("original_language"))
        for o in info.findall("original_language_info")
        if text_of(o.find("language_type")) == "한자" and text_of(o.find("original_language"))
    ]
    lines = []
    for pos_info in info.findall("pos_info"):
        pos = text_of(pos_info.find("pos"))
        senses = [
            text_of(s.find("definition"))
            for pattern in pos_info.findall("comm_pattern_info")
            for s in pattern.findall("sense_info")
            if text_of(s.find("definition"))
        ]
        if not senses:
            continue
        head = f"「{pos}」" if pos and pos != "품사 없음" else ""
        if hanja and not lines:
            head = f"{head} ({'/'.join(hanja)})".strip()
        if head:
            lines.append(head)
        if len(senses) == 1:
            lines.append(senses[0])
        else:
            lines.extend(f"{i}. {d}" for i, d in enumerate(senses, 1))
    if not lines:
        return None
    return headword, homograph, "\n".join(lines)


def sort_key(word):
    """StarDict's index order: ASCII case-insensitive first, then bytewise (as the reader compares)."""
    raw = word.encode("utf-8")
    return raw.lower(), raw


def write_dictzip(data, path):
    """gzip with dictzip's RA chunk table, so the reader can inflate one entry without the rest."""
    chunks = [data[i : i + DICTZIP_CHUNK] for i in range(0, len(data), DICTZIP_CHUNK)] or [b""]
    deflate = zlib.compressobj(9, zlib.DEFLATED, -zlib.MAX_WBITS)
    compressed = []
    for i, chunk in enumerate(chunks):
        out = deflate.compress(chunk)
        out += deflate.flush(zlib.Z_FINISH if i == len(chunks) - 1 else zlib.Z_FULL_FLUSH)
        if len(out) > 0xFFFF:
            raise SystemExit("dictzip chunk does not compress below 64 KB")
        compressed.append(out)
    ra = struct.pack("<HHH", 1, DICTZIP_CHUNK, len(chunks)) + b"".join(struct.pack("<H", len(c)) for c in compressed)
    extra = b"RA" + struct.pack("<H", len(ra)) + ra
    if len(extra) > 0xFFFF:
        raise SystemExit("dictionary too large for one dictzip header")
    header = b"\x1f\x8b\x08\x04" + struct.pack("<I", 0) + b"\x02\x03" + struct.pack("<H", len(extra)) + extra
    trailer = struct.pack("<II", zlib.crc32(data) & 0xFFFFFFFF, len(data) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(header)
        for c in compressed:
            f.write(c)
        f.write(trailer)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("source", help="stdict XML ZIP, or a folder of its XML files")
    parser.add_argument("out_dir", help="output folder (copied to /dictionaries/ on the SD card)")
    parser.add_argument("--name", default="stdict", help="file stem (default: stdict)")
    parser.add_argument("--no-compress", action="store_true", help="write .dict instead of .dict.dz")
    args = parser.parse_args()

    blocks = {}  # headword -> [(homograph, text)]
    items = 0
    for name, data in xml_sources(args.source):
        for item in ET.fromstring(data).iter("item"):
            items += 1
            block = entry_block(item)
            if block:
                headword, homograph, text = block
                blocks.setdefault(headword, []).append((homograph, text))
        print(f"  {name}: {len(blocks):,} headwords so far", file=sys.stderr)
    if not blocks:
        raise SystemExit("no entries found: is this the 표준국어대사전 XML?")

    words = sorted(blocks, key=sort_key)
    body = bytearray()
    index = bytearray()
    for word in words:
        homographs = sorted(blocks[word], key=lambda b: b[0])
        if len(homographs) == 1:
            text = homographs[0][1]
        else:
            text = "\n\n".join(f"[{i}] {t}" for i, (_, t) in enumerate(homographs, 1))
        encoded = text.encode("utf-8")
        index += word.encode("utf-8") + b"\0" + struct.pack(">II", len(body), len(encoded))
        body += encoded
    if len(body) > 0xFFFFFFFF:
        raise SystemExit("definitions exceed 4 GB (32-bit index offsets)")

    os.makedirs(args.out_dir, exist_ok=True)
    stem = os.path.join(args.out_dir, args.name)
    with open(stem + ".idx", "wb") as f:
        f.write(index)
    if args.no_compress:
        with open(stem + ".dict", "wb") as f:
            f.write(body)
    else:
        write_dictzip(bytes(body), stem + ".dict.dz")
    today = datetime.date.today().isoformat()
    with open(stem + ".ifo", "w", encoding="utf-8") as f:
        f.write(
            "StarDict's dict ifo file\nversion=2.4.2\n"
            f"bookname=표준국어대사전\nwordcount={len(words)}\nidxfilesize={len(index)}\n"
            "sametypesequence=m\n"
            "author=국립국어원\n"
            f"description=국립국어원 표준국어대사전(CC BY-SA 2.0 KR)을 {today}에 StarDict로 변환. 용례 제외.\n"
            f"date={today}\n"
        )
    with open(os.path.join(args.out_dir, "LICENSE.txt"), "w", encoding="utf-8") as f:
        f.write(
            "출처: 국립국어원 표준국어대사전 (https://stdict.korean.go.kr)\n"
            "이 사전 파일은 크리에이티브 커먼즈 저작자표시-동일조건변경허락 2.0 대한민국 라이선스를 따릅니다.\n"
            "https://creativecommons.org/licenses/by-sa/2.0/kr/\n"
            "변경 사항: 단어 표제어의 품사, 한자, 뜻풀이만 남기고 용례와 멀티미디어를 뺐으며, "
            "표제어 표기(어깨번호, -, ^)를 정리하고 동음이의어를 한 항목으로 합쳤습니다.\n"
        )
    print(f"{items:,} items -> {len(words):,} headwords, definitions {len(body) / 1e6:.1f} MB", file=sys.stderr)


if __name__ == "__main__":
    main()
