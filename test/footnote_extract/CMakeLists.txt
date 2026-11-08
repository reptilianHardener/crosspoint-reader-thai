add_executable(FootnoteExtractTest
  FootnoteExtractTest.cpp
  ${REPO_ROOT}/src/util/FootnoteExtract.cpp
  ${REPO_ROOT}/src/util/HtmlToPlainText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
)

target_include_directories(FootnoteExtractTest PRIVATE
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(FootnoteExtractTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FootnoteExtractTest)
