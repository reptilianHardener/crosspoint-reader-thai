#include <Utf8.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "lib/Epub/Epub/hyphenation/Hyphenator.h"
#include "lib/Epub/Epub/hyphenation/ThaiWordBreaker.h"

namespace {

std::vector<size_t> expectedPositionsFromAnnotatedWord(const std::string& annotated) {
  std::vector<size_t> positions;
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(annotated.c_str());
  size_t codepointIndex = 0;

  while (*ptr != 0) {
    if (*ptr == '=') {
      positions.push_back(codepointIndex);
      ++ptr;
      continue;
    }

    utf8NextCodepoint(&ptr);
    ++codepointIndex;
  }

  return positions;
}

std::string stripMarkers(const std::string& annotated) {
  std::string word;
  word.reserve(annotated.size());
  for (char ch : annotated) {
    if (ch != '=') {
      word.push_back(ch);
    }
  }
  return word;
}

std::vector<size_t> breakOffsetsToPositions(const std::string& word, const std::vector<Hyphenator::BreakInfo>& breaks) {
  std::vector<size_t> positions;
  positions.reserve(breaks.size());

  for (const auto& info : breaks) {
    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
    size_t codepointIndex = 0;
    size_t byteOffset = 0;

    while (*ptr != 0 && byteOffset < info.byteOffset) {
      const unsigned char* current = ptr;
      utf8NextCodepoint(&ptr);
      byteOffset += static_cast<size_t>(ptr - current);
      ++codepointIndex;
    }

    positions.push_back(codepointIndex);
  }

  std::sort(positions.begin(), positions.end());
  positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
  return positions;
}

std::string positionsToAnnotated(const std::string& word, const std::vector<size_t>& positions) {
  std::string result;
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
  size_t codepointIndex = 0;
  size_t breakIndex = 0;

  while (*ptr != 0) {
    while (breakIndex < positions.size() && positions[breakIndex] == codepointIndex) {
      result.push_back('=');
      ++breakIndex;
    }

    const unsigned char* current = ptr;
    utf8NextCodepoint(&ptr);
    result.append(reinterpret_cast<const char*>(current), reinterpret_cast<const char*>(ptr));
    ++codepointIndex;
  }

  while (breakIndex < positions.size() && positions[breakIndex] == codepointIndex) {
    result.push_back('=');
    ++breakIndex;
  }

  return result;
}

// Each case is a Thai run with '=' marking where a line may break. The breaker must find
// exactly those boundaries: dictionary words, never inside a consonant cluster.
std::string segment(const std::string& annotated, const bool includeFallback = false) {
  const std::string word = stripMarkers(annotated);
  return positionsToAnnotated(word, breakOffsetsToPositions(word, Hyphenator::breakOffsets(word, includeFallback)));
}

class ThaiWordBreak : public ::testing::Test {
 protected:
  void SetUp() override {
    Hyphenator::setPreferredLanguage("th");
    ThaiWordBreaker::setUserDictionary({});
  }
  void TearDown() override { ThaiWordBreaker::setUserDictionary({}); }
};

}  // namespace

TEST_F(ThaiWordBreak, SingleWordsStayWhole) {
  for (const char* word : {"กัน", "หนัง", "ตัด", "ขึ้น", "ซึ่ง", "ไว้"}) {
    EXPECT_EQ(segment(word), word);
  }
}

TEST_F(ThaiWordBreak, SegmentsRunsAtDictionaryWordBoundaries) {
  for (const char* annotated : {
           "หนัง=กวาง",
           "โดย=จะ=ตัด=การ=เคลื่อนที่=ไป=กลับ=ของ=ก้านสูบ=ออก=ไป",
           "ความคิด=ฟุ้งซ่าน=ภายใน=หัว",
           "ประสิทธิภาพ=ของ=มัน=ดีกว่า=เครื่องจักร=ไอน้ำ",
           "นี่=คือ=ผลิตภัณฑ์=ที่จะ=พลิกโฉม=เมือง",
           "ใน=การ=ขับเคลื่อน=แทน",
       }) {
    EXPECT_EQ(segment(annotated), annotated);
  }
}

TEST_F(ThaiWordBreak, SingleLettersAreNotWords) {
  // The word list holds letter names such as ห. An unknown run must not be cut at them.
  EXPECT_EQ(segment("หญิ"), "หญิ");
  EXPECT_EQ(segment("เด็กหญิ"), "เด็ก=หญิ");
  // The fallback used only for a word wider than the line still cuts unknown text at clusters.
  EXPECT_EQ(segment("เด็กหญิ", true), "เด็ก=ห=ญิ");
}

