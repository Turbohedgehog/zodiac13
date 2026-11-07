# One CTest test per gtest suite instead of per test: starting z13_test_runner costs about 0.3 s in a Debug build
# (a 490 MB library), a third of a serial run, and a suite's tests share the process. Called when CTest reads the
# test list, so it sees the tests the built runner has; `excluded` is a gtest negative filter (the tests registered
# on their own).
function(z13_add_gtest_suite_tests runner working_directory excluded)
  execute_process(
      COMMAND "${runner}" --gtest_list_tests "--gtest_filter=-${excluded}"
      WORKING_DIRECTORY "${working_directory}"
      OUTPUT_VARIABLE listing
      RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Listing the tests of ${runner} failed: ${result}")
  endif()
  string(REPLACE "\n" ";" lines "${listing}")
  set(suite "")
  set(has_enabled_test FALSE)
  # The trailing empty entry closes the last suite.
  foreach(line IN LISTS lines ITEMS "")
    if(line STREQUAL "" OR line MATCHES "^([^ #]+)\\.( +#.*)?$")
      if(NOT suite STREQUAL "" AND has_enabled_test)
        # CTest reads this file with the old add_test signature only.
        add_test("${suite}" "${runner}" "--gtest_filter=${suite}.*-${excluded}")
        set_tests_properties("${suite}" PROPERTIES WORKING_DIRECTORY "${working_directory}")
      endif()
      set(suite "${CMAKE_MATCH_1}")
      set(has_enabled_test FALSE)
    elseif(line MATCHES "^  ([^ ]+)")
      # A disabled suite or test is not run; a suite of only those gets no test.
      if(NOT suite MATCHES "(^|/)DISABLED_" AND NOT CMAKE_MATCH_1 MATCHES "^DISABLED_")
        set(has_enabled_test TRUE)
      endif()
    endif()
  endforeach()
endfunction()
