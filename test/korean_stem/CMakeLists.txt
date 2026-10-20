add_executable(KoreanStemTest
  KoreanStemTest.cpp
  ${REPO_ROOT}/src/util/KoreanStem.cpp
)

target_include_directories(KoreanStemTest PRIVATE ${REPO_ROOT}/src/util)

target_link_libraries(KoreanStemTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KoreanStemTest)
