#include "KoreanStem.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace koreanstem {
namespace {

constexpr char32_t kFirstSyllable = 0xAC00;
constexpr char32_t kLastSyllable = 0xD7A3;

// Jongseong (final consonant) indexes, 0 = none.
constexpr int kFinalN = 4;   // ㄴ
constexpr int kFinalD = 7;   // ㄷ
constexpr int kFinalL = 8;   // ㄹ
constexpr int kFinalM = 16;  // ㅁ
constexpr int kFinalB = 17;  // ㅂ
constexpr int kFinalSS = 20;  // ㅆ
constexpr int kFinalH = 27;   // ㅎ
// Jungseong (vowel) indexes.
constexpr int kVowelA = 0;    // ㅏ
constexpr int kVowelAe = 1;   // ㅐ
constexpr int kVowelEo = 4;   // ㅓ
constexpr int kVowelYeo = 6;  // ㅕ
constexpr int kVowelO = 8;    // ㅗ
constexpr int kVowelWa = 9;   // ㅘ
constexpr int kVowelWae = 10;  // ㅙ
constexpr int kVowelOe = 11;   // ㅚ
constexpr int kVowelU = 13;    // ㅜ
constexpr int kVowelWo = 14;   // ㅝ
constexpr int kVowelEu = 18;   // ㅡ
constexpr int kVowelI = 20;    // ㅣ
// Choseong (initial) indexes.
constexpr int kInitialIeung = 11;  // ㅇ
constexpr int kInitialH = 18;      // ㅎ
constexpr int kInitialR = 5;       // ㄹ

bool isSyllable(const char32_t c) { return c >= kFirstSyllable && c <= kLastSyllable; }
int initialOf(const char32_t c) { return static_cast<int>((c - kFirstSyllable) / (21 * 28)); }
int vowelOf(const char32_t c) { return static_cast<int>((c - kFirstSyllable) / 28 % 21); }
int finalOf(const char32_t c) { return static_cast<int>((c - kFirstSyllable) % 28); }
char32_t compose(const int initial, const int vowel, const int final) {
  return kFirstSyllable + static_cast<char32_t>((initial * 21 + vowel) * 28 + final);
}
char32_t withFinal(const char32_t c, const int final) { return compose(initialOf(c), vowelOf(c), final); }
char32_t withVowel(const char32_t c, const int vowel) { return compose(initialOf(c), vowel, finalOf(c)); }

// UTF-8 decode, keeping only the first run of Hangul syllables (a word's punctuation, quotes and
// Latin around it are dropped). Malformed bytes end the run.
std::u32string hangulRun(const std::string& word) {
  std::u32string run;
  const auto* p = reinterpret_cast<const uint8_t*>(word.data());
  const auto* end = p + word.size();
  while (p < end) {
    char32_t c;
    int len;
    if (*p < 0x80) {
      c = *p;
      len = 1;
    } else if ((*p & 0xE0) == 0xC0) {
      c = *p & 0x1F;
      len = 2;
    } else if ((*p & 0xF0) == 0xE0) {
      c = *p & 0x0F;
      len = 3;
    } else if ((*p & 0xF8) == 0xF0) {
      c = *p & 0x07;
      len = 4;
    } else {
      break;
    }
    if (end - p < len) break;
    for (int i = 1; i < len; i++) c = (c << 6) | (p[i] & 0x3F);
    p += len;
    if (isSyllable(c)) {
      run.push_back(c);
    } else if (!run.empty()) {
      break;
    }
  }
  return run;
}

void appendUtf8(std::string& out, const char32_t c) {
  // Hangul syllables are all three-byte sequences.
  out.push_back(static_cast<char>(0xE0 | (c >> 12)));
  out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
  out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
}

std::string utf8(const std::u32string& s, const size_t count) {
  std::string out;
  out.reserve(count * 3 + 3);
  for (size_t i = 0; i < count && i < s.size(); i++) appendUtf8(out, s[i]);
  return out;
}

// Particles, longest first so 에서는 is tried before 는. Stored as UTF-8.
constexpr const char* kParticles[] = {
    "에서부터", "으로부터", "에게서", "으로서", "으로써", "이라도", "까지는", "에서는", "에게는", "으로는",
    "에서도", "부터", "까지", "에게", "한테", "께서", "에서", "으로", "처럼", "보다", "마저", "조차",
    "이나", "이랑", "이란", "하고", "에는", "에도", "와", "과", "을", "를", "이", "가", "은", "는",
    "의", "에", "도", "만", "로", "랑", "나"};

// Syllables (UTF-8) that follow an ㅂ-irregular stem whose ㅂ dropped: 아름다운, 고마워, 더웠다.
bool followsBIrregular(const char32_t next) {
  return next == U'우' || next == U'운' || next == U'울' || next == U'움' || next == U'워' || next == U'웠';
}

// Syllables that follow a 르-irregular stem: 몰라, 불렀다.
bool followsReuIrregular(const char32_t next) {
  return next == U'라' || next == U'러' || next == U'랐' || next == U'렀';
}

class Collector {
 public:
  Collector(const std::string& word, std::vector<std::string>& out) : word_(word), out_(out) {}

  void add(const std::string& v) {
    if (out_.size() >= kMaxKoreanVariants || v.empty() || v == word_) return;
    if (std::find(out_.begin(), out_.end(), v) == out_.end()) out_.push_back(v);
  }

