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

    # Standard warning and position-independent code flags.
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-long-long -pedantic -fPIC")

    # SIMD / Architecture Logic.
    # Note: On Apple Silicon (M1/M2/M3), -mavx2 will FAIL.
    # We only want this for Intel Macs.
    IF(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        FIND_PACKAGE(AVX 2.0 EXACT)
        IF(AVX_FOUND)
            MESSAGE(STATUS "AVX2 detected. Adding compiler definitions.")
            ADD_DEFINITIONS(-mavx2 -march=haswell)
        ELSE()
            MESSAGE(STATUS "No AVX2 support. Reverting to SSE2")
            ADD_DEFINITIONS(-msse2 -mfpmath=sse -march=nocona)
        ENDIF()
    ENDIF()

    SET(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")

    # Note: removed 'fvariable-expansion-in-unroller' as it's GCC-specific.
    SET(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3 -funsafe-math-optimizations -ftree-vectorize -funroll-loops")

ENDIF()

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,$ORIGIN")
    # ... keep your existing Linux export map logic here ...

ELSEIF(APPLE)
    # 1. @loader_path: For dylibs to find other dylibs in the same folder.
    # 2. @executable_path: For the app to find dylibs in the SAME folder (MacOS).
    # 3. @executable_path/../Frameworks: For finding the Qt Frameworks macdeployqt installs.
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,@loader_path -Wl,-rpath,@executable_path -Wl,-rpath,@executable_path/../Frameworks")
    
    SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-rpath,@loader_path")
    SET(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,-rpath,@loader_path")

ENDIF()
