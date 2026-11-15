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

# Only run the compiler setup once to prevent duplicates.
if(DEFINED LUX_COMPILER_SETUP_DONE)
    return()
endif()
set(LUX_COMPILER_SETUP_DONE TRUE)

# Use the Intel compiler if available.
if(DEFINED ENV{ONEAPI_ROOT})
    set(INTEL_CC "$ENV{ONEAPI_ROOT}/compiler/latest/bin/icx")
    set(INTEL_CXX "$ENV{ONEAPI_ROOT}/compiler/latest/bin/icpx")
    set(LLD_LINKER "$ENV{ONEAPI_ROOT}/compiler/latest/bin/lld")
else()
    find_program(INTEL_CC NAMES icx)
    find_program(INTEL_CXX NAMES icpx)
    find_program(LLD_LINKER NAMES lld) # Only look globally if Intel env isn't set.
endif()

# Look for Clang if Intel wasn't found.
if(NOT INTEL_CC)
    find_program(CLANG_CC NAMES clang)
    find_program(CLANG_CXX NAMES clang++)
    if(NOT LLD_LINKER)
        find_program(LLD_LINKER NAMES lld)
    endif()
endif()

if(INTEL_CC AND INTEL_CXX)
    set(CMAKE_C_COMPILER ${INTEL_CC})
    set(CMAKE_CXX_COMPILER ${INTEL_CXX})

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIE")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIE")

    # Bake linker fixes into the LLD call rule.
    set(CMAKE_C_USING_LINKER_LLD "-fuse-ld=lld -pie -Wl,--ignore-data-address-equality")
    set(CMAKE_CXX_USING_LINKER_LLD "-fuse-ld=lld -pie -Wl,--ignore-data-address-equality")
    set(CMAKE_LINKER_TYPE LLD)

    message(STATUS "Using Intel OneAPI C: ${CMAKE_C_COMPILER}")
    message(STATUS "Using Intel OneAPI CXX: ${CMAKE_CXX_COMPILER}")
    message(STATUS "Using Intel OneAPI LLD: ${LLD_LINKER}")
elseif(CLANG_CC AND CLANG_CXX AND LLD_LINKER)
    set(CMAKE_C_COMPILER ${CLANG_CC})
    set(CMAKE_CXX_COMPILER ${CLANG_CXX})

	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIE")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIE")

    # Bake linker fixes into the LLD call rule.
    set(CMAKE_C_USING_LINKER_LLD "-fuse-ld=lld -pie -Wl,--ignore-data-address-equality")
    set(CMAKE_CXX_USING_LINKER_LLD "-fuse-ld=lld -pie -Wl,--ignore-data-address-equality")

    set(CMAKE_LINKER_TYPE LLD)
    message(STATUS "Using LLVM Clang: ${CMAKE_C_COMPILER}")
else()
    message(STATUS "Using GCC")
endif()

##############################
# Set the default build type #
##############################

# CMake needs to know which configuration to generate by default.
IF (NOT CMAKE_BUILD_TYPE)
	SET(CMAKE_BUILD_TYPE Release)
ENDIF(NOT CMAKE_BUILD_TYPE)

MESSAGE(STATUS "Building mode: " ${CMAKE_BUILD_TYPE})
SET(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "assure config" FORCE) # Makes sure type is shown in cmake gui.
