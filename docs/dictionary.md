# Dictionary

Look up words and phrases while reading an EPUB using an offline StarDict dictionary stored on the SD card.

## Supported Format

The reader supports **StarDict** dictionaries. When searching for dictionaries online, look for "StarDict format" or files with `.dict`, `.idx`, and `.ifo` extensions.

A dictionary folder must contain:

- `.idx` — word index (required, **must be uncompressed** — a `.idx.gz` will not work; decompress it on your computer with `gzip -d` first)
- `.dict` or `.dict.dz` — definition data (`.dict.dz` is supported as-is; entries are decompressed on the fly during lookup)
- `.syn` — synonym index (optional; maps alternate spellings and irregular forms to their headword)
- `.ifo` — metadata (optional)

Not supported: dictionaries with 64-bit index offsets (`idxoffsetbits=64` in the `.ifo` — rare, and rejected with an error).

> [!NOTE]  
> If you use .dict.dz and Crosspoint failed to load your dictionary with error message "Not enough memory", try unzipping it.

## Setting Up a Dictionary

1. Copy your dictionary folder(s) to `/dictionaries/` on the SD card — one dictionary per folder, e.g. `/dictionaries/webster/webster.idx` + `webster.dict.dz`. A hidden `/.dictionaries/` folder (dot-prefixed) works the same way, for keeping it out of the file browser.
2. Open **Settings → Reader → Dictionary** on the device.
3. Select a dictionary from the list, or **None** to disable lookups.

The Dictionary setting only appears when at least one usable dictionary folder exists. Folders containing more than one dictionary (multiple `.idx` stems) are skipped as ambiguous.

## Looking Up Words and Phrases

Two ways to start a lookup while reading:

- Open the reader menu (**Confirm**) and choose **Look Up**.
- Or set **Settings → Controls → Long-press Menu** to "Dictionary", then hold **Confirm** (~0.4s) on the reading page.

### Selecting Text

**On button-driven devices:**

1. A selection cursor appears on a word. Use **Left/Right** to navigate between words in reading order, and the side **Up/Down** buttons to jump between lines or flip across nearby pages.
2. Press **Confirm** (**Select**) to anchor the start of the selection.
3. **Single word:** Press **Confirm** again immediately (**Look Up**) to look up the highlighted word.
4. **Phrase / multiple words:** Use **Left/Right** or **Up/Down** to extend the highlighted selection across words, lines, or pages. Then press **Confirm** (**Look Up**).
5. Press **Back** at any time to cancel and return to reading.

**On touchscreen devices:**

- Tap or drag the selection handles across words to highlight a word or phrase, then tap **Look Up**.

On the very first lookup with a dictionary (and again whenever the `.idx` or `.syn` source file changes), the reader shows *"Indexing dictionary…"* while it builds small sidecar files next to them — a `.qidx` for the word index, and a `.sidx` when a `.syn` synonym file is present. Each sidecar is rebuilt independently, only when its own source changes. This takes a few seconds for large dictionaries and makes all subsequent lookups fast. The sidecars can be deleted safely at any time — they will simply be rebuilt.

### How Lookup Works

1. **Direct match** — the word or phrase is searched as-is (case-insensitive) in the dictionary index. Leading and trailing punctuation and whitespace are stripped automatically. Multi-word phrases match dictionary entries containing spaces (such as idioms, phrasal verbs, or compound terms). An index may list the same headword more than once (one entry per part of speech, for example); every entry it lists is shown, each labelled with its headword.
2. **Synonyms** — on a miss, if the dictionary ships a `.syn` file, alternate spellings and irregular forms recorded there are resolved to their headword (e.g. `oxen` → `ox`, `colour` → `color`). All the headwords a synonym points at are shown. This step is skipped if the `.sidx` sidecar could not be built (e.g. transient low memory during indexing); the dictionary otherwise stays usable, and the build is retried the next time it is opened.
3. **Stemming** — still no match: common English word forms are retried automatically: possessives and plurals (`dogs` → `dog`, `stories` → `story`) and verb endings (`walked` → `walk`, `running` → `run`, `making` → `make`).
4. **Korean forms** — still no match and the word has Hangul: the reader tries the forms a Korean dictionary lists. Particles come off (`사랑을` → `사랑`, `집에` → `집`), verbs and adjectives go to their 다 form (`먹었다` → `먹다`, `공부했다` → `공부하다`, `간다` → `가다`), and the common irregulars are restored (`아름다운` → `아름답다`, `몰라` → `모르다`, `파란` → `파랗다`, `드문` → `드물다`). Longer stems are tried first, at most 24 forms per word. These are rule-based guesses, so a form can land on another word (`물을` → `물다`); the definition screen shows the headword that matched.
5. **Not found** — a short popup appears. Touchscreen devices return to reading; buttons-only devices return to word selection.

