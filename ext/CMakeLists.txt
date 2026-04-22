###########################################################################
#   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  #
#                                                                         #
#   This file is part of LuxRender.                                       #
#                                                                         #
#   LuxRender is free software; you can redistribute it and/or modify     #
#   it under the terms of the GNU General Public License as published by  #
#   the Free Software Foundation; either version 3 of the License, or     #
#   (at your option) any later version.                                   #
#                                                                         #
#   LuxRender is distributed in the hope that it will be useful,          #
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        #
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
#   GNU General Public License for more details.                          #
#                                                                         #
#   You should have received a copy of the GNU General Public License     #
#   along with this program.  If not, see <http://www.gnu.org/licenses/>. #
#                                                                         #
#   LuxRender website: https://github.com/rrubberr/LuxRender              #
###########################################################################

MESSAGE(STATUS "CMAKE VERSION DETECTED " ${CMAKE_VERSION})
	cmake_minimum_required(VERSION 3.10)

project(LuxRenderDeps)

include(ExternalProject)

# Clean build option.
option(REBUILD_DEPS "Remove build artifacts for a clean rebuild." OFF)

if(REBUILD_DEPS)
    message(STATUS "Clean build requested - removing build directory.")
    file(GLOB_RECURSE _build_files "${CMAKE_CURRENT_SOURCE_DIR}/build/*")
    file(REMOVE_RECURSE ${_build_files})
endif()

