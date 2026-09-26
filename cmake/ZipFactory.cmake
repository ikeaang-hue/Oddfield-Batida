# Zips resources/factory into the archive built into the plugin. Run at build
# time (cmake -P), so files the factory tool added since CMake last
# configured are included too.
#   -DSOURCE=<resources/factory>  -DOUTPUT=<Factory.zip>
file(GLOB_RECURSE files RELATIVE ${SOURCE} ${SOURCE}/*)
list(FILTER files EXCLUDE REGEX "(^|/)\\.")
list(SORT files)
file(REMOVE ${OUTPUT})
execute_process(COMMAND ${CMAKE_COMMAND} -E tar cf ${OUTPUT} --format=zip -- ${files}
                WORKING_DIRECTORY ${SOURCE}
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Couldn't zip the factory library")
endif()
