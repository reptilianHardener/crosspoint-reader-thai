#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

// Dictionary forms a Japanese verb or adjective may have come from, so a lookup of 食べた can find
// 食べる. One step only, from a small table of common endings; each candidate is a guess that the
// dictionary confirms or rejects.
namespace japaneseDeinflect {

struct Rule {
  const char* ending;
  const char* base;
};

// Godan rows: the masu stem (i-row), negative stem (a-row), e-row and o-row kana of each
// dictionary ending.
struct GodanRow {
  const char* iRow;
  const char* aRow;
  const char* eRow;
  const char* oRow;
  const char* base;
};

inline constexpr GodanRow kGodanRows[] = {
    {"い", "わ", "え", "お", "う"}, {"き", "か", "け", "こ", "く"}, {"ぎ", "が", "げ", "ご", "ぐ"},
    {"し", "さ", "せ", "そ", "す"}, {"ち", "た", "て", "と", "つ"}, {"に", "な", "ね", "の", "ぬ"},
    {"び", "ば", "べ", "ぼ", "ぶ"}, {"み", "ま", "め", "も", "む"}, {"り", "ら", "れ", "ろ", "る"},
};

// Endings that follow a godan masu stem, negative stem, e-row (potential, conditional) and
// o-row (volitional) kana.
inline constexpr const char* kAfterIRow[] = {"ます", "ました", "ません", "ませんでした", "たい", "たくない", "ながら"};
inline constexpr const char* kAfterARow[] = {"ない", "なかった", "なくて", "せる", "れる"};
inline constexpr const char* kAfterERow[] = {"る", "ば"};
inline constexpr const char* kAfterORow[] = {"う"};

// Fixed endings: past and te forms of godan verbs, ichidan verbs, する, 来る and i-adjectives.
inline constexpr Rule kRules[] = {
    {"った", "う"},
    {"った", "つ"},
    {"った", "る"},
    {"って", "う"},
    {"って", "つ"},
    {"って", "る"},
    {"んだ", "む"},
    {"んだ", "ぶ"},
    {"んだ", "ぬ"},
    {"んで", "む"},
    {"んで", "ぶ"},
    {"んで", "ぬ"},
    {"いた", "く"},
    {"いて", "く"},
    {"いだ", "ぐ"},
    {"いで", "ぐ"},
    {"した", "す"},
    {"して", "す"},
    {"た", "る"},
    {"て", "る"},
    {"ない", "る"},
    {"なかった", "る"},
    {"ます", "る"},
    {"ました", "る"},
    {"ません", "る"},
    {"られる", "る"},
    {"させる", "る"},
    {"よう", "る"},
    {"れば", "る"},
    {"たい", "る"},
    {"した", "する"},
    {"して", "する"},
    {"します", "する"},
    {"しました", "する"},
    {"しない", "する"},
    {"しません", "する"},
    {"しませんでした", "する"},
    {"される", "する"},
    {"させる", "する"},
    {"かった", "い"},
    {"くて", "い"},
    {"くない", "い"},
    {"ければ", "い"},
    {"く", "い"},
    {"さ", "い"},
    {"そう", "い"},
};

// Whole words written in kana whose stem is empty (来た and the like are covered by kRules).
inline constexpr Rule kWholeWords[] = {
    {"きた", "くる"}, {"きて", "くる"}, {"きます", "くる"}, {"こない", "くる"},
    {"した", "する"}, {"して", "する"}, {"します", "する"}, {"しない", "する"},
};

// Longest candidate: an 8-character word (3 bytes each) with its ending swapped.
inline constexpr size_t kMaxCandidateBytes = 40;
using Candidate = std::array<char, kMaxCandidateBytes>;

namespace detail {
inline bool endsWith(const std::string_view word, const std::string_view ending) {
  return word.size() > ending.size() && word.substr(word.size() - ending.size()) == ending;
}

// Writes word minus its ending plus base into out; false when it would not fit.
inline bool replaceEnding(const std::string_view word, const size_t endingBytes, const std::string_view base,
                          Candidate& out) {
  const size_t stemBytes = word.size() - endingBytes;
  if (stemBytes + base.size() + 1 > out.size()) return false;
  std::memcpy(out.data(), word.data(), stemBytes);
  std::memcpy(out.data() + stemBytes, base.data(), base.size());
  out[stemBytes + base.size()] = '\0';
  return true;
}

template <typename Fn>
bool tryRow(const std::string_view word, const char* rowKana, const char* const* endings, const size_t endingCount,
            const char* base, Fn& visit, Candidate& candidate) {
  const std::string_view kana(rowKana);
  for (size_t i = 0; i < endingCount; ++i) {
    const std::string_view ending(endings[i]);
    if (word.size() <= kana.size() + ending.size()) continue;
    if (!endsWith(word, ending) || word.substr(word.size() - ending.size() - kana.size(), kana.size()) != kana) {
      continue;
    }
    if (replaceEnding(word, kana.size() + ending.size(), base, candidate) && visit(candidate.data())) return true;
  }
  return false;
}
}  // namespace detail

// Calls visit(const char* candidate) for each possible dictionary form of word, in table order,
// until visit returns true. Returns true when a visit returned true. Uses no heap.
template <typename Fn>
bool forEachCandidate(const std::string_view word, Fn&& visit) {
  Candidate candidate{};
  for (const Rule& rule : kWholeWords) {
    if (word == rule.ending && visit(rule.base)) return true;
  }
  for (const Rule& rule : kRules) {
    const std::string_view ending(rule.ending);
    if (detail::endsWith(word, ending) && detail::replaceEnding(word, ending.size(), rule.base, candidate) &&
        visit(candidate.data())) {
      return true;
    }
  }
  for (const GodanRow& row : kGodanRows) {
    if (detail::tryRow(word, row.iRow, kAfterIRow, std::size(kAfterIRow), row.base, visit, candidate) ||
        detail::tryRow(word, row.aRow, kAfterARow, std::size(kAfterARow), row.base, visit, candidate) ||
        detail::tryRow(word, row.eRow, kAfterERow, std::size(kAfterERow), row.base, visit, candidate) ||
        detail::tryRow(word, row.oRow, kAfterORow, std::size(kAfterORow), row.base, visit, candidate)) {
      return true;
    }
  }
  return false;
}

}  // namespace japaneseDeinflect
