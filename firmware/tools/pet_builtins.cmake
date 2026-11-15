# Generates pet_builtins.h and pet_builtins.c (the packs of pets/builtin.txt)
# into OUT_DIR with tools/petpack.py at configure time, so every component
# sees the header before anything compiles. A change to the list, a listed
# source or the tool re-runs the configure step.
function(devotchi_pet_builtins PYTHON OUT_DIR)
    set(tool "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/petpack.py")
    get_filename_component(pets_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../pets" ABSOLUTE)
    file(STRINGS "${pets_dir}/builtin.txt" lines)
    set(deps "${tool}" "${pets_dir}/builtin.txt")
    foreach(line ${lines})
        string(REGEX REPLACE "#.*" "" id "${line}")
        string(STRIP "${id}" id)
        if(id)
            list(APPEND deps "${pets_dir}/${id}.pet.json")
        endif()
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${deps})
    execute_process(COMMAND "${PYTHON}" -I "${tool}" builtins "${pets_dir}" "${OUT_DIR}"
                    RESULT_VARIABLE rc ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "petpack.py builtins failed: ${err}")
    endif()
endfunction()
