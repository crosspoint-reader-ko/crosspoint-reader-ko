#include "LanguageRegistry.h"

#include <algorithm>
#include <array>
#include <cstddef>

#include "HyphenationCommon.h"

// Liang hyphenation trie tables are large (~310 KB total across all languages,
// German alone is ~206 KB). Korean text is not Liang-hyphenated, so the Korean
// release compiles them out to fit the firmware into the 6.25 MB stock OTA app
// partition (one-step OTA install on locked X4). Toggle the whole set with
// CP_HYPHENATION_LANGS, or individual languages with CP_HYPHEN_LANG_<XX>.
// Default keeps every language (upstream parity).
#ifndef CP_HYPHENATION_LANGS
#define CP_HYPHENATION_LANGS 1
#endif
#ifndef CP_HYPHEN_LANG_EN
#define CP_HYPHEN_LANG_EN CP_HYPHENATION_LANGS
#endif
#ifndef CP_HYPHEN_LANG_FR
#define CP_HYPHEN_LANG_FR CP_HYPHENATION_LANGS
#endif
#ifndef CP_HYPHEN_LANG_DE
#define CP_HYPHEN_LANG_DE CP_HYPHENATION_LANGS
#endif
#ifndef CP_HYPHEN_LANG_RU
#define CP_HYPHEN_LANG_RU CP_HYPHENATION_LANGS
#endif
#ifndef CP_HYPHEN_LANG_ES
#define CP_HYPHEN_LANG_ES CP_HYPHENATION_LANGS
#endif
#ifndef CP_HYPHEN_LANG_IT
#define CP_HYPHEN_LANG_IT CP_HYPHENATION_LANGS
#endif
#ifndef CP_HYPHEN_LANG_UK
#define CP_HYPHEN_LANG_UK CP_HYPHENATION_LANGS
#endif
#ifndef CP_HYPHEN_LANG_SV
#define CP_HYPHEN_LANG_SV CP_HYPHENATION_LANGS
#endif
#ifndef CP_HYPHEN_LANG_PL
#define CP_HYPHEN_LANG_PL CP_HYPHENATION_LANGS
#endif
#ifndef CP_HYPHEN_LANG_FI
#define CP_HYPHEN_LANG_FI CP_HYPHENATION_LANGS
#endif
#ifndef CP_HYPHEN_LANG_PT
#define CP_HYPHEN_LANG_PT CP_HYPHENATION_LANGS
#endif

#if CP_HYPHEN_LANG_EN
#include "generated/hyph-en.trie.h"
#endif
#if CP_HYPHEN_LANG_FR
#include "generated/hyph-fr.trie.h"
#endif
#if CP_HYPHEN_LANG_DE
#include "generated/hyph-de.trie.h"
#endif
#if CP_HYPHEN_LANG_RU
#include "generated/hyph-ru.trie.h"
#endif
#if CP_HYPHEN_LANG_ES
#include "generated/hyph-es.trie.h"
#endif
#if CP_HYPHEN_LANG_IT
#include "generated/hyph-it.trie.h"
#endif
#if CP_HYPHEN_LANG_SV
#include "generated/hyph-sv.trie.h"
#endif
#if CP_HYPHEN_LANG_PL
#include "generated/hyph-pl.trie.h"
#endif
#if CP_HYPHEN_LANG_UK
#include "generated/hyph-uk.trie.h"
#endif
#if CP_HYPHEN_LANG_FI
#include "generated/hyph-fi.trie.h"
#endif
#if CP_HYPHEN_LANG_PT
#include "generated/hyph-pt.trie.h"
#endif