# This points to LuxRender/ext.
set(EXT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# This points to LuxRender/ext/build.
set(DEPS_BUILD_DIR ${EXT_DIR}/build)

# This points to LuxRender/ext/build/bin.
set(DEPS_BIN_DIR ${DEPS_BUILD_DIR}/bin)

# This points to LuxRender/ext/build/lib.
set(DEPS_LIB_DIR ${DEPS_BUILD_DIR}/lib)

# This points to LuxRender/ext/build/include.
set(DEPS_INC_DIR ${DEPS_BUILD_DIR}/include)

#This points to LuxRender/ext/build/pfx.
set(DEPS_PFX_DIR ${DEPS_BUILD_DIR}/pfx)

set(
	COMMON_EXT_PROJECT_ARGS
	-DCMAKE_POSITION_INDEPENDENT_CODE=TRUE
    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_INSTALL_INCLUDEDIR=include
    -DCMAKE_PREFIX_PATH=${DEPS_BUILD_DIR}
)

#################
# Build OpenSSL #
#################

ExternalProject_Add(
    openssl
    SOURCE_DIR ${EXT_DIR}/openssl
    PREFIX ${DEPS_PFX_DIR}/openssl
    INSTALL_DIR ${DEPS_BUILD_DIR}
    WORKING_DIRECTORY ${EXT_DIR}/openssl
    CONFIGURE_COMMAND bash -c "cd ${EXT_DIR}/openssl && ./config shared -fPIC -w -std=c++14 --prefix=${DEPS_BUILD_DIR}"
    BUILD_COMMAND bash -c "cd ${EXT_DIR}/openssl && make -j1 install"
    INSTALL_COMMAND ""
)

#################
# Build cpython #
#################

ExternalProject_Add(
	cpython
	DEPENDS
		openssl
	SOURCE_DIR ${EXT_DIR}/cpython
	PREFIX ${DEPS_PFX_DIR}/cpython
	INSTALL_DIR ${DEPS_BUILD_DIR}
    CONFIGURE_COMMAND
        /bin/sh -c "cd ${EXT_DIR}/cpython && sh ./configure --prefix=${DEPS_BUILD_DIR} --disable-shared \
        --with-ensurepip=no --enable-optimizations"
	PATCH_COMMAND
		/bin/sh -c "(cd ${EXT_DIR}/cpython && grep -q m_sinpi Modules/mathmodule.c || sed -i 's@sinpi@m_sinpi@g' Modules/mathmodule.c) && \
					(cd ${EXT_DIR}/cpython && grep -q is_pad Modules/_cursesmodule.c || sed -i 's@(self->win->_flags & _ISPAD)@(is_pad(self->win))@g' Modules/_cursesmodule.c) && \
                    (cd ${EXT_DIR}/cpython && patch -p1 -N -i ${EXT_DIR}/patches/python/python-disable-nis.patch || true)"
    BUILD_COMMAND
        /bin/sh -c "cd ${EXT_DIR}/cpython && make install -j$(nproc) cc=g++ cxx=g++ \
            LDFLAGS=\"-Wl,-rpath,'\$\$ORIGIN'\" \
            cflags=\"-O2 -w -fPIC -I ${BIN_DIR} -I ${INC_DIR}/openssl\" \
            cxxflags=\"-O2 -w -fPIC -std=c++14 -I ${BIN_DIR} -I ${INC_DIR}/openssl -fpermissive\""
    INSTALL_COMMAND ""
)

#################
# Build zlib-ng #
#################

ExternalProject_Add(
	zlib-ng
    DEPENDS cpython
	SOURCE_DIR ${EXT_DIR}/zlib-ng
	PREFIX ${DEPS_PFX_DIR}/zlib-ng
	INSTALL_DIR ${DEPS_BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DZLIB_COMPAT=TRUE
		-DBUILD_TESTING=FALSE
        -DBUILD_SHARED_LIBS=FALSE
)

###################
# Build robin-map #
###################

ExternalProject_Add(
	robin-map
    DEPENDS cpython
	SOURCE_DIR ${EXT_DIR}/robin-map
	PREFIX ${DEPS_PFX_DIR}/robin-map
	INSTALL_DIR ${DEPS_BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DTSL_ROBIN_MAP_ENABLE_INSTALL=TRUE
)

##################
# Build yaml-cpp #
##################

ExternalProject_Add(
	yaml-cpp
    DEPENDS cpython
	SOURCE_DIR ${EXT_DIR}/yaml-cpp
	PREFIX ${DEPS_PFX_DIR}/yaml-cpp
	INSTALL_DIR ${DEPS_BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DYAML_CPP_BUILD_CONTRIB=FALSE
		-DYAML_CPP_BUILD_TOOLS=FALSE
		-DYAML_BUILD_SHARED_LIBS=FALSE
)

##################
# Build pybind11 #
##################

ExternalProject_Add(
	pybind11
    DEPENDS cpython
	SOURCE_DIR ${EXT_DIR}/pybind11
	PREFIX ${DEPS_PFX_DIR}/pybind11
	INSTALL_DIR ${DEPS_BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DPYBIND11_TEST=FALSE
)

##################
# Build pystring #
##################

ExternalProject_Add(
	pystring
    DEPENDS cpython
	SOURCE_DIR ${EXT_DIR}/pystring
	PREFIX ${DEPS_PFX_DIR}/pystring
	INSTALL_DIR ${DEPS_BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
        -DBUILD_SHARED_LIBS=FALSE

)

####################
# Build minizip-ng #
####################

ExternalProject_Add(
	minizip-ng
    DEPENDS zlib-ng
	SOURCE_DIR ${EXT_DIR}/minizip-ng
	PREFIX ${DEPS_PFX_DIR}/minizip-ng
	INSTALL_DIR ${DEPS_BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DMZ_ZLIB_FLAVOR=zlib
		-DMZ_PKCRYPT=FALSE
		-DMZ_WZAES=FALSE
		-DMZ_OPENSSL=FALSE
        -DMZ_COMPAT=TRUE
        -DMZ_ZLIB=TRUE
        -DMZ_BZIP2=FALSE
        -DMZ_LZMA=FALSE
        -DMZ_ZSTD=FALSE
        -DMZ_LIBBSD=FALSE
        -DMZ_ICONV=TRUE
        -DBUILD_SHARED_LIBS=FALSE
)

################
# Build libpng #
################

ExternalProject_Add(
	libpng
    DEPENDS minizip-ng
	SOURCE_DIR ${EXT_DIR}/libpng
	PREFIX ${DEPS_PFX_DIR}/libpng
	INSTALL_DIR ${DEPS_BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DPNG_STATIC=TRUE
		-DPNG_SHARED=FALSE
		-DPNG_TESTS=FALSE
		-DPNG_EXECUTABLES=FALSE
)

#################
# Build OpenJPH #
#################

ExternalProject_Add(
	OpenJPH
    DEPENDS minizip-ng
	SOURCE_DIR ${EXT_DIR}/OpenJPH
	PREFIX ${DEPS_PFX_DIR}/OpenJPH
	INSTALL_DIR ${DEPS_BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DOJPH_BUILD_EXECUTABLES=FALSE
		-DOJPH_ENABLE_TIFF_SUPPORT=FALSE
        -DBUILD_SHARED_LIBS=FALSE
)

#######################
# Build libjpeg-turbo #
#######################

ExternalProject_Add(
	libjpeg-turbo
    DEPENDS OpenJPH
	SOURCE_DIR ${EXT_DIR}/libjpeg-turbo
	PREFIX ${DEPS_PFX_DIR}/libjpeg-turbo
	INSTALL_DIR ${DEPS_BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DWITH_JAVA=FALSE
		-DWITH_TURBOJPEG=FALSE
        -DBUILD_SHARED_LIBS=FALSE
)

#################
# Build libtiff #
#################

ExternalProject_Add(
	libtiff
    DEPENDS libjpeg-turbo
	SOURCE_DIR ${EXT_DIR}/libtiff
	PREFIX ${DEPS_PFX_DIR}/libtiff
	INSTALL_DIR ${DEPS_BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-Dtiff-static=TRUE
		-Dtiff-tools=FALSE
		-Dtiff-tests=FALSE
		-Dtiff-contrib=FALSE
		-Dtiff-docs=FALSE
		-Dtiff-deprecated=FALSE
		-Dlibdeflate=FALSE
		-Dlzma=FALSE
		-Dzstd=FALSE
		-Dwebp=FALSE
		-Dtiff-cxx=FALSE
		-Dtiff-opengl=FALSE
		-Djbig=FALSE
)

###############
# Build Imath #
###############

ExternalProject_Add(
	Imath
    DEPENDS libtiff
	SOURCE_DIR ${EXT_DIR}/Imath
	PREFIX ${DEPS_PFX_DIR}/Imath
	INSTALL_DIR ${DEPS_BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
        -DBUILD_SHARED_LIBS=FALSE
)

#################
# Build openexr #
#################

ExternalProject_Add(
	openexr
	DEPENDS Imath
	SOURCE_DIR ${EXT_DIR}/openexr
	PREFIX ${DEPS_PFX_DIR}/openexr
	INSTALL_DIR ${DEPS_BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DOPENEXR_BUILD_TOOLS=FALSE
		-DOPENEXR_BUILD_EXAMPLES=FALSE
		-DOPENEXR_INSTALL_DOCS=FALSE
		-DOPENEXR_BUILD_PYTHON=FALSE
		-DOPENEXR_INSTALL_TOOLS=FALSE
		-DOPENEXR_INSTALL_DEVELOPER_TOOLS=FALSE
		-DOPENEXR_FORCE_INTERNAL_OPENJPH=FALSE # Do not use internal. This will break the universe.
        -DBUILD_SHARED_LIBS=FALSE
)

#####################
# Build OpenColorIO #
#####################

ExternalProject_Add(
	OpenColorIO
    DEPENDS openexr
	SOURCE_DIR ${EXT_DIR}/OpenColorIO
	PREFIX ${DEPS_PFX_DIR}/OpenColorIO
	INSTALL_DIR ${DEPS_BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DOCIO_BUILD_APPS=FALSE
		-DOCIO_BUILD_OPENFX=FALSE
		-DOCIO_BUILD_TESTS=FALSE
		-DOCIO_BUILD_PYTHON=FALSE
		-DOCIO_BUILD_GPU_TESTS=FALSE
        -DBUILD_SHARED_LIBS=FALSE
        -DOCIO_INSTALL_EXT_PACKAGES=NONE
)

#####################
# Build OpenImageIO #
#####################

ExternalProject_Add(
	OpenImageIO
	DEPENDS OpenColorIO
	SOURCE_DIR ${EXT_DIR}/OpenImageIO
	PREFIX ${DEPS_PFX_DIR}/OpenImageIO
	INSTALL_DIR ${DEPS_BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DUSE_PYTHON=FALSE
		-DOIIO_BUILD_TESTS=FALSE
		-DOIIO_BUILD_TOOLS=FALSE
		-DINSTALL_FONTS=FALSE
		-DBUILD_DOCS=FALSE
		-DINSTALL_DOCS=FALSE
		-DUSE_GIF=FALSE
		-DUSE_FFMPEG=FALSE
		-DUSE_FREETYPE=FALSE
		-DUSE_LIBUHDR=FALSE
		-DUSE_LIBRAW=FALSE
		-DUSE_NUKE=FALSE
		-DUSE_QT=FALSE
		-DUSE_JXL=FALSE
		-DUSE_OPENCV=FALSE
		-DUSE_DCMTK=FALSE
		-DUSE_FFmpeg=FALSE
		-DUSE_LibRaw=FALSE
		-DUSE_OpenVDB=FALSE
		-DUSE_Ptex=FALSE
		-DUSE_WebP=FALSE
		-DUSE_OpenVDB=FALSE
		-DUSE_openjph=FALSE
		-DUSE_Libheif=FALSE
		-DUSE_OpenJPEG=FALSE
        -DUSE_TBB=FALSE
        -DBUILD_SHARED_LIBS=FALSE
)

###############
# Build Boost #
###############

set(
	BOOST_MODULES
	"filesystem|system|program_options|regex|system|thread|serialization|iostreams|python|chrono|array|assert|locale|format"
)

ExternalProject_Add(
	boost
	DEPENDS cpython OpenImageIO
	SOURCE_DIR ${EXT_DIR}/boost
	PREFIX ${DEPS_PFX_DIR}/boost
	INSTALL_DIR ${DEPS_BUILD_DIR}
	LIST_SEPARATOR |
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DBOOST_INCLUDE_LIBRARIES=${BOOST_MODULES}
		-DCMAKE_INSTALL_MESSAGE=LAZY
		-DCMAKE_CXX_FLAGS="-I\"${INC_DIR}/python3.5m\" -w -fPIC -std=c++14"
		-DBOOST_RUNTIME_LINK=static
		-DBOOST_ENABLE_PYTHON=ON
		-DBUILD_SHARED_LIBS=OFF
		-DBUILD_STATIC_LIBS=ON
		-DBOOST_CMAKE_RELOCATABLE=ON
		-DCMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE
		-DCMAKE_BUILD_WITH_INSTALL_RPATH=TRUE
		-DPython_ROOT_DIR=${BLD_DIR}
		-DPython_FIND_STRATEGY=LOCATION
)