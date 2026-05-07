################################################################################
# Copyright 1998-2015 by authors (see AUTHORS.txt)
#
#   This file is part of LuxRender.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

# This covers GCC, Apple Clang, and LLVM Clang.
IF(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")

    # Look for the highest supported AVX version.
    FIND_PACKAGE(AVX)

    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-long-long -pedantic -fPIC")

    IF(AVX_FOUND)
        MESSAGE(STATUS "AVX ${AVX_VERSION} detected (${AVX_STR}). Adding compiler definitions.")
        
        # Use the flags determined by the FindAVX script.
        # This will include -mavx512f if on an AVX-512 machine,
        # -mavx2 on an AVX2 machine, or -mavx on an AVX machine.
        ADD_DEFINITIONS(${AVX_FLAGS})

    ELSE()
        MESSAGE(STATUS "No AVX support. Reverting to SSE2.")
        ADD_DEFINITIONS(-msse2 -mfpmath=sse -march=nocona)
    ENDIF()

    SET(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")

    # Note: removed 'fvariable-expansion-in-unroller' as it's GCC-specific.
    SET(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3 -funsafe-math-optimizations -ftree-vectorize -funroll-loops")

ENDIF()

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,$ORIGIN")
    # ... keep your existing Linux export map logic here ...

ELSEIF(APPLE)
    # All binaries should resolve dependencies from the Frameworks folder.
    set(CMAKE_EXE_LINKER_FLAGS
        "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,@executable_path/../Frameworks"
    )

    set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-rpath,@loader_path/../Frameworks"
    )

    set(CMAKE_MODULE_LINKER_FLAGS
        "${CMAKE_MODULE_LINKER_FLAGS} -Wl,-rpath,@loader_path/../Frameworks"
    )
ENDIF()