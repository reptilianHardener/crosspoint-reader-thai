add_executable(ReadingStatusStoreTest ReadingStatusStoreTest.cpp ${REPO_ROOT}/src/ReadingStatusStore.cpp)
target_link_libraries(ReadingStatusStoreTest PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(ReadingStatusStoreTest PRIVATE ../library_builder/stubs ${REPO_ROOT}/src ${REPO_ROOT}/lib/LibraryIndex)
gtest_discover_tests(ReadingStatusStoreTest)
