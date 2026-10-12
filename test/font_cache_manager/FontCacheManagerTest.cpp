#include <gtest/gtest.h>

#include <cstdlib>
#include <map>
#include <memory>
#include <new>

#include "FontCacheManager.h"
#include "FontDecompressor.h"
#include "SdFontFamily.h"

// The Korean fork keeps its own font engine: flash EpdFontFamily fonts are prewarmed through the
// FontDecompressor, while SD-card .epdfont families keep glyphs resident and are never prewarmed.

namespace {

bool countHeapAllocations = false;
size_t heapAllocationCount = 0;

UnifiedFontSlot flashSlot(const EpdFontFamily& family) {
  return UnifiedFontSlot(std::make_unique<UnifiedFontFamily>(&family));
}

const FontDecompressor::PrewarmCall* findCall(const FontDecompressor& decompressor, const EpdFontData* data) {
  for (int i = 0; i < decompressor.prewarmCallCount; i++) {
    if (decompressor.prewarmCalls[i].fontData == data) return &decompressor.prewarmCalls[i];
  }
  return nullptr;
}

}  // namespace

void* operator new(const size_t size) {
  if (countHeapAllocations) heapAllocationCount++;
  if (void* allocation = std::malloc(size)) return allocation;
  throw std::bad_alloc();
}

void operator delete(void* allocation) noexcept { std::free(allocation); }

void operator delete(void* allocation, size_t) noexcept { std::free(allocation); }

TEST(FontCacheManagerTest, PrewarmScopeBatchesEachFontAndResolvedStyleSeparately) {
  EpdFontData readerRegular{reinterpret_cast<const void*>(1)};
  EpdFontData readerBold{reinterpret_cast<const void*>(1)};
  EpdFontData fallbackRegular{reinterpret_cast<const void*>(1)};
  const EpdFontFamily readerFamily(&readerRegular, &readerBold, nullptr, nullptr);
  const EpdFontFamily fallbackFamily(&fallbackRegular, nullptr, nullptr, nullptr);
  std::map<int, UnifiedFontSlot> fonts;
  fonts.emplace(-17, flashSlot(readerFamily));
  fonts.emplace(23, flashSlot(fallbackFamily));
  FontDecompressor decompressor;
  FontCacheManager manager(fonts);
  manager.setFontDecompressor(&decompressor);

  auto scope = manager.createPrewarmScope();
  manager.recordText("A", -17, EpdFontFamily::REGULAR);
  manager.recordText("B", -17, EpdFontFamily::BOLD);
  manager.recordText("C", 23, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();

  ASSERT_EQ(3, decompressor.prewarmCallCount);
  const auto* regularCall = findCall(decompressor, &readerRegular);
  ASSERT_NE(nullptr, regularCall);
  EXPECT_STREQ("A", regularCall->text);
  const auto* boldCall = findCall(decompressor, &readerBold);
  ASSERT_NE(nullptr, boldCall);
  EXPECT_STREQ("B", boldCall->text);
  const auto* fallbackCall = findCall(decompressor, &fallbackRegular);
  ASSERT_NE(nullptr, fallbackCall);
  EXPECT_STREQ("C", fallbackCall->text);
}

TEST(FontCacheManagerTest, PrewarmScopeMergesBuiltInStylesThatShareFontData) {
  EpdFontData regularData{reinterpret_cast<const void*>(1)};
  EpdFontData boldData{reinterpret_cast<const void*>(1)};
  const EpdFontFamily family(&regularData, &boldData, &regularData, nullptr);
  std::map<int, UnifiedFontSlot> fonts;
  fonts.emplace(5, flashSlot(family));
  FontDecompressor decompressor;
  FontCacheManager manager(fonts);
  manager.setFontDecompressor(&decompressor);

  auto scope = manager.createPrewarmScope();
  manager.recordText("A", 5, EpdFontFamily::REGULAR);
  manager.recordText("B", 5, EpdFontFamily::BOLD);
  manager.recordText("C", 5, EpdFontFamily::ITALIC);
  scope.endScanAndPrewarm();

  ASSERT_EQ(2, decompressor.prewarmCallCount);
  const auto* regularCall = findCall(decompressor, &regularData);
  const auto* boldCall = findCall(decompressor, &boldData);
  ASSERT_NE(nullptr, regularCall);
  ASSERT_NE(nullptr, boldCall);
  EXPECT_STREQ("AC", regularCall->text);
  EXPECT_STREQ("B", boldCall->text);
}

TEST(FontCacheManagerTest, PrewarmScopePreservesUniqueMultibyteCodepoints) {
  EpdFontData regularData{reinterpret_cast<const void*>(1)};
  const EpdFontFamily family(&regularData, nullptr, nullptr, nullptr);
  std::map<int, UnifiedFontSlot> fonts;
  fonts.emplace(7, flashSlot(family));
  FontDecompressor decompressor;
  FontCacheManager manager(fonts);
  manager.setFontDecompressor(&decompressor);

  auto scope = manager.createPrewarmScope();
  manager.recordText("\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80\xC3\xA9", 7, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();

  ASSERT_EQ(1, decompressor.prewarmCallCount);
  EXPECT_STREQ("\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80", decompressor.prewarmCalls[0].text);
}

TEST(FontCacheManagerTest, PrewarmScanDoesNotAllocateHeapMemory) {
  EpdFontData regularData{reinterpret_cast<const void*>(1)};
  const EpdFontFamily family(&regularData, nullptr, nullptr, nullptr);
  std::map<int, UnifiedFontSlot> fonts;
  fonts.emplace(7, flashSlot(family));
  FontDecompressor decompressor;
  FontCacheManager manager(fonts);
  manager.setFontDecompressor(&decompressor);

  heapAllocationCount = 0;
  countHeapAllocations = true;
  auto scope = manager.createPrewarmScope();
  manager.recordText("Repeated text: \xC3\xA9 \xE4\xB8\xAD \xF0\x9F\x98\x80", 7, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();
  countHeapAllocations = false;

  EXPECT_EQ(0U, heapAllocationCount);
}

TEST(FontCacheManagerTest, SdFontsAreNotPrewarmed) {
  std::map<int, UnifiedFontSlot> fonts;
  fonts.emplace(7, UnifiedFontSlot(std::make_unique<UnifiedFontFamily>(new SdFontFamily())));
  FontDecompressor decompressor;
  FontCacheManager manager(fonts);
  manager.setFontDecompressor(&decompressor);

  auto scope = manager.createPrewarmScope();
  manager.recordText("\xEA\xB0\x80\xEB\x82\x98", 7, EpdFontFamily::BOLD);
  scope.endScanAndPrewarm();
  manager.prewarmCache(7, "title", 0x0F);

  EXPECT_EQ(0, decompressor.prewarmCallCount);
}

TEST(FontCacheManagerTest, IncrementalPrewarmHonoursStyleMask) {
  EpdFontData regularData{reinterpret_cast<const void*>(1)};
  EpdFontData boldData{reinterpret_cast<const void*>(1)};
  const EpdFontFamily family(&regularData, &boldData, nullptr, nullptr);
  std::map<int, UnifiedFontSlot> fonts;
  fonts.emplace(7, flashSlot(family));
  FontDecompressor decompressor;
  FontCacheManager manager(fonts);
  manager.setFontDecompressor(&decompressor);

  manager.prewarmCache(7, "title", 0x02);
  ASSERT_EQ(1, decompressor.prewarmCallCount);
  EXPECT_EQ(&boldData, decompressor.prewarmCalls[0].fontData);
  EXPECT_STREQ("title", decompressor.prewarmCalls[0].text);
}
