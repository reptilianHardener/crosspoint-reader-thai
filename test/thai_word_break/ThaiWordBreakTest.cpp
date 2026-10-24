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
