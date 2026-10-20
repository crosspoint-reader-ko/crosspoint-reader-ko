#include <KoreanStem.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

std::vector<std::string> variantsOf(const char* word) {
  std::vector<std::string> out;
  koreanstem::variants(word, out);
  return out;
}

// Position of `headword` among the candidates, or -1.
int rankOf(const char* word, const char* headword) {
  const auto v = variantsOf(word);
  const auto it = std::find(v.begin(), v.end(), headword);
  return it == v.end() ? -1 : static_cast<int>(it - v.begin());
}

// The headword is a candidate and comes before every listed wrong guess.
void expectBefore(const char* word, const char* headword, std::initializer_list<const char*> wrong = {}) {
  const int rank = rankOf(word, headword);
  ASSERT_GE(rank, 0) << word << " -> " << headword;
  for (const char* w : wrong) {
    const int other = rankOf(word, w);
    if (other >= 0) EXPECT_LT(rank, other) << word << ": " << headword << " before " << w;
  }
}

TEST(KoreanStem, NounsLoseTheirParticle) {
  expectBefore("사랑을", "사랑", {"사다"});
  expectBefore("학교에서는", "학교");
  expectBefore("학생들이", "학생");
  expectBefore("바다로", "바다");
  expectBefore("사람처럼", "사람");
}

TEST(KoreanStem, VerbsGoToTheirDaForm) {
  expectBefore("먹었다", "먹다", {"먹"});
  expectBefore("먹고", "먹다");
  expectBefore("먹은", "먹다", {"먹"});
  expectBefore("간다", "가다");
  expectBefore("갔다", "가다");
  expectBefore("갑니다", "가다");
  expectBefore("할", "하다");
}

TEST(KoreanStem, ContractedVowels) {
  expectBefore("공부했다", "공부하다");
  expectBefore("봤다", "보다");
  expectBefore("줘", "주다");
  expectBefore("됐다", "되다");
  expectBefore("썼다", "쓰다");
}

TEST(KoreanStem, Irregulars) {
  expectBefore("아름다운", "아름답다");
  expectBefore("고마워", "고맙다");
  expectBefore("몰라", "모르다");
  expectBefore("불렀다", "부르다");
  expectBefore("파란", "파랗다");
  expectBefore("들어", "듣다");
}

TEST(KoreanStem, DroppedLieulAndEu) {
  expectBefore("드문", "드물다");
  expectBefore("드는", "들다");
  expectBefore("듭니다", "들다");
  expectBefore("떠는", "떨다");
  expectBefore("떠나", "떠나다");
  expectBefore("떠", "뜨다");
}

TEST(KoreanStem, LongerStemsBeforeShorterVerbs) {
  expectBefore("실례지만", "실례", {"실다"});
  expectBefore("사랑이라고", "사랑", {"사다"});
  expectBefore("친구였으며", "친구", {"치다"});
}

TEST(KoreanStem, NounBeforeVerbWhereTheTextSaysSo) {
  expectBefore("그림이자", "그림", {"그리다"});
  expectBefore("누웠을", "눕다", {"누다"});
  expectBefore("날이", "날", {"날다"});
  expectBefore("먹은", "먹다", {"먹"});
  expectBefore("선하고", "선하다", {"선"});
}

TEST(KoreanStem, OneSyllableNounsStillFound) { EXPECT_GE(rankOf("물을", "물"), 0); }

TEST(KoreanStem, PunctuationAroundTheWord) {
  EXPECT_EQ(rankOf("「사랑」", "사랑"), 0);
  EXPECT_EQ(rankOf("\xE2\x80\x9C사랑을,", "사랑"), 1);
}

TEST(KoreanStem, NonHangulGivesNothing) {
  EXPECT_TRUE(variantsOf("walked").empty());
  EXPECT_TRUE(variantsOf("").empty());
  EXPECT_TRUE(variantsOf("漢字").empty());
}

TEST(KoreanStem, BoundedAndNeverTheWordItself) {
  const auto v = variantsOf("아름다웠습니다만");
  EXPECT_LE(v.size(), koreanstem::kMaxKoreanVariants);
  EXPECT_EQ(std::find(v.begin(), v.end(), "아름다웠습니다만"), v.end());
}

}  // namespace