namespace {

#if CP_HYPHEN_LANG_EN
// English hyphenation patterns (3/3 minimum prefix/suffix length)
LanguageHyphenator englishHyphenator(en_patterns, isLatinLetter, toLowerLatin, 3, 3);
#endif
#if CP_HYPHEN_LANG_FR
LanguageHyphenator frenchHyphenator(fr_patterns, isLatinLetter, toLowerLatin);
#endif
#if CP_HYPHEN_LANG_DE
LanguageHyphenator germanHyphenator(de_patterns, isLatinLetter, toLowerLatin);
#endif
#if CP_HYPHEN_LANG_RU
LanguageHyphenator russianHyphenator(ru_patterns, isCyrillicLetter, toLowerCyrillic);
#endif
#if CP_HYPHEN_LANG_ES
LanguageHyphenator spanishHyphenator(es_patterns, isLatinLetter, toLowerLatin);
#endif
#if CP_HYPHEN_LANG_IT
LanguageHyphenator italianHyphenator(it_patterns, isLatinLetter, toLowerLatin);
#endif
#if CP_HYPHEN_LANG_SV
LanguageHyphenator swedishHyphenator(sv_patterns, isLatinLetter, toLowerLatin);
#endif
#if CP_HYPHEN_LANG_PL
LanguageHyphenator polishHyphenator(pl_patterns, isLatinLetter, toLowerLatin);
#endif
#if CP_HYPHEN_LANG_UK
LanguageHyphenator ukrainianHyphenator(uk_patterns, isCyrillicLetter, toLowerCyrillic);
#endif
#if CP_HYPHEN_LANG_FI
LanguageHyphenator finnishHyphenator(fi_patterns, isLatinLetter, toLowerLatin);
#endif
#if CP_HYPHEN_LANG_PT
LanguageHyphenator portugueseHyphenator(pt_patterns, isLatinLetter, toLowerLatin);
#endif

constexpr std::size_t kLanguageCount = CP_HYPHEN_LANG_EN + CP_HYPHEN_LANG_FR + CP_HYPHEN_LANG_DE + CP_HYPHEN_LANG_RU +
                                       CP_HYPHEN_LANG_ES + CP_HYPHEN_LANG_IT + CP_HYPHEN_LANG_SV + CP_HYPHEN_LANG_PL +
                                       CP_HYPHEN_LANG_UK + CP_HYPHEN_LANG_FI + CP_HYPHEN_LANG_PT;

using EntryArray = std::array<LanguageEntry, kLanguageCount>;

const EntryArray& entries() {
  static const EntryArray kEntries = {{
#if CP_HYPHEN_LANG_EN
      {"english", "en", &englishHyphenator},
#endif
#if CP_HYPHEN_LANG_FR
      {"french", "fr", &frenchHyphenator},
#endif
#if CP_HYPHEN_LANG_DE
      {"german", "de", &germanHyphenator},
#endif
#if CP_HYPHEN_LANG_RU
      {"russian", "ru", &russianHyphenator},
#endif
#if CP_HYPHEN_LANG_ES
      {"spanish", "es", &spanishHyphenator},
#endif
#if CP_HYPHEN_LANG_IT
      {"italian", "it", &italianHyphenator},
#endif
#if CP_HYPHEN_LANG_SV
      {"swedish", "sv", &swedishHyphenator},
#endif
#if CP_HYPHEN_LANG_PL
      {"polish", "pl", &polishHyphenator},
#endif
#if CP_HYPHEN_LANG_UK
      {"ukrainian", "uk", &ukrainianHyphenator},
#endif
#if CP_HYPHEN_LANG_FI
      {"finnish", "fi", &finnishHyphenator},
#endif
#if CP_HYPHEN_LANG_PT
      {"portuguese", "pt", &portugueseHyphenator},
#endif
  }};
  return kEntries;
}

}  // namespace

const LanguageHyphenator* getLanguageHyphenatorForPrimaryTag(const std::string& primaryTag) {
  const auto& allEntries = entries();
  const auto it = std::find_if(allEntries.begin(), allEntries.end(),
                               [&primaryTag](const LanguageEntry& entry) { return primaryTag == entry.primaryTag; });
  return (it != allEntries.end()) ? it->hyphenator : nullptr;
}

LanguageEntryView getLanguageEntries() {
  const auto& allEntries = entries();
  return LanguageEntryView{allEntries.data(), allEntries.size()};
}
