# MIT License
# Copyright (c) 2020 ipc-sim
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# This script checks for the highest level of AVX support on the host
# by compiling and running small C++ programs that use AVX intrinsics.
#
# You can invoke this module using the following command:
#
#   FIND_PACKAGE(AVX [major[.minor]] [EXACT] [QUIET|REQUIRED])
#
# where the version string is one of:
#
#   1.0 for AVX support
#   2.0 for AVX2 support
#   3.0 for AVX-512 support
#
set(AVX_FLAGS)
set(AVX_FOUND)
set(DETECTED_AVX_10)
set(DETECTED_AVX_20)
set(DETECTED_AVX_512)

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
  execute_process(COMMAND ${CMAKE_CXX_COMPILER} "-dumpversion" OUTPUT_VARIABLE GCC_VERSION_STRING)
  if(GCC_VERSION_STRING VERSION_GREATER 4.2 AND NOT APPLE AND NOT CMAKE_CROSSCOMPILING)
    SET(AVX_FLAGS "${AVX_FLAGS} -march=native")
  endif()
endif()

include(CheckCXXSourceRuns)
set(CMAKE_REQUIRED_FLAGS)

# Generate a list of AVX versions to test.

set(_AVX_TEST_512 1)
set(_AVX_TEST_20 1)
set(_AVX_TEST_10 1)

# Check for AVX-512 support.
if(_AVX_TEST_512)
  if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_REQUIRED_FLAGS "-mavx512f")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Intel")
    set(CMAKE_REQUIRED_FLAGS "-xCORE-AVX512")
  elseif(MSVC AND NOT CMAKE_CL_64)
    set(CMAKE_REQUIRED_FLAGS "/arch:AVX512")
  endif()
  check_cxx_source_runs("
  #include <immintrin.h>
  int main()
    {
    __m512 a = _mm512_setzero_ps();
    __m512 b = _mm512_setzero_ps();
    __m512 result = _mm512_add_ps(a, b);
    return 0;
    }" DETECTED_AVX_512)
endif()

# Check for AVX2 support.
if(_AVX_TEST_20)
  if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_REQUIRED_FLAGS "-mavx2")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Intel")
    set(CMAKE_REQUIRED_FLAGS "-xHost")
  elseif(MSVC AND NOT CMAKE_CL_64)
    set(CMAKE_REQUIRED_FLAGS "/arch:AVX2")
  endif()
  check_cxx_source_runs("
  #include <immintrin.h>
  int main()
    {
    __m256i a = _mm256_set_epi32 (-1, 2, -3, 4, -1, 2, -3, 4);
    __m256i result = _mm256_abs_epi32 (a);
    return 0;
    }" DETECTED_AVX_20)
endif()

# Check for AVX support.
if(_AVX_TEST_10)
  if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_REQUIRED_FLAGS "-mavx")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Intel")
    set(CMAKE_REQUIRED_FLAGS "-xHost")
  elseif(MSVC AND NOT CMAKE_CL_64)
    set(CMAKE_REQUIRED_FLAGS "/arch:AVX")
  endif()
  check_cxx_source_runs("
  #include <immintrin.h>
  int main()
    {
    __m256 a = _mm256_set_ps (-1.0f, 2.0f, -3.0f, 4.0f, -1.0f, 2.0f, -3.0f, 4.0f);
    __m256 b = _mm256_set_ps (1.0f, 2.0f, 3.0f, 4.0f, 1.0f, 2.0f, 3.0f, 4.0f);
    __m256 result = _mm256_add_ps (a, b);
    return 0;
    }" DETECTED_AVX_10)
endif()

set(CMAKE_REQUIRED_FLAGS)

# Set final variables based on highest detected version.
if(DETECTED_AVX_512)
  set(AVX_VERSION "3.0")
  set(AVX_STR "30")
  set(AVX_FOUND 1)
elseif(DETECTED_AVX_20)
  set(AVX_VERSION "2.0")
  set(AVX_STR "20")
  set(AVX_FOUND 1)
elseif(DETECTED_AVX_10)
  set(AVX_VERSION "1.0")
  set(AVX_STR "10")
  set(AVX_FOUND 1)
endif()

if(AVX_FOUND)
  message(STATUS "Found AVX ${AVX_VERSION} extensions.")
else()
  message(STATUS "No AVX support found.")
  set(AVX_FLAGS "")
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${AVX_FLAGS}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${AVX_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${AVX_FLAGS}")
