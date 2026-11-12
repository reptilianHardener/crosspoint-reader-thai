add_executable(JapaneseDeinflectTest
  JapaneseDeinflectTest.cpp
)

target_link_libraries(JapaneseDeinflectTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(JapaneseDeinflectTest)
