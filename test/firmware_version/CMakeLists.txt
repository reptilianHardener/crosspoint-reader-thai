add_executable(FirmwareVersionTest
  FirmwareVersionTest.cpp
)

target_link_libraries(FirmwareVersionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FirmwareVersionTest)