TEST_F(ThaiWordBreak, HintsBreakOnlyUnknownText) {
  // A hint is where the source had a zero-width space or soft hyphen. Byte offsets: Thai is
  // three bytes per codepoint.
  const auto breaksWithHint = [](const std::string& word, const uint16_t hint) {
    const std::vector<uint16_t> hints{hint};
    return breakOffsetsToPositions(word, Hyphenator::breakOffsets(word, false, &hints));
  };
  // Inside a dictionary word (เด็กหญิง, and ต่าง before its ๆ): dropped.
  EXPECT_TRUE(breaksWithHint("เด็กหญิง", 21).empty());
  EXPECT_TRUE(breaksWithHint("ต่างๆ", 9).empty());
  // Unknown text (ฃ and ฅ are obsolete letters): kept at a cluster boundary ...
  EXPECT_EQ(breaksWithHint("ฃฅฃฅ", 6), (std::vector<size_t>{2}));
  // ... but not before a vowel mark, nor before a consonant that carries การันต์ (์).
  EXPECT_TRUE(breaksWithHint("ฃฅิฅ", 6).empty());
  EXPECT_TRUE(breaksWithHint("ฃฅซ์", 6).empty());
  // A hint at an existing boundary adds nothing.
  EXPECT_EQ(breaksWithHint("ในการ", 6), (std::vector<size_t>{2}));
}

TEST_F(ThaiWordBreak, BreaksNeverRequestAnInsertedHyphen) {
  for (const auto& info : Hyphenator::breakOffsets("ในการขับเคลื่อนแทน", false)) {
    EXPECT_FALSE(info.requiresInsertedHyphen);
  }
}

TEST_F(ThaiWordBreak, UserDictionaryRevisionTracksContents) {
  EXPECT_EQ(ThaiWordBreaker::userDictionaryRevision(), 0);
  EXPECT_FALSE(ThaiWordBreaker::hasUserDictionary());

  ThaiWordBreaker::setUserDictionary({"ก้านสูบ"});
  const uint16_t first = ThaiWordBreaker::userDictionaryRevision();
  EXPECT_NE(first, 0);
  EXPECT_TRUE(ThaiWordBreaker::hasUserDictionary());

  ThaiWordBreaker::setUserDictionary({"ก้านสูบ", "พลิกโฉม"});
  EXPECT_NE(ThaiWordBreaker::userDictionaryRevision(), first);

  // Same words again give the same fingerprint, so an unchanged dictionary keeps caches.
  ThaiWordBreaker::setUserDictionary({"ก้านสูบ"});
  EXPECT_EQ(ThaiWordBreaker::userDictionaryRevision(), first);
}

TEST_F(ThaiWordBreak, UserWordsAreNeverSplit) {
  // ตาก|ลม covers every letter, so as a mere scoring preference กลม lost to it.
  EXPECT_EQ(segment("ตาก=ลม"), "ตาก=ลม");
  ThaiWordBreaker::setUserDictionary({"กลม"});
  EXPECT_EQ(segment("ตา=กลม"), "ตา=กลม");
  EXPECT_EQ(segment("ตา=กลม=กิน"), "ตา=กลม=กิน");
  // Kept whole even when that leaves letters the dictionary does not know (งสา, ว).
  ThaiWordBreaker::setUserDictionary({"งสา"});
  EXPECT_EQ(segment("เด็ก=หญิ=งสา=ว"), "เด็ก=หญิ=งสา=ว");
}

TEST_F(ThaiWordBreak, UserWordsMatchOnlyAtClusterBoundaries) {
  // ะสม would start on a following vowel, i.e. inside มานะ's last cluster: not an occurrence.
  ThaiWordBreaker::setUserDictionary({"ะสม"});
  EXPECT_EQ(segment("เขา=ชื่อ=มานะ=สมใจ"), "เขา=ชื่อ=มานะ=สมใจ");
}

TEST_F(ThaiWordBreak, OverlappingUserWordsKeepTheLeftmostLongest) {
  // The longest word starting at a position wins.
  ThaiWordBreaker::setUserDictionary({"กลม", "กลมกิ"});
  EXPECT_EQ(segment("ตา=กลมกิ=น"), "ตา=กลมกิ=น");
  // The leftmost occurrence wins; กลม overlaps its tail and is not protected.
  ThaiWordBreaker::setUserDictionary({"ตาก", "กลม"});
  EXPECT_EQ(segment("ตาก=ลม"), "ตาก=ลม");
}