  void addStem(const std::u32string& stem) {
    if (stem.empty()) return;
    std::string v = utf8(stem, stem.size());
    v += "\xEB\x8B\xA4";  // 다
    add(v);
  }

 private:
  const std::string& word_;
  std::vector<std::string>& out_;
};

// 다 forms for the stem run[0..k) given what follows it.
void addVerbForms(Collector& c, const std::u32string& run, const size_t k) {
  std::u32string stem = run.substr(0, k);
  const char32_t last = stem.back();
  const char32_t next = k < run.size() ? run[k] : 0;
  const int final = finalOf(last);
  const int vowel = vowelOf(last);

  // 르 irregular first: 몰(라) is 모르다, never 몰다 (which conjugates 몰아).
  if (final == kFinalL && next != 0 && followsReuIrregular(next)) {
    std::u32string r = stem;
    r.back() = withFinal(last, 0);
    r.push_back(compose(kInitialR, kVowelEu, 0));
    c.addStem(r);
  }

  // The whole word as a stem only through its final consonant (간 -> 가다, 파란 -> 파랗다): the
  // word plus 다 is never a better guess than the word itself.
  if (k < run.size()) c.addStem(stem);  // 먹(었다) -> 먹다

  // A final ㄴ/ㄹ/ㅁ/ㅂ/ㅆ that is an ending, not the stem's: 간다, 할, 함, 갑니다, 갔다 -> 가다.
  if (final == kFinalN || final == kFinalL || final == kFinalM || final == kFinalB || final == kFinalSS) {
    std::u32string bare = stem;
    bare.back() = withFinal(last, 0);
    c.addStem(bare);
    // Contracted vowels: 했/해 -> 하, 봤/봐 -> 보, 줬 -> 주, 됐 -> 되, 켰 -> 키, 썼 -> 쓰.
    if (final == kFinalSS) {
      int original = -1;
      if (vowel == kVowelAe && initialOf(last) == kInitialH) original = kVowelA;
      if (vowel == kVowelWa) original = kVowelO;
      if (vowel == kVowelWo) original = kVowelU;
      if (vowel == kVowelWae) original = kVowelOe;
      if (vowel == kVowelYeo) original = kVowelI;
      if (vowel == kVowelEo || vowel == kVowelA) original = kVowelEu;
      if (original >= 0) {
        bare.back() = compose(initialOf(last), original, 0);
        c.addStem(bare);
      }
    }
    // ㅎ irregular: 파란, 그런 -> 파랗다, 그렇다.
    if (final == kFinalN && k == run.size()) {
      std::u32string h = stem;
      h.back() = withFinal(last, kFinalH);
      c.addStem(h);
    }
  } else if (final == 0) {
    // Contracted vowels with no past marker: 해 -> 하, 봐 -> 보, 줘 -> 주, 돼 -> 되, 켜 -> 키, 써 -> 쓰.
    int original = -1;
    if (vowel == kVowelAe && initialOf(last) == kInitialH) original = kVowelA;
    if (vowel == kVowelWa) original = kVowelO;
    if (vowel == kVowelWo) original = kVowelU;
    if (vowel == kVowelWae) original = kVowelOe;
    if (vowel == kVowelYeo) original = kVowelI;
    if (original >= 0) {
      std::u32string v = stem;
      v.back() = withVowel(last, original);
      c.addStem(v);
    }
    // ㅂ irregular: 아름다(운) -> 아름답다, 고마(워) -> 고맙다.
    if (next != 0 && followsBIrregular(next)) {
      std::u32string b = stem;
      b.back() = withFinal(last, kFinalB);
      c.addStem(b);
    }
  }

  // ㄷ irregular: 들(어) -> 듣다, 걸(었다) -> 걷다.
  if (final == kFinalL && next != 0 && initialOf(next) == kInitialIeung) {
    std::u32string d = stem;
    d.back() = withFinal(last, kFinalD);
    c.addStem(d);
  }
}

}  // namespace

void variants(const std::string& word, std::vector<std::string>& out) {
  out.clear();
  const std::u32string run = hangulRun(word);
  if (run.empty()) return;
  out.reserve(kMaxKoreanVariants);
  Collector c(word, out);
  const std::string whole = utf8(run, run.size());

  c.add(whole);

  // Remainders before a particle (and before a plural 들), most syllables first.
  std::vector<std::string> nouns;
  for (const char* particle : kParticles) {
    const size_t plen = strlen(particle);
    if (whole.size() <= plen || whole.compare(whole.size() - plen, plen, particle) != 0) continue;
    std::string noun = whole.substr(0, whole.size() - plen);
    nouns.push_back(noun);
    static constexpr char kPlural[] = "\xEB\x93\xA4";  // 들
    if (noun.size() > 3 && noun.compare(noun.size() - 3, 3, kPlural) == 0) nouns.push_back(noun.substr(0, noun.size() - 3));
  }
  for (const auto& noun : nouns) {
    if (noun.size() >= 6) c.add(noun);  // two syllables or more
  }

  for (size_t k = run.size(); k >= 1; k--) addVerbForms(c, run, k);

  for (const auto& noun : nouns) c.add(noun);  // one-syllable nouns: 물(을)

  for (size_t k = run.size() - 1; k >= 1; k--) c.add(utf8(run, k));
}

}  // namespace koreanstem
