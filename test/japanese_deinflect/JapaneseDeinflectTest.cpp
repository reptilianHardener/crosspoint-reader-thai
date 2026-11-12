#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "src/util/JapaneseDeinflect.h"

namespace {

std::vector<std::string> candidates(const char* word) {
  std::vector<std::string> out;
  japaneseDeinflect::forEachCandidate(word, [&out](const char* candidate) {
    out.emplace_back(candidate);
    return false;
  });
  return out;
}

bool hasCandidate(const char* word, const char* base) {
  const auto all = candidates(word);
  return std::find(all.begin(), all.end(), base) != all.end();
}

}  // namespace

TEST(JapaneseDeinflect, IchidanVerb) {
  EXPECT_TRUE(hasCandidate("食べた", "食べる"));
  EXPECT_TRUE(hasCandidate("食べて", "食べる"));
  EXPECT_TRUE(hasCandidate("食べない", "食べる"));
  EXPECT_TRUE(hasCandidate("食べなかった", "食べる"));
  EXPECT_TRUE(hasCandidate("食べました", "食べる"));
}

TEST(JapaneseDeinflect, GodanVerb) {
  EXPECT_TRUE(hasCandidate("書いた", "書く"));
  EXPECT_TRUE(hasCandidate("泳いで", "泳ぐ"));
  EXPECT_TRUE(hasCandidate("読んだ", "読む"));
  EXPECT_TRUE(hasCandidate("待って", "待つ"));
  EXPECT_TRUE(hasCandidate("話した", "話す"));
  EXPECT_TRUE(hasCandidate("書きます", "書く"));
  EXPECT_TRUE(hasCandidate("書かない", "書く"));
  EXPECT_TRUE(hasCandidate("帰りました", "帰る"));
  EXPECT_TRUE(hasCandidate("行こう", "行く"));
  EXPECT_TRUE(hasCandidate("書けば", "書く"));
}

TEST(JapaneseDeinflect, SuruKuruAndAdjectives) {
  EXPECT_TRUE(hasCandidate("勉強した", "勉強する"));
  EXPECT_TRUE(hasCandidate("勉強します", "勉強する"));
  EXPECT_TRUE(hasCandidate("来ない", "来る"));
  EXPECT_TRUE(hasCandidate("きた", "くる"));
  EXPECT_TRUE(hasCandidate("高かった", "高い"));
  EXPECT_TRUE(hasCandidate("高くて", "高い"));
  EXPECT_TRUE(hasCandidate("早く", "早い"));
}

TEST(JapaneseDeinflect, StemIsNeverEmpty) {
  // A bare ending has no stem to attach the dictionary ending to.
  EXPECT_TRUE(candidates("た").empty());
  EXPECT_TRUE(candidates("ます").empty());
  for (const auto& candidate : candidates("いた")) EXPECT_FALSE(candidate.empty());
}

TEST(JapaneseDeinflect, StopsWhenVisitReturnsTrue) {
  const auto all = candidates("書いた");
  const auto target = std::find(all.begin(), all.end(), "書く");
  ASSERT_NE(target, all.end());
  int visits = 0;
  const bool found = japaneseDeinflect::forEachCandidate("書いた", [&visits](const char* candidate) {
    ++visits;
    return std::string(candidate) == "書く";
  });
  EXPECT_TRUE(found);
  EXPECT_EQ(visits, static_cast<int>(target - all.begin()) + 1);
  EXPECT_FALSE(japaneseDeinflect::forEachCandidate("猫", [](const char*) { return true; }));
}

TEST(JapaneseDeinflect, LongWordsFit) {
  // Nine characters (27 bytes) with a longer ending still fit the candidate buffer.
  EXPECT_TRUE(hasCandidate("勉強しませんでした", "勉強する"));
}
