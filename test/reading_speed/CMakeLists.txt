add_executable(ReadingSpeedTest
  ReadingSpeedTest.cpp
  ${REPO_ROOT}/src/util/ReadingSpeed.cpp
)

target_include_directories(ReadingSpeedTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(ReadingSpeedTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReadingSpeedTest)
