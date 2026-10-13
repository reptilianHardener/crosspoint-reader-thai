set(THAI_HYPHENATION_SOURCES
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/ThaiWordBreaker.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

# Dictionary word boundaries.
add_executable(ThaiWordBreakTest
  ThaiWordBreakTest.cpp
  ${THAI_HYPHENATION_SOURCES}
)
target_include_directories(ThaiWordBreakTest PRIVATE
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Utf8
)
target_link_libraries(ThaiWordBreakTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)
gtest_discover_tests(ThaiWordBreakTest)

# Line layout of Thai text through ParsedText.
add_executable(ThaiLineBreakingTest
  ThaiLineBreakingTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/WordStore.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${THAI_HYPHENATION_SOURCES}
)
target_include_directories(ThaiLineBreakingTest PRIVATE
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/FsHelpers
)
target_link_libraries(ThaiLineBreakingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)
gtest_discover_tests(ThaiLineBreakingTest)

# Vertical stacking of Thai marks.
add_executable(ThaiMarkStackTest
  ThaiMarkStackTest.cpp
)
target_include_directories(ThaiMarkStackTest PRIVATE
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
)
target_link_libraries(ThaiMarkStackTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)
gtest_discover_tests(ThaiMarkStackTest)
