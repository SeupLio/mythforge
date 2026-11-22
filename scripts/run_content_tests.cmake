# Runs the content pipeline as one CTest case so CI covers compiler + content
# without shell-specific quoting. Invoked with:
#   cmake -DMF=<mf executable> -DCONTENT=<dir> -DSCHEMA=<file> -P run_content_tests.cmake
foreach(step validate build test)
  execute_process(
    COMMAND "${MF}" ${step} "${CONTENT}" --schema "${SCHEMA}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  message(STATUS "mf ${step}:\n${output}${error}")
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "mf ${step} failed with ${result}")
  endif()
endforeach()
