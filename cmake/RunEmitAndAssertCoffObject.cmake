if(NOT DEFINED COMMAND_PATH)
    message(FATAL_ERROR "COMMAND_PATH is required")
endif()

if(NOT DEFINED INPUT_PATH)
    message(FATAL_ERROR "INPUT_PATH is required")
endif()

if(NOT DEFINED OUTPUT_PATH)
    message(FATAL_ERROR "OUTPUT_PATH is required")
endif()

if(NOT DEFINED TARGET_TRIPLE)
    set(TARGET_TRIPLE x86_64-pc-windows-msvc)
endif()

if(NOT DEFINED EXPECTED_MACHINE_HEX)
    set(EXPECTED_MACHINE_HEX "6486")
endif()

if(NOT DEFINED EXPECTED_OPTIONAL_HEADER_SIZE_HEX)
    set(EXPECTED_OPTIONAL_HEADER_SIZE_HEX "0000")
endif()

if(NOT DEFINED EXPECTED_CHARACTERISTICS_HEX)
    set(EXPECTED_CHARACTERISTICS_HEX "0000")
endif()

if(NOT DEFINED EXPECTED_FIRST_SECTION_NAME_HEX)
    set(EXPECTED_FIRST_SECTION_NAME_HEX "2e74657874000000")
endif()

if(NOT DEFINED EXPECTED_FIRST_SECTION_CHARACTERISTICS_HEX)
    set(EXPECTED_FIRST_SECTION_CHARACTERISTICS_HEX "20005060")
endif()

function(read_u16le_hex input_hex output_var)
    string(LENGTH "${input_hex}" input_len)
    if(NOT input_len EQUAL 4)
        message(FATAL_ERROR "internal test error: expected 2 bytes of hex, got '${input_hex}'")
    endif()
    string(SUBSTRING "${input_hex}" 2 2 b1)
    string(SUBSTRING "${input_hex}" 0 2 b0)
    math(EXPR value "0x${b1}${b0}")
    set(${output_var} "${value}" PARENT_SCOPE)
endfunction()

function(read_u32le_hex input_hex output_var)
    string(LENGTH "${input_hex}" input_len)
    if(NOT input_len EQUAL 8)
        message(FATAL_ERROR "internal test error: expected 4 bytes of hex, got '${input_hex}'")
    endif()
    string(SUBSTRING "${input_hex}" 6 2 b3)
    string(SUBSTRING "${input_hex}" 4 2 b2)
    string(SUBSTRING "${input_hex}" 2 2 b1)
    string(SUBSTRING "${input_hex}" 0 2 b0)
    math(EXPR value "0x${b3}${b2}${b1}${b0}")
    set(${output_var} "${value}" PARENT_SCOPE)
endfunction()

execute_process(
    COMMAND "${COMMAND_PATH}" "--target" "${TARGET_TRIPLE}" "--emit-obj" "${OUTPUT_PATH}" "${INPUT_PATH}"
    RESULT_VARIABLE command_result
    OUTPUT_VARIABLE command_stdout
    ERROR_VARIABLE command_stderr
)

