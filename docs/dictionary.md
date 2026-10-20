# Dictionary

Look up words while reading an EPUB using an offline StarDict dictionary stored on the SD card.

## Supported Format

The reader supports **StarDict** dictionaries. When searching for dictionaries online, look for "StarDict format" or files with `.dict`, `.idx`, and `.ifo` extensions.

A dictionary folder must contain:

- `.idx` — word index (required, **must be uncompressed** — a `.idx.gz` will not work; decompress it on your computer with `gzip -d` first)
- `.dict` or `.dict.dz` — definition data (`.dict.dz` is supported as-is; entries are decompressed on the fly during lookup)
- `.syn` — synonym index (optional; maps alternate spellings and irregular forms to their headword)
- `.ifo` — metadata (optional)

Not supported: dictionaries with 64-bit index offsets (`idxoffsetbits=64` in the `.ifo` — rare, and rejected with an error).

## Setting Up a Dictionary

1. Copy your dictionary folder(s) to `/dictionaries/` on the SD card — one dictionary per folder, e.g. `/dictionaries/webster/webster.idx` + `webster.dict.dz`. A hidden `/.dictionaries/` folder (dot-prefixed) works the same way, for keeping it out of the file browser.
2. Open **Settings → Reader → Dictionary** on the device.
3. Select a dictionary from the list, or **None** to disable lookups.

The Dictionary setting only appears when at least one usable dictionary folder exists. Folders containing more than one dictionary (multiple `.idx` stems) are skipped as ambiguous.

## Looking Up a Word

Two ways to start a lookup while reading:

- Open the reader menu (**Confirm**) and choose **Look Up**.
- Or set **Settings → Controls → Long-press Menu** to "Dictionary", then hold **Confirm** (~0.4s) on the reading page.

One word on the page becomes highlighted:

1. Use **Left/Right** to move between words in reading order, and the side **Up/Down** buttons to jump between lines.
2. Press **Confirm** to look up the highlighted word.
3. Press **Back** to return to the reader.

On the very first lookup with a dictionary (and again whenever the `.idx` or `.syn` source file changes), the reader shows *"Indexing dictionary…"* while it builds small sidecar files next to them — a `.qidx` for the word index, and a `.sidx` when a `.syn` synonym file is present. Each sidecar is rebuilt independently, only when its own source changes. This takes a few seconds for large dictionaries and makes all subsequent lookups fast. The sidecars can be deleted safely at any time — they will simply be rebuilt.

### How Lookup Works

1. **Direct match** — the word is found as-is (case-insensitive) in the dictionary index. Surrounding punctuation is ignored.
2. **Synonyms** — on a miss, if the dictionary ships a `.syn` file, alternate spellings and irregular forms recorded there are resolved to their headword (e.g. `oxen` → `ox`, `colour` → `color`). This step is skipped if the `.sidx` sidecar could not be built (e.g. transient low memory during indexing); the dictionary otherwise stays usable, and the build is retried the next time it is opened.
3. **Stemming** — still no match: common English word forms are retried automatically: possessives and plurals (`dogs` → `dog`, `stories` → `story`) and verb endings (`walked` → `walk`, `running` → `run`, `making` → `make`).
4. **Korean forms** — still no match and the word has Hangul: the reader tries the forms a Korean dictionary lists. Particles come off (`사랑을` → `사랑`, `집에` → `집`), verbs and adjectives go to their 다 form (`먹었다` → `먹다`, `공부했다` → `공부하다`, `간다` → `가다`), and the common irregulars are restored (`아름다운` → `아름답다`, `몰라` → `모르다`, `파란` → `파랗다`, `드문` → `드물다`). Longer stems are tried first, at most 24 forms per word. These are rule-based guesses, so a form can land on another word (`물을` → `물다`); the definition screen shows the headword that matched.
5. **Not found** — a short popup appears and you return to word selection.

## The Definition Screen

When a word is found, the definition screen shows the matched headword at the top and the definition text below, with a page counter for long definitions.

HTML dictionaries that declare `sametypesequence=h` use the EPUB text-layout engine for semantic formatting such as headings, bold, italics, lists, and line breaks. Images and CSS styling are ignored. Definitions that are too large or cannot be laid out within the available memory fall back to plain text.

- **Left/Right** or side **Up/Down** — previous / next page
- **Back** — return to word selection



## Korean dictionary (표준국어대사전)

