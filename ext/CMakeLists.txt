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

MESSAGE(STATUS "CMAKE VERSION DETECTED " ${CMAKE_VERSION})
cmake_minimum_required(VERSION 3.25)

###################################
# Set the compiler and build type #
###################################

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CompilerSetup.cmake)

##################
# Set up folders #
##################

set(EXT_DIR ${CMAKE_CURRENT_SOURCE_DIR})      # LuxRender/ext
set(BUILD_DIR ${EXT_DIR}/build)               # LuxRender/ext/build
set(BIN_DIR ${BUILD_DIR}/bin)
set(LIB_DIR ${BUILD_DIR}/lib)
set(INC_DIR ${BUILD_DIR}/include)
set(PFX_DIR ${EXT_DIR}/pfx)

######################
# Set up the project #
######################

project(LuxRenderDeps)
include(ExternalProject)

##############################
# Set the default build args #
##############################

file(READ "${EXT_DIR}/cpython/Include/patchlevel.h" _lux_patchlevel_h)
string(REGEX MATCH "#define[ \t]+PY_MAJOR_VERSION[ \t]+([0-9]+)" _ "" ${_lux_patchlevel_h})
set(LUX_PYTHON_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define[ \t]+PY_MINOR_VERSION[ \t]+([0-9]+)" _ "" ${_lux_patchlevel_h})
set(LUX_PYTHON_MINOR "${CMAKE_MATCH_1}")
set(LUX_PYTHON_INCLUDE_DIR "${INC_DIR}/python${LUX_PYTHON_MAJOR}.${LUX_PYTHON_MINOR}")
message(STATUS "Vendored CPython headers: ${LUX_PYTHON_INCLUDE_DIR}")

set(COMMON_EXT_PROJECT_ARGS
	-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
	-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
	-DCMAKE_CXX_STANDARD=17
	-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
	"-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} -I\"${LUX_PYTHON_INCLUDE_DIR}\" -w"
	"-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS} -w"
	-DCMAKE_POSITION_INDEPENDENT_CODE=TRUE
	-DCMAKE_INSTALL_PREFIX=${BUILD_DIR}
	-DCMAKE_INSTALL_LIBDIR=lib
	-DCMAKE_INSTALL_INCLUDEDIR=include
	-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
	-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
	-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
	-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY
	-DCMAKE_PREFIX_PATH=${BUILD_DIR}
)

# Values with spaces must go through CMAKE_CACHE_ARGS.
set(LUX_EXT_LINKER_FLAGS "")
if(CMAKE_LINKER_TYPE STREQUAL "LLD")
	set(LUX_EXT_LINKER_FLAGS "-Wl,--undefined-version")
endif()
set(COMMON_EXT_CACHE_ARGS
	"-DCMAKE_LINKER_TYPE:STRING=${CMAKE_LINKER_TYPE}"
	"-DCMAKE_C_USING_LINKER_LLD:STRING=${CMAKE_C_USING_LINKER_LLD}"
	"-DCMAKE_CXX_USING_LINKER_LLD:STRING=${CMAKE_CXX_USING_LINKER_LLD}"
	"-DCMAKE_SHARED_LINKER_FLAGS:STRING=${LUX_EXT_LINKER_FLAGS}"
	"-DCMAKE_MODULE_LINKER_FLAGS:STRING=${LUX_EXT_LINKER_FLAGS}"
	"-DCMAKE_EXE_LINKER_FLAGS:STRING=${LUX_EXT_LINKER_FLAGS}"
)

############################################
# Optional serialized builds for debugging #
############################################

# cmake -DLUX_EXT_SERIAL=ON ..
option(LUX_EXT_SERIAL "Build external projects one at a time" OFF)
function(lux_external_project_add TARGET_NAME)
	if(LUX_EXT_SERIAL AND DEFINED LUX_EXT_PREV_TARGET)
		ExternalProject_Add(${TARGET_NAME} ${ARGN} DEPENDS ${LUX_EXT_PREV_TARGET})
	else()
		ExternalProject_Add(${TARGET_NAME} ${ARGN})
	endif()
	if(LUX_EXT_SERIAL)
		set(LUX_EXT_PREV_TARGET ${TARGET_NAME} PARENT_SCOPE)
	endif()
endfunction()
if(LUX_EXT_SERIAL)
	message(STATUS "LuxRender ext: serialized external project builds ENABLED")
endif()

# lux_ext(<name> [SOURCE_DIR d] [URL u] [DEPENDS d...] [CACHE_ARGS a...] [ARGS a...])
# Emits: SOURCE_DIR|URL, PREFIX ${PFX_DIR}/<name>, INSTALL_DIR ${BUILD_DIR},
# CMAKE_ARGS ${COMMON_EXT_PROJECT_ARGS} ${ARGS}.
function(lux_ext NAME)
	cmake_parse_arguments(PARSE_ARGV 1 LX "" "SOURCE_DIR;URL" "DEPENDS;ARGS;CACHE_ARGS")
	if(LX_URL)
		set(_src URL ${LX_URL})
	elseif(LX_SOURCE_DIR)
		set(_src SOURCE_DIR ${LX_SOURCE_DIR})
	else()
		set(_src SOURCE_DIR ${EXT_DIR}/${NAME})
	endif()
	set(_args ${_src} PREFIX ${PFX_DIR}/${NAME} INSTALL_DIR ${BUILD_DIR}
		CMAKE_ARGS ${COMMON_EXT_PROJECT_ARGS} ${LX_ARGS})
	list(APPEND LX_DEPENDS cpython)
	list(APPEND _args DEPENDS ${LX_DEPENDS})
	list(APPEND LX_CACHE_ARGS ${COMMON_EXT_CACHE_ARGS})
	list(APPEND _args CMAKE_CACHE_ARGS ${LX_CACHE_ARGS})
	lux_external_project_add(${NAME} ${_args})
endfunction()

#################
# Build cpython #
#################

lux_external_project_add(cpython
	SOURCE_DIR ${EXT_DIR}/cpython PREFIX ${PFX_DIR}/cpython INSTALL_DIR ${BUILD_DIR}
	CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
		CC=gcc CXX=g++
		${EXT_DIR}/cpython/configure --prefix=${BUILD_DIR}
		--disable-shared --with-ensurepip=no --disable-test-modules
		CFLAGS=-O2\ -w\ -fPIC
		CXXFLAGS=-O2\ -w\ -fPIC\ -std=c++17
	BUILD_COMMAND make INSTALL_COMMAND make install)

###################
# Argument Arrays #
###################

lux_ext(zlib-ng      ARGS -DZLIB_COMPAT=TRUE -DBUILD_TESTING=FALSE -DBUILD_SHARED_LIBS=FALSE)
lux_ext(bzip2        ARGS -DENABLE_LIB_ONLY=ON -DENABLE_APP=OFF -DENABLE_EXAMPLES=OFF -DENABLE_TESTS=OFF -DENABLE_DOCS=OFF)
lux_ext(xz)
lux_ext(robin-map    ARGS -DTSL_ROBIN_MAP_ENABLE_INSTALL=TRUE)
lux_ext(yaml-cpp     ARGS -DYAML_CPP_BUILD_CONTRIB=FALSE -DYAML_CPP_BUILD_TOOLS=FALSE -DYAML_BUILD_SHARED_LIBS=FALSE)
lux_ext(pybind11     DEPENDS cpython ARGS -DPYBIND11_TEST=FALSE)
lux_ext(pystring     ARGS -DBUILD_SHARED_LIBS=FALSE)
lux_ext(minizip-ng   DEPENDS zlib-ng bzip2 xz
	ARGS -DMZ_ZLIB_FLAVOR=zlib -DMZ_PKCRYPT=FALSE -DMZ_WZAES=FALSE -DMZ_OPENSSL=FALSE
		-DMZ_COMPAT=TRUE -DMZ_ZLIB=TRUE -DMZ_BZIP2=FALSE -DMZ_LZMA=FALSE -DMZ_ZSTD=FALSE
		-DMZ_LIBBSD=FALSE -DMZ_ICONV=TRUE -DMZ_PPMD=OFF -DBUILD_SHARED_LIBS=FALSE)
lux_ext(libexpat     SOURCE_DIR ${EXT_DIR}/libexpat/expat ARGS -DBUILD_SHARED_LIBS=FALSE)
lux_ext(libdeflate   ARGS -DLIBDEFLATE_BUILD_STATIC_LIB=ON -DLIBDEFLATE_BUILD_SHARED_LIB=OFF)
lux_ext(fftw         URL ${EXT_DIR}/fftw/fftw-3.3.11.tar.gz
	ARGS -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
lux_ext(libpng       DEPENDS zlib-ng
	ARGS -DPNG_STATIC=TRUE -DPNG_SHARED=FALSE -DPNG_TESTS=FALSE -DPNG_EXECUTABLES=FALSE
		-DZLIB_INCLUDE_DIR=${INC_DIR} -DZLIB_LIBRARY=${LIB_DIR}/libz.a)
lux_ext(OpenJPH      ARGS -DOJPH_BUILD_EXECUTABLES=FALSE -DOJPH_ENABLE_TIFF_SUPPORT=FALSE -DBUILD_SHARED_LIBS=FALSE)
lux_ext(libjpeg-turbo
	ARGS -DWITH_TESTS=FALSE -DWITH_TURBOJPEG=FALSE -DENABLE_SHARED=FALSE -DENABLE_STATIC=TRUE
		-DWITH_JPEG7=TRUE -DWITH_JPEG8=TRUE)
lux_ext(libtiff      DEPENDS zlib-ng libdeflate
	ARGS -Dtiff-static=TRUE -Dtiff-tools=FALSE -Dtiff-tests=FALSE -Dtiff-contrib=FALSE
		-Dtiff-docs=FALSE -Dtiff-deprecated=FALSE -Dlibdeflate=FALSE -Dlzma=FALSE
		-Dzstd=FALSE -Dwebp=FALSE -Dtiff-cxx=FALSE -Dtiff-opengl=FALSE -Dlerc=FALSE
		-Djbig=FALSE -Djpeg12=FALSE -Djpeg=FALSE)
lux_ext(Imath        ARGS -DBUILD_SHARED_LIBS=FALSE)
lux_ext(openexr      DEPENDS OpenJPH Imath zlib-ng minizip-ng bzip2 xz
	ARGS -DOPENEXR_BUILD_TOOLS=FALSE -DOPENEXR_BUILD_EXAMPLES=FALSE -DOPENEXR_INSTALL_DOCS=FALSE
		-DOPENEXR_BUILD_PYTHON=FALSE -DOPENEXR_INSTALL_TOOLS=FALSE -DOPENEXR_INSTALL_DEVELOPER_TOOLS=FALSE
		-DOPENEXR_FORCE_INTERNAL_OPENJPH=FALSE -DBUILD_SHARED_LIBS=FALSE # internal OpenJPH will break the universe
	CACHE_ARGS "-DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS} -I${INC_DIR}/openjph")
# TODO: OCIO "No compiler support for 'IntelLLVM' compiler." 
lux_ext(OpenColorIO  DEPENDS zlib-ng minizip-ng yaml-cpp pystring Imath bzip2 xz
	ARGS -DOCIO_BUILD_APPS=FALSE -DOCIO_BUILD_OPENFX=FALSE -DOCIO_BUILD_TESTS=FALSE
		-DOCIO_BUILD_PYTHON=FALSE -DOCIO_BUILD_GPU_TESTS=FALSE -DBUILD_SHARED_LIBS=FALSE
		-DOCIO_INSTALL_EXT_PACKAGES=NONE -DZLIB_INCLUDE_DIR=${INC_DIR} -DZLIB_LIBRARY=${LIB_DIR}/libz.a)
lux_ext(OpenImageIO  DEPENDS robin-map zlib-ng Imath openexr libtiff libjpeg-turbo libpng OpenColorIO
	ARGS -DUSE_PYTHON=FALSE -DOIIO_BUILD_TESTS=FALSE -DOIIO_BUILD_TOOLS=FALSE -DEMBEDPLUGINS=TRUE
		-DINSTALL_FONTS=FALSE -DBUILD_DOCS=FALSE -DINSTALL_DOCS=FALSE -DUSE_GIF=FALSE
		-DUSE_FFMPEG=FALSE -DUSE_FREETYPE=FALSE -DUSE_LIBUHDR=FALSE -DUSE_LIBRAW=FALSE
		-DUSE_NUKE=FALSE -DUSE_QT=FALSE -DUSE_JXL=FALSE -DUSE_OPENCV=FALSE -DUSE_DCMTK=FALSE
		-DUSE_FFmpeg=FALSE -DUSE_LibRaw=FALSE -DUSE_OpenVDB=FALSE -DUSE_Ptex=FALSE
		-DUSE_WebP=FALSE -DUSE_OpenVDB=FALSE -DUSE_openjph=FALSE -DUSE_Libheif=FALSE
		-DUSE_OpenJPEG=FALSE -DUSE_TBB=FALSE -DBUILD_SHARED_LIBS=FALSE
		-DZLIB_INCLUDE_DIR=${INC_DIR} -DZLIB_LIBRARY=${LIB_DIR}/libz.a -DJPEG_ROOT=${BUILD_DIR})
# TBB and Embree are SHARED on purpose!
lux_ext(tbb          ARGS -DTBB_TEST=OFF -DBUILD_SHARED_LIBS=ON)
lux_ext(embree       DEPENDS tbb
	ARGS -DEMBREE_STATIC_LIB=FALSE -DEMBREE_TUTORIALS=FALSE -DEMBREE_ZIP_MODE=FALSE
		-DEMBREE_STAT_COUNTERS=FALSE -DEMBREE_BACKFACE_CULLING=FALSE
		-DEMBREE_GEOMETRY_TRIANGLE=TRUE -DEMBREE_GEOMETRY_QUAD=TRUE
		-DEMBREE_GEOMETRY_CURVE=FALSE -DEMBREE_GEOMETRY_USER=TRUE
		-DEMBREE_GEOMETRY_SUBDIVISION=FALSE -DEMBREE_GEOMETRY_INSTANCE=TRUE
		-DEMBREE_GEOMETRY_INSTANCE_ARRAY=TRUE -DEMBREE_GEOMETRY_POINT=TRUE
		-DEMBREE_GEOMETRY_GRID=FALSE -DEMBREE_IGNORE_INVALID_RAYS=TRUE
		-DEMBREE_COMPACT_POLYS=TRUE -DEMBREE_SYCL_SUPPORT=FALSE
		-DEMBREE_IGNORE_CMAKE_CXX_FLAGS=FALSE -DEMBREE_RAY_MASK=TRUE
		-DEMBREE_RAY_PACKETS=TRUE)

###############
# Build Boost #
###############

set(BOOST_MODULES "filesystem|system|program_options|regex|thread|serialization|iostreams|python|chrono|array|assert|locale|format|atomic|interprocess|ptr_container|asio|uuid")
lux_external_project_add(boost
	DEPENDS cpython libexpat fftw
	SOURCE_DIR ${EXT_DIR}/boost PREFIX ${PFX_DIR}/boost INSTALL_DIR ${BUILD_DIR}
	LIST_SEPARATOR |
	CMAKE_ARGS ${COMMON_EXT_PROJECT_ARGS}
		-DBOOST_INCLUDE_LIBRARIES=${BOOST_MODULES}
		-DCMAKE_INSTALL_MESSAGE=LAZY
		-DBOOST_RUNTIME_LINK=static
		-DBOOST_ENABLE_PYTHON=ON
		-DBUILD_SHARED_LIBS=OFF
		-DCMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE
		-DCMAKE_BUILD_WITH_INSTALL_RPATH=TRUE
		-DPython_ROOT_DIR=${BUILD_DIR}
		-DPython_FIND_STRATEGY=LOCATION
		-DCMAKE_FIND_FRAMEWORK=LAST
	CMAKE_CACHE_ARGS ${COMMON_EXT_CACHE_ARGS})
