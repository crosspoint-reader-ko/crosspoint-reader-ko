#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "Print.h"
#include "TxtToHtml.h"

namespace {

class StringPrint : public Print {
 public:
  std::string str;
  size_t write(uint8_t b) override {
    str.push_back(static_cast<char>(b));
    return 1;
  }
  size_t write(const uint8_t* buffer, size_t size) override {
    str.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }
};

const std::string kHeader =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<!-- TXT_CACHE_VERSION: 1-ko.1 -->\n"
    "<!DOCTYPE html>\n<html>\n<head><title>test</title></head>\n<body>\n";
const std::string kFooter = "\n</body>\n</html>\n";

std::string convert(std::string_view content) {
  StringPrint out;
  EXPECT_TRUE(TxtToHtml::stream("test.txt", content, out));
  return out.str;
}

TEST(TxtToHtmlTest, LeadingSpacesIndentation) {
  EXPECT_EQ(convert("  two spaces"), kHeader + "<p>&#160;&#160;two spaces</p>" + kFooter);
  EXPECT_EQ(convert("    four spaces"), kHeader + "<p>&#160;&#160;&#160;&#160;four spaces</p>" + kFooter);
  EXPECT_EQ(convert("Line 1\n   three spaces"),
            kHeader + "<p>Line 1</p>\n<p>&#160;&#160;&#160;three spaces</p>" + kFooter);
}

TEST(TxtToHtmlTest, MidLineConsecutiveSpaces) {
  EXPECT_EQ(convert("One space"), kHeader + "<p>One space</p>" + kFooter);
  EXPECT_EQ(convert("Two  spaces"), kHeader + "<p>Two&#160; spaces</p>" + kFooter);
  EXPECT_EQ(convert("Three   spaces"), kHeader + "<p>Three&#160;&#160; spaces</p>" + kFooter);
  EXPECT_EQ(convert("Four    spaces"), kHeader + "<p>Four&#160;&#160;&#160; spaces</p>" + kFooter);
}

TEST(TxtToHtmlTest, PreservesCacheVersionTagsForBothFormats) {
  StringPrint out;
  ASSERT_TRUE(TxtToHtml::stream("test.MD", "text", out));
  EXPECT_EQ(out.str,
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!-- MD_CACHE_VERSION: 1-ko.1 -->\n"
            "<!DOCTYPE html>\n<html>\n<head><title>test</title></head>\n<body>\n<p>text</p>" +
                kFooter);
  EXPECT_NE(out.str.find(TxtToHtml::cacheVersionTag("test.MD")), std::string::npos);
  EXPECT_NE(convert("text").find(TxtToHtml::cacheVersionTag("test.txt")), std::string::npos);
}

TEST(TxtToHtmlTest, EachLineIsAParagraph) {
  EXPECT_EQ(convert("first\nsecond\n"), kHeader + "<p>first</p>\n<p>second</p>\n" + kFooter);
  EXPECT_EQ(convert("first\n\nsecond"), kHeader + "<p>first</p>\n<br />\n<p>second</p>" + kFooter);
  EXPECT_EQ(convert("a\r\nb"), kHeader + "<p>a</p>\n<p>b</p>" + kFooter);
}

}  // namespace