Every Korean release since 1.6.5-ko.10 carries `stdict-표준국어대사전.zip`: the National Institute of Korean Language's Standard Korean Language Dictionary (국립국어원 표준국어대사전), converted for the reader. It holds 287,761 single-word headwords with part of speech, Hanja and definitions (about 21 MB).

1. Unzip it and copy the `stdict` folder into `/dictionaries/`, so the SD card has `/dictionaries/stdict/stdict.ifo`.
2. Select **표준국어대사전** in **Settings → Reader → Dictionary**. The first lookup indexes it once.

The data is © 국립국어원 under [CC BY-SA 2.0 KR](https://creativecommons.org/licenses/by-sa/2.0/kr/); `LICENSE.txt` in the folder says so. Anything built from it carries the same license.

### Building it yourself

`scripts/build_korean_dictionary.py` (Python 3, standard library only) builds the dictionary from the dictionary's bulk XML:

1. Get the XML: sign in at [stdict.korean.go.kr](https://stdict.korean.go.kr), open **내 정보 관리 → 사전 내려받기**, and press **전체 내려받기** for the XML ZIP. The [`spellcheck-ko/korean-dict-nikl`](https://github.com/spellcheck-ko/korean-dict-nikl) repository keeps a copy of the same files in its `stdict/` folder.
2. Run the script on the ZIP or the folder of XML files:

   ```bash
   python3 scripts/build_korean_dictionary.py stdict.zip out/stdict
   ```

3. Copy `out/stdict` to `/dictionaries/stdict` on the SD card.

What the script keeps and changes:

- Single words only (`word_unit` 단어). Phrases, idioms and proverbs cannot be selected as one word on the page, and affixes and endings (`-ㄴ가`, `개-`) are left out.
- Headword markup is removed: homograph numbers (`사과01` → `사과`) and the `-` and `^` that mark word parts and optional spaces (`공부-하다` → `공부하다`).
- All homographs and senses of a headword become one entry, numbered `[1]`, `[2]`, … in the dictionary's order, each with its part of speech (`「명사」`) and Hanja.
- Examples are dropped: those quoted from books and newspapers are not under the open license, and short entries suit the screen.
- The output is `.ifo`, `.idx` and a `.dict.dz` with standard 58,315-byte dictzip chunks (`--no-compress` writes a plain `.dict`). Takes about 20 seconds.

The same approach works for 우리말샘 (opendict.korean.go.kr), whose bulk XML has a similar `<item>` layout, but it has 1.2 million headwords, most of them technical terms; the script reads the 표준국어대사전 layout only.

## Where to find dictionaries

> credit to https://github.com/koreader/koreader/wiki/Dictionary-support for the list.

- The [reader.dict](https://www.reader-dict.com) (ex "BoboTiG/ebook-reader-dict") project provides StarDict version of daily dumps of [Wiktionary](https://www.wiktionary.org/) monolingual dictionaries for a variety of languages. It also provides [non-free multilingual](https://www.reader-dict.com) dictionaries.
- The [WikDict](https://www.wikdict.com) project provides free bilingual dictionaries based on [Wiktionary](https://www.wiktionary.org/) for a lot of language pairs. StarDict versions can be [downloaded from here](https://download.wikdict.com/dictionaries/stardict/).
- The [`Vuizur/Wiktionary-Dictionaries`](https://github.com/Vuizur/Wiktionary-Dictionaries) repository contains dictionaries based on [Wiktionary](https://www.wiktionary.org/) from many languages to English, including English-English.
- The [DictInfo](https://www.dictinfo.com/) website provides outdated monolingual dictionaries based on [Wiktionary](https://www.wiktionary.org/).
- The [Firedict site](https://tuxor1337.frama.io/firedict/dictionaries.html) contains a list of freely available dictionaries.
- [wiktionary_stardict](https://xxyzz.github.io/wiktionary_stardict/): update monthly.
- [Fictionaries](https://fictionary.gumroad.com/) provides dictionaries for various speculative fiction books and series.
- [World Factbooks Archive](https://github.com/MilkMp/CIA-World-Factbooks-Archive-1990-2025) provides 36 years of CIA's World Factbook dictionaries in StarDict format.
- [StarDict-Hebrew](https://github.com/Uri-Tauber/StarDict-Hebrew) Hebrew-English StarDict versions of Babylon dictionaries.
