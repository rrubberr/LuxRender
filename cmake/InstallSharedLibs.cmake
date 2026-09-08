###########################################################################
#   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  #
#                                                                         #
#   This file is part of LuxRender.                                       #
#                                                                         #
#   LuxRender is free software; you can redistribute it and/or modify     #
#   it under the terms of the GNU General Public License as published by  #
#   the Free Software Foundation; either version 3 of the License, or     #
#   any later version.                                                    #
#                                                                         #
#   LuxRender is distributed in the hope that it will be useful,          #
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        #
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          #
#   GNU General Public License for more details.                          #
#                                                                         #
#   You should have received a copy of the GNU General Public License     #
#   along with this program. If not, see <http://www.gnu.org/licenses/>   #
#                                                                         #
#   This project is based on PBRT; see <http://www.pbrt.org>              #
###########################################################################

# Copies the shared runtime libraries (TBB, Embree) from LIB_DIR.

foreach(_required LIB_DIR RELEASE_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} must be defined when running ${CMAKE_CURRENT_LIST_FILE}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${RELEASE_DIR}")

# The exact runtime dependencies, by SONAME:
#   liblux.so        -> libembree4.so.4
#   libembree4.so.4  -> libtbb.so.12  (Release) or libtbb_debug.so.12 (Debug)
#   libtbbmalloc.so.2  (Release) or libtbbmalloc_debug.so.2 (Debug)
file(GLOB _runtime_shared_libs
    "${LIB_DIR}/libtbb.so.*"
    "${LIB_DIR}/libtbb_debug.so.*"
    "${LIB_DIR}/libtbbmalloc.so.*"
    "${LIB_DIR}/libtbbmalloc_debug.so.*"
    "${LIB_DIR}/libembree4.so.*")

if(NOT _runtime_shared_libs)
    message(WARNING
        "No TBB/Embree shared libraries found in ${LIB_DIR}; "
        "${RELEASE_DIR} will not be self-contained.")
endif()

find_program(READELF_EXECUTABLE NAMES readelf objdump)

foreach(_shared_lib ${_runtime_shared_libs})
    # Only the real library carries SONAME.
    if(IS_SYMLINK "${_shared_lib}")
        continue()
    endif()

    set(_soname "")
    if(READELF_EXECUTABLE)
        execute_process(COMMAND
            ${READELF_EXECUTABLE} -d "${_shared_lib}"
            OUTPUT_VARIABLE _dynamic_section
            ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX MATCH "[Ss]oname:?[ \t]*\\[([^]]+)\\]" _match "${_dynamic_section}")
        set(_soname "${CMAKE_MATCH_1}")
    endif()

    if(_soname)
        set(_dest "${RELEASE_DIR}/${_soname}")
    else()
        # No SONAME recorded; fall back to the file's own name.
        set(_dest "${RELEASE_DIR}/")
    endif()

    execute_process(COMMAND
        ${CMAKE_COMMAND} -E copy "${_shared_lib}" "${_dest}")
endforeach()
