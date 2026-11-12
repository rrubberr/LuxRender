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

if(DEFINED LUX_HOST_ISA_DONE)
    return()
endif()
set(LUX_HOST_ISA_DONE TRUE)

include(CheckCXXSourceCompiles)
include(CheckCXXCompilerFlag)

# Probe against -march=native.
set(CMAKE_REQUIRED_FLAGS "-march=native")
check_cxx_source_compiles("#if !defined(__AVX512F__)\n#error\n#endif\nint main(){}" HOST_HAS_AVX512)
check_cxx_source_compiles("#if !defined(__AVX2__)\n#error\n#endif\nint main(){}" HOST_HAS_AVX2)
check_cxx_source_compiles("#if !defined(__SSE4_2__)\n#error\n#endif\nint main(){}" HOST_HAS_SSE42)
unset(CMAKE_REQUIRED_FLAGS)

# -mprefer-vector-width is a Clang/ICX option; GCC rejects.
set(CMAKE_REQUIRED_FLAGS "-march=native")
check_cxx_compiler_flag("-mprefer-vector-width=512" COMPILER_SUPPORTS_PREFER_VECTOR_WIDTH)
unset(CMAKE_REQUIRED_FLAGS)

# Pin Embree to a single kernel set.
if(HOST_HAS_AVX512)
    set(LUX_PACKET_WIDTH 16 CACHE STRING "SIMD lanes in lux2 SoA packets" FORCE)
    set(LUX_MARCH_FLAGS "-march=skylake-avx512 -mtune=native" CACHE STRING
        "lux2/Embree ISA codegen flags derived from the host CPU" FORCE)
    set(LUX_EMBREE_ISA_NAME "AVX512" CACHE STRING "Embree ISA to build" FORCE)
elseif(HOST_HAS_AVX2)
    set(LUX_PACKET_WIDTH 8 CACHE STRING "SIMD lanes in lux2 SoA packets" FORCE)
    set(LUX_MARCH_FLAGS "-march=haswell -mtune=native" CACHE STRING
        "lux2/Embree ISA codegen flags derived from the host CPU" FORCE)
    set(LUX_EMBREE_ISA_NAME "AVX2" CACHE STRING "Embree ISA to build" FORCE)
elseif(HOST_HAS_SSE42)
    set(LUX_PACKET_WIDTH 4 CACHE STRING "SIMD lanes in lux2 SoA packets" FORCE)
    set(LUX_MARCH_FLAGS "-march=nehalem -mtune=native" CACHE STRING
        "lux2/Embree ISA codegen flags derived from the host CPU" FORCE)
    set(LUX_EMBREE_ISA_NAME "SSE4.2" CACHE STRING "Embree ISA to build" FORCE)
else()
    set(LUX_PACKET_WIDTH 1 CACHE STRING "SIMD lanes in lux2 SoA packets" FORCE)
    set(LUX_MARCH_FLAGS "-march=native" CACHE STRING
        "lux2/Embree ISA codegen flags derived from the host CPU" FORCE)
    set(LUX_EMBREE_ISA_NAME "SSE2" CACHE STRING "Embree ISA to build" FORCE)
endif()

set(LUX_EMBREE_ISA_SSE2   OFF)
set(LUX_EMBREE_ISA_SSE42  OFF)
set(LUX_EMBREE_ISA_AVX    OFF)
set(LUX_EMBREE_ISA_AVX2   OFF)
set(LUX_EMBREE_ISA_AVX512 OFF)

if(LUX_EMBREE_ISA_NAME STREQUAL "SSE2")
    set(LUX_EMBREE_ISA_SSE2 ON)
elseif(LUX_EMBREE_ISA_NAME STREQUAL "SSE4.2")
    set(LUX_EMBREE_ISA_SSE42 ON)
elseif(LUX_EMBREE_ISA_NAME STREQUAL "AVX2")
    set(LUX_EMBREE_ISA_AVX2 ON)
elseif(LUX_EMBREE_ISA_NAME STREQUAL "AVX512")
    set(LUX_EMBREE_ISA_AVX512 ON)
endif()

set(LUX_EMBREE_ISA_ARGS
    -DEMBREE_MAX_ISA=NONE
    -DEMBREE_ISA_SSE2=${LUX_EMBREE_ISA_SSE2}
    -DEMBREE_ISA_SSE42=${LUX_EMBREE_ISA_SSE42}
    -DEMBREE_ISA_AVX=${LUX_EMBREE_ISA_AVX}
    -DEMBREE_ISA_AVX2=${LUX_EMBREE_ISA_AVX2}
    -DEMBREE_ISA_AVX512=${LUX_EMBREE_ISA_AVX512}
)

if(COMPILER_SUPPORTS_PREFER_VECTOR_WIDTH)
    if(LUX_PACKET_WIDTH EQUAL 16)
        string(APPEND LUX_MARCH_FLAGS " -mprefer-vector-width=512")
    elseif(LUX_PACKET_WIDTH EQUAL 8)
        string(APPEND LUX_MARCH_FLAGS " -mprefer-vector-width=256")
    endif()
endif()

message(STATUS "LuxRender host ISA: PACKET_WIDTH=${LUX_PACKET_WIDTH}, "
    "lux2 flags='${LUX_MARCH_FLAGS}', Embree ISA=${LUX_EMBREE_ISA_NAME} (single kernel)")
