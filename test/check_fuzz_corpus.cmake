if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

# LLVMFuzzerTestOneInput consumes the first byte as its source-form selector.
# Repository seeds use ASCII 0/1 (free/fixed); evolved inputs may use any byte.
file(GLOB CORPUS_FILES LIST_DIRECTORIES FALSE "${SOURCE_DIR}/test/fuzz_corpus/*")
if(NOT CORPUS_FILES)
    message(FATAL_ERROR "the versioned fuzz corpus is empty")
endif()
foreach(CORPUS_FILE IN LISTS CORPUS_FILES)
    file(READ "${CORPUS_FILE}" SELECTOR LIMIT 1 HEX)
    if(NOT SELECTOR STREQUAL "30" AND NOT SELECTOR STREQUAL "31")
        file(RELATIVE_PATH RELATIVE_FILE "${SOURCE_DIR}" "${CORPUS_FILE}")
        message(FATAL_ERROR
            "${RELATIVE_FILE} must start with the 0/1 source-form selector byte")
    endif()
endforeach()