## The Definition Screen

When a word is found, the definition screen shows the matched headword at the top and the definition text below, with a page counter for long definitions. A word that maps to several definitions shows them one after another, each labelled with its headword. Definitions that exceed the remaining size limit are skipped, and later definitions are still considered. If no labelled definition fits, the first definition is shown alone without an extra label.

HTML dictionaries (`sametypesequence=h`) and XDXF dictionaries (`sametypesequence=x`) use the EPUB text-layout engine for supported formatting such as headings, bold, italics, lists, and line breaks. Unrecognized tags keep their text without special formatting. Images and CSS styling are ignored. Definitions that are too large or cannot be parsed or laid out within the available memory fall back to plain text.

A readable `.ifo` file with a missing or empty `sametypesequence` enables the type stored in each entry field. HTML (`h`) and XDXF (`x`) fields use the same formatting support. Text fields are shown as text; binary fields, such as images and sounds, are skipped. Missing, empty, or unreadable `.ifo` files retain plain-text handling.

Inline entries with only empty or binary fields are skipped. If all matching entries are empty, the lookup reports no match.

Definitions are drawn in your reading font. Dictionaries may use IPA symbols (e.g. `ˈ ː ɹ ɪ ʊ`…) as pronunciation guide, which built-in reader fonts do not include, so they are shown as `�`. To see them, install an [SD card font](./sd-card-fonts.md) that covers IPA and select it as the reading font.

- **Left/Right** or side **Up/Down** — previous / next page
- **Confirm** — select words or a phrase inside the definition, then choose **Look Up**
- **Long press on text** — select the touched word on touchscreen devices when touch reader controls are enabled
- **Back during selection** — cancel selection
- **Back while reading a definition** — return to the previous definition. At the first definition, touchscreen devices return to reading; buttons-only devices return to book word selection
- **Long Back while reading a definition** — hold **Back** for at least one second, then release to close all definitions and return directly to reading
- **Long press the header Back arrow** — close all definitions and return directly to reading on touchscreen devices, even when touch reader controls are disabled

During definition word selection, press **Back** to cancel selection first, then hold **Back** or long-press the header Back arrow to leave the lookup chain.

Nested lookups use the same dictionary. Both formatted and plain-text definitions support selection.
Returning to a previous definition restores its page and remembers its selection for the next press of **Confirm**.

The reader keeps up to 16 previous definitions. At this limit, another lookup shows a brief message and leaves the current definition open.
A failed lookup reloads the current definition and keeps its page and selection.

The current definition is released before loading another entry, so two definitions do not need to fit in memory together.
Returning to a previous definition reads it from the SD card again.
If a previous definition cannot be read or its position restored, the definition screen closes. Touchscreen devices return to reading; buttons-only devices return to book word selection.
The screen also closes if a failed forward lookup cannot reload the current definition.


## Korean dictionary (표준국어대사전)

Every Korean release since 1.6.5-ko.10 carries `stdict-korean-dictionary.zip`: the National Institute of Korean Language's Standard Korean Language Dictionary (국립국어원 표준국어대사전), converted for the reader. It holds 287,761 single-word headwords with part of speech, Hanja and definitions (about 21 MB).

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
