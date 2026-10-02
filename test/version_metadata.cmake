if(NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR OR NOT DEFINED C_COMPILER
   OR NOT DEFINED BUILD_GENERATOR)
    message(FATAL_ERROR "version metadata test requires source, work, compiler, and generator")
endif()

# Exercise the actual root version prelude in an isolated project. Never rewrite the repository's
# version header while other compiler or CTest configurations may be using it.
file(READ "${SOURCE_DIR}/CMakeLists.txt" ROOT_CMAKE)
string(FIND "${ROOT_CMAKE}" "set(CMAKE_C_STANDARD 17)" PRELUDE_END)
if(PRELUDE_END LESS 0)
    message(FATAL_ERROR "cannot locate the root CMake version prelude")
endif()
string(SUBSTRING "${ROOT_CMAKE}" 0 ${PRELUDE_END} PRELUDE)
file(READ "${SOURCE_DIR}/include/f2c/version.h" ORIGINAL_VERSION)
string(REGEX MATCH "#define F2C_VERSION_STRING \"([0-9]+\\.[0-9]+\\.[0-9]+)\""
       VERSION_MATCH "${ORIGINAL_VERSION}")
set(EXPECTED_VERSION "${CMAKE_MATCH_1}")
if(NOT VERSION_MATCH)
    message(FATAL_ERROR "missing authoritative numeric version")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/project/include/f2c")
file(WRITE "${WORK_DIR}/project/CMakeLists.txt" "${PRELUDE}")
file(WRITE "${WORK_DIR}/project/include/f2c/version.h" "${ORIGINAL_VERSION}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${WORK_DIR}/project" -B "${WORK_DIR}/configured"
            -G "${BUILD_GENERATOR}" "-DCMAKE_C_COMPILER=${C_COMPILER}"
    RESULT_VARIABLE STATUS OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR
)
if(NOT STATUS EQUAL 0)
    message(FATAL_ERROR "initial version configure failed: ${OUTPUT}\n${ERROR}")
endif()
file(STRINGS "${WORK_DIR}/configured/CMakeCache.txt" CACHE_VERSION
     REGEX "^CMAKE_PROJECT_VERSION:STATIC=")
if(NOT CACHE_VERSION STREQUAL "CMAKE_PROJECT_VERSION:STATIC=${EXPECTED_VERSION}")
    message(FATAL_ERROR "initial CMake metadata does not match the authoritative header")
endif()

# Account for generators/filesystems that compare modification times at whole-second resolution.
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.1)
string(REGEX REPLACE "#define F2C_VERSION_STRING \"[0-9]+\\.[0-9]+\\.[0-9]+\""
       "#define F2C_VERSION_STRING \"9.8.7\"" NEXT_VERSION "${ORIGINAL_VERSION}")
file(WRITE "${WORK_DIR}/project/include/f2c/version.h" "${NEXT_VERSION}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${WORK_DIR}/configured" --config Release
    RESULT_VARIABLE STATUS OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR
)
if(NOT STATUS EQUAL 0)
    message(FATAL_ERROR "incremental version build failed: ${OUTPUT}\n${ERROR}")
endif()
file(STRINGS "${WORK_DIR}/configured/CMakeCache.txt" CACHE_VERSION
     REGEX "^CMAKE_PROJECT_VERSION:STATIC=")
if(NOT CACHE_VERSION STREQUAL "CMAKE_PROJECT_VERSION:STATIC=9.8.7")
    message(FATAL_ERROR "header-only version changes did not refresh CMake metadata")
endif()