if(NOT command_result EQUAL 0)
    message(FATAL_ERROR
        "compiler command failed with exit code ${command_result}\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

if(NOT command_stdout STREQUAL "")
    message(FATAL_ERROR
        "compiler command wrote unexpected stdout for ${INPUT_PATH}\nstdout:\n${command_stdout}")
endif()

if(NOT command_stderr STREQUAL "")
    message(FATAL_ERROR
        "compiler command wrote unexpected stderr for ${INPUT_PATH}\nstderr:\n${command_stderr}")
endif()

if(NOT EXISTS "${OUTPUT_PATH}")
    message(FATAL_ERROR "expected emitted object file does not exist: ${OUTPUT_PATH}")
endif()

file(SIZE "${OUTPUT_PATH}" output_size)
if(output_size LESS 20)
    message(FATAL_ERROR "expected emitted COFF object to be at least 20 bytes: ${OUTPUT_PATH}")
endif()

file(READ "${OUTPUT_PATH}" machine_hex OFFSET 0 LIMIT 2 HEX)
if(NOT machine_hex STREQUAL EXPECTED_MACHINE_HEX)
    message(FATAL_ERROR
        "COFF machine mismatch for ${OUTPUT_PATH}\nexpected: ${EXPECTED_MACHINE_HEX}\nactual: ${machine_hex}")
endif()

file(READ "${OUTPUT_PATH}" section_count_hex OFFSET 2 LIMIT 2 HEX)
read_u16le_hex("${section_count_hex}" section_count)
if(section_count LESS 1)
    message(FATAL_ERROR "COFF object has no sections: ${OUTPUT_PATH}")
endif()

file(READ "${OUTPUT_PATH}" symbol_table_pointer_hex OFFSET 8 LIMIT 4 HEX)
read_u32le_hex("${symbol_table_pointer_hex}" symbol_table_pointer)

file(READ "${OUTPUT_PATH}" symbol_count_hex OFFSET 12 LIMIT 4 HEX)
read_u32le_hex("${symbol_count_hex}" symbol_count)
if(symbol_count LESS 1)
    message(FATAL_ERROR "COFF object has no symbol-table records: ${OUTPUT_PATH}")
endif()
if(symbol_table_pointer LESS 20)
    message(FATAL_ERROR "COFF object symbol table starts before the header: ${OUTPUT_PATH}")
endif()
math(EXPR symbol_table_end "${symbol_table_pointer} + (${symbol_count} * 18)")
if(output_size LESS symbol_table_end)
    message(FATAL_ERROR "COFF object symbol table extends beyond emitted file size: ${OUTPUT_PATH}")
endif()
math(EXPR string_table_size_end "${symbol_table_end} + 4")
if(output_size LESS string_table_size_end)
    message(FATAL_ERROR "COFF object is missing the string-table size field after the symbol table: ${OUTPUT_PATH}")
endif()

file(READ "${OUTPUT_PATH}" string_table_size_hex OFFSET ${symbol_table_end} LIMIT 4 HEX)
read_u32le_hex("${string_table_size_hex}" string_table_size)
if(string_table_size LESS 4)
    message(FATAL_ERROR "COFF object string-table size is smaller than the size field: ${OUTPUT_PATH}")
endif()
math(EXPR string_table_end "${symbol_table_end} + ${string_table_size}")
if(output_size LESS string_table_end)
    message(FATAL_ERROR "COFF object string table extends beyond emitted file size: ${OUTPUT_PATH}")
endif()

file(READ "${OUTPUT_PATH}" optional_header_size_hex OFFSET 16 LIMIT 2 HEX)
if(NOT optional_header_size_hex STREQUAL EXPECTED_OPTIONAL_HEADER_SIZE_HEX)
    message(FATAL_ERROR
        "COFF optional-header size mismatch for ${OUTPUT_PATH}\nexpected: ${EXPECTED_OPTIONAL_HEADER_SIZE_HEX}\nactual: ${optional_header_size_hex}")
endif()
read_u16le_hex("${optional_header_size_hex}" optional_header_size)

file(READ "${OUTPUT_PATH}" characteristics_hex OFFSET 18 LIMIT 2 HEX)
if(NOT characteristics_hex STREQUAL EXPECTED_CHARACTERISTICS_HEX)
    message(FATAL_ERROR
        "COFF characteristics mismatch for ${OUTPUT_PATH}\nexpected: ${EXPECTED_CHARACTERISTICS_HEX}\nactual: ${characteristics_hex}")
endif()

math(EXPR section_table_offset "20 + ${optional_header_size}")
math(EXPR section_table_end "${section_table_offset} + (${section_count} * 40)")
if(output_size LESS section_table_end)
    message(FATAL_ERROR "COFF section table extends beyond emitted file size: ${OUTPUT_PATH}")
endif()
if(symbol_table_pointer LESS section_table_end)
    message(FATAL_ERROR "COFF symbol table starts before the end of the section table: ${OUTPUT_PATH}")
endif()

math(EXPR last_section_index "${section_count} - 1")
foreach(section_index RANGE 0 ${last_section_index})
    math(EXPR section_offset "${section_table_offset} + (${section_index} * 40)")

    file(READ "${OUTPUT_PATH}" section_name_hex OFFSET ${section_offset} LIMIT 8 HEX)
    if(section_name_hex STREQUAL "0000000000000000")
        message(FATAL_ERROR "COFF section ${section_index} has an empty name: ${OUTPUT_PATH}")
    endif()

    math(EXPR section_raw_size_offset "${section_offset} + 16")
    file(READ "${OUTPUT_PATH}" section_raw_size_hex OFFSET ${section_raw_size_offset} LIMIT 4 HEX)
    read_u32le_hex("${section_raw_size_hex}" section_raw_size)

    math(EXPR section_raw_pointer_offset "${section_offset} + 20")
    file(READ "${OUTPUT_PATH}" section_raw_pointer_hex OFFSET ${section_raw_pointer_offset} LIMIT 4 HEX)
    read_u32le_hex("${section_raw_pointer_hex}" section_raw_pointer)

    if(section_raw_size GREATER 0)
        if(section_raw_pointer LESS section_table_end)
            message(FATAL_ERROR "COFF section ${section_index} raw data overlaps the section table: ${OUTPUT_PATH}")
        endif()

        math(EXPR section_raw_end "${section_raw_pointer} + ${section_raw_size}")
        if(output_size LESS section_raw_end)
            message(FATAL_ERROR "COFF section ${section_index} raw data extends beyond emitted file size: ${OUTPUT_PATH}")
        endif()
        if(symbol_table_pointer LESS section_raw_end)
            message(FATAL_ERROR "COFF section ${section_index} raw data overlaps the symbol table: ${OUTPUT_PATH}")
        endif()

        if(section_index GREATER 0)
            math(EXPR previous_last_section_index "${section_index} - 1")
            foreach(previous_section_index RANGE 0 ${previous_last_section_index})
                math(EXPR previous_section_offset "${section_table_offset} + (${previous_section_index} * 40)")

                math(EXPR previous_section_raw_size_offset "${previous_section_offset} + 16")
                file(READ "${OUTPUT_PATH}" previous_section_raw_size_hex OFFSET ${previous_section_raw_size_offset} LIMIT 4 HEX)
                read_u32le_hex("${previous_section_raw_size_hex}" previous_section_raw_size)

                if(previous_section_raw_size GREATER 0)
                    math(EXPR previous_section_raw_pointer_offset "${previous_section_offset} + 20")
                    file(READ "${OUTPUT_PATH}" previous_section_raw_pointer_hex OFFSET ${previous_section_raw_pointer_offset} LIMIT 4 HEX)
                    read_u32le_hex("${previous_section_raw_pointer_hex}" previous_section_raw_pointer)
                    math(EXPR previous_section_raw_end "${previous_section_raw_pointer} + ${previous_section_raw_size}")

                    if(section_raw_pointer LESS previous_section_raw_end AND previous_section_raw_pointer LESS section_raw_end)
                        message(FATAL_ERROR
                            "COFF section raw data ranges overlap in ${OUTPUT_PATH}: section ${previous_section_index} and section ${section_index}")
                    endif()
                endif()
            endforeach()
        endif()
    endif()

    math(EXPR section_relocation_pointer_offset "${section_offset} + 24")
    file(READ "${OUTPUT_PATH}" section_relocation_pointer_hex OFFSET ${section_relocation_pointer_offset} LIMIT 4 HEX)
    read_u32le_hex("${section_relocation_pointer_hex}" section_relocation_pointer)

    math(EXPR section_relocation_count_offset "${section_offset} + 32")
    file(READ "${OUTPUT_PATH}" section_relocation_count_hex OFFSET ${section_relocation_count_offset} LIMIT 2 HEX)
    read_u16le_hex("${section_relocation_count_hex}" section_relocation_count)

    if(section_relocation_count GREATER 0)
        if(section_relocation_pointer LESS section_table_end)
            message(FATAL_ERROR "COFF section ${section_index} relocation table overlaps the section table: ${OUTPUT_PATH}")
        endif()

        math(EXPR section_relocation_end "${section_relocation_pointer} + (${section_relocation_count} * 10)")
        if(output_size LESS section_relocation_end)
            message(FATAL_ERROR "COFF section ${section_index} relocation table extends beyond emitted file size: ${OUTPUT_PATH}")
        endif()
        if(symbol_table_pointer LESS section_relocation_end)
            message(FATAL_ERROR "COFF section ${section_index} relocation table overlaps the symbol table: ${OUTPUT_PATH}")
        endif()
    endif()

    math(EXPR section_line_number_pointer_offset "${section_offset} + 28")
    file(READ "${OUTPUT_PATH}" section_line_number_pointer_hex OFFSET ${section_line_number_pointer_offset} LIMIT 4 HEX)
    read_u32le_hex("${section_line_number_pointer_hex}" section_line_number_pointer)

    math(EXPR section_line_number_count_offset "${section_offset} + 34")
    file(READ "${OUTPUT_PATH}" section_line_number_count_hex OFFSET ${section_line_number_count_offset} LIMIT 2 HEX)
    read_u16le_hex("${section_line_number_count_hex}" section_line_number_count)

    if(section_line_number_count GREATER 0)
        if(section_line_number_pointer LESS section_table_end)
            message(FATAL_ERROR "COFF section ${section_index} line-number table overlaps the section table: ${OUTPUT_PATH}")
        endif()

        math(EXPR section_line_number_end "${section_line_number_pointer} + (${section_line_number_count} * 6)")
        if(output_size LESS section_line_number_end)
            message(FATAL_ERROR "COFF section ${section_index} line-number table extends beyond emitted file size: ${OUTPUT_PATH}")
        endif()
        if(symbol_table_pointer LESS section_line_number_end)
            message(FATAL_ERROR "COFF section ${section_index} line-number table overlaps the symbol table: ${OUTPUT_PATH}")
        endif()
    endif()
endforeach()

file(READ "${OUTPUT_PATH}" first_section_name_hex OFFSET ${section_table_offset} LIMIT 8 HEX)
if(NOT first_section_name_hex STREQUAL EXPECTED_FIRST_SECTION_NAME_HEX)
    message(FATAL_ERROR
        "COFF first-section name mismatch for ${OUTPUT_PATH}\nexpected: ${EXPECTED_FIRST_SECTION_NAME_HEX}\nactual: ${first_section_name_hex}")
endif()

math(EXPR first_section_raw_size_offset "${section_table_offset} + 16")
file(READ "${OUTPUT_PATH}" first_section_raw_size_hex OFFSET ${first_section_raw_size_offset} LIMIT 4 HEX)
read_u32le_hex("${first_section_raw_size_hex}" first_section_raw_size)
if(first_section_raw_size LESS 1)
    message(FATAL_ERROR "COFF first .text section has no raw data: ${OUTPUT_PATH}")
endif()

math(EXPR first_section_raw_pointer_offset "${section_table_offset} + 20")
file(READ "${OUTPUT_PATH}" first_section_raw_pointer_hex OFFSET ${first_section_raw_pointer_offset} LIMIT 4 HEX)
read_u32le_hex("${first_section_raw_pointer_hex}" first_section_raw_pointer)
if(first_section_raw_pointer LESS section_table_end)
    message(FATAL_ERROR "COFF first .text section raw data overlaps the section table: ${OUTPUT_PATH}")
endif()
math(EXPR first_section_raw_end "${first_section_raw_pointer} + ${first_section_raw_size}")
if(output_size LESS first_section_raw_end)
    message(FATAL_ERROR "COFF first .text section raw data extends beyond emitted file size: ${OUTPUT_PATH}")
endif()

math(EXPR first_section_characteristics_offset "${section_table_offset} + 36")
file(READ "${OUTPUT_PATH}" first_section_characteristics_hex OFFSET ${first_section_characteristics_offset} LIMIT 4 HEX)
if(NOT first_section_characteristics_hex STREQUAL EXPECTED_FIRST_SECTION_CHARACTERISTICS_HEX)
    message(FATAL_ERROR
        "COFF first .text section characteristics mismatch for ${OUTPUT_PATH}\nexpected: ${EXPECTED_FIRST_SECTION_CHARACTERISTICS_HEX}\nactual: ${first_section_characteristics_hex}")
endif()
