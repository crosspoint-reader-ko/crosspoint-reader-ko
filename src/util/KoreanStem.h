#pragma once

#include <string>
#include <vector>

// Dictionary headword guesses for a Korean word as it appears in running text. Korean words carry
// particles and verb endings (사랑을, 먹었다, 아름다운), while dictionaries list the bare noun or
// the 다 form (사랑, 먹다, 아름답다). Rule-based, no lexicon: candidates come out in the order a
// lookup should try them, most likely first, and most will not be headwords.
//
// Order: the word's Hangul alone (edge punctuation dropped), then the word with a particle removed
// when at least two syllables are left, then stems from the longest down, each as its 다 form (with
// contracted vowels, 하다 and the ㅂ/르/ㅎ/ㄷ irregulars restored) and then as a plain noun, and last
// a one-syllable noun. Words with no Hangul give nothing. At most kMaxKoreanVariants candidates.
namespace koreanstem {

inline constexpr size_t kMaxKoreanVariants = 24;

void variants(const std::string& word, std::vector<std::string>& out);

}  // namespace koreanstem