TEST_F(ThaiWordBreak, FallbackAndHintsSkipUserWords) {
  // ฃ and ฅ are obsolete letters, so the fallback cuts ฃฅฃฅ at every consonant.
  EXPECT_EQ(segment("ฃ=ฅ=ฃ=ฅ", true), "ฃ=ฅ=ฃ=ฅ");
  ThaiWordBreaker::setUserDictionary({"ฅฃ"});
  EXPECT_EQ(segment("ฃ=ฅฃ=ฅ", true), "ฃ=ฅฃ=ฅ");
  // A hint (byte offset 6, between ฅ and ฃ) inside the user word is dropped.
  const std::vector<uint16_t> hints{6};
  EXPECT_EQ(Hyphenator::breakOffsets("ฃฅฃฅ", false, &hints).size(), 2u);
  for (const auto& info : Hyphenator::breakOffsets("ฃฅฃฅ", false, &hints)) {
    EXPECT_NE(info.byteOffset, 6u);
  }
}

TEST_F(ThaiWordBreak, EmptyUserDictionaryRevisionStaysZero) {
  ThaiWordBreaker::setUserDictionary({"ก้านสูบ"});
  ThaiWordBreaker::setUserDictionary({});
  EXPECT_EQ(ThaiWordBreaker::userDictionaryRevision(), 0);
  // Words that are all dropped (shorter than two codepoints) count as no dictionary.
  ThaiWordBreaker::setUserDictionary({"ก"});
  EXPECT_EQ(ThaiWordBreaker::userDictionaryRevision(), 0);
}

TEST_F(ThaiWordBreak, ValidUserWordsAreThaiAndAtLeastTwoCodepoints) {
  EXPECT_TRUE(ThaiWordBreaker::isValidUserWord("กลม"));
  EXPECT_TRUE(ThaiWordBreaker::isValidUserWord("ก่"));
  EXPECT_FALSE(ThaiWordBreaker::isValidUserWord(""));
  EXPECT_FALSE(ThaiWordBreaker::isValidUserWord("ก"));
  EXPECT_FALSE(ThaiWordBreaker::isValidUserWord("กลม "));
  EXPECT_FALSE(ThaiWordBreaker::isValidUserWord("abc"));
  EXPECT_FALSE(ThaiWordBreaker::isValidUserWord("iPhoneไทย"));
}

TEST_F(ThaiWordBreak, UserWordCutLimitFindsAWordCutOffByTheEnd) {
  ThaiWordBreaker::setUserDictionary({"กลม"});
  const std::string cutOff = "ตากล";  // กลม starts after ตา (6 bytes) and runs past the end
  EXPECT_EQ(ThaiWordBreaker::userWordCutLimit(cutOff.data(), cutOff.size()), 6u);
  const std::string whole = "ตากลม";
  EXPECT_EQ(ThaiWordBreaker::userWordCutLimit(whole.data(), whole.size()), whole.size());
  const std::string other = "ตากิ";  // ก followed by something else
  EXPECT_EQ(ThaiWordBreaker::userWordCutLimit(other.data(), other.size()), other.size());
  ThaiWordBreaker::setUserDictionary({});
  EXPECT_EQ(ThaiWordBreaker::userWordCutLimit(cutOff.data(), cutOff.size()), cutOff.size());
}

TEST_F(ThaiWordBreak, UserWordsMustStartAndEndAtBreakableEdges) {
  EXPECT_FALSE(ThaiWordBreaker::isValidUserWord("ะสม"));  // starts with a following vowel
  EXPECT_FALSE(ThaiWordBreaker::isValidUserWord("่กลม"));  // starts with a tone mark
  EXPECT_FALSE(ThaiWordBreaker::isValidUserWord("กเ"));   // ends with a leading vowel
  EXPECT_TRUE(ThaiWordBreaker::isValidUserWord("เกม"));
  std::string longWord;
  for (int i = 0; i < 20; ++i) longWord += "ก";
  EXPECT_TRUE(ThaiWordBreaker::isValidUserWord(longWord));
  EXPECT_FALSE(ThaiWordBreaker::isValidUserWord(longWord + "ก"));  // 21 codepoints
  // Hand-edited entries that could never match are dropped when the list loads.
  ThaiWordBreaker::setUserDictionary({"ะสม", "abc"});
  EXPECT_FALSE(ThaiWordBreaker::hasUserDictionary());
  EXPECT_EQ(ThaiWordBreaker::userDictionaryRevision(), 0);
}
