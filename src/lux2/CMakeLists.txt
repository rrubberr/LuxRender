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

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CompilerSetup.cmake)

##################
# Set up folders #
##################

# This points to LuxRender/ext.
set(EXT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../ext)

# This points to LuxRender/ext/build.
set(BUILD_DIR ${EXT_DIR}/build)

# This points to LuxRender/ext/build/lib.
set(LIB_DIR ${BUILD_DIR}/lib)

# This points to LuxRender/ext/build/include.
set(INC_DIR ${BUILD_DIR}/include)

#####################
# Setup the project #
#####################

PROJECT(lux2)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Enables ctest for lux2corecheck.
enable_testing()

#####################
# Find Dependencies #
#####################

# Enoki is header-only.
if(NOT EXISTS ${INC_DIR}/enoki/array.h)
	message(FATAL_ERROR
		"Enoki headers not found in ${INC_DIR}/enoki - "
		"build the 'enoki' target in the top-level build first.")
endif()
INCLUDE_DIRECTORIES(BEFORE SYSTEM "${INC_DIR}")

FIND_PACKAGE(embree CONFIG REQUIRED)
IF(TARGET embree::embree)
	SET(LUX2_EMBREE_TARGET embree::embree)
ELSEIF(TARGET embree)
	SET(LUX2_EMBREE_TARGET embree)
ELSE()
	MESSAGE(FATAL_ERROR "No Embree target (embree::embree or embree) found.")
ENDIF()
MESSAGE(STATUS "lux2: Embree found (${LUX2_EMBREE_TARGET})")

FIND_PACKAGE(TBB CONFIG REQUIRED)
IF(NOT TARGET TBB::tbb)
	MESSAGE(FATAL_ERROR "TBB::tbb target not found - build the 'tbb' ext target first.")
ENDIF()
MESSAGE(STATUS "lux2: TBB found (TBB::tbb)")

FIND_PACKAGE(Threads REQUIRED)

FIND_PACKAGE(OpenEXR CONFIG REQUIRED)
if(TARGET OpenEXR::OpenEXR)
	get_target_property(EXR_INC OpenEXR::OpenEXR INTERFACE_INCLUDE_DIRECTORIES)
	MESSAGE(STATUS "OpenEXR include directory: ${EXR_INC}")
endif()
FIND_PACKAGE(Imath CONFIG REQUIRED)
if(TARGET Imath::Imath)
	get_target_property(IMATH_INC Imath::Imath INTERFACE_INCLUDE_DIRECTORIES)
	MESSAGE(STATUS "Imath include directory: ${IMATH_INC}")
endif()

FIND_PACKAGE(PNG CONFIG REQUIRED)
IF(PNG_INCLUDE_DIRS)
	MESSAGE(STATUS "PNG include directory: " ${PNG_INCLUDE_DIRS})
	INCLUDE_DIRECTORIES(BEFORE SYSTEM ${PNG_INCLUDE_DIRS})
ELSE(PNG_INCLUDE_DIRS)
	MESSAGE(STATUS "Warning : could not find PNG headers - building without png support")
endif()

FIND_PACKAGE(libjpeg-turbo CONFIG REQUIRED)
MESSAGE(STATUS "lux2: libjpeg-turbo found (libjpeg-turbo::jpeg-static)")

FIND_PACKAGE(TIFF CONFIG REQUIRED)
IF(TIFF_INCLUDE_DIRS)
	MESSAGE(STATUS "TIFF include directory: " ${TIFF_INCLUDE_DIRS})
	INCLUDE_DIRECTORIES(BEFORE SYSTEM ${TIFF_INCLUDE_DIRS})
ENDIF()
MESSAGE(STATUS "lux2: TIFF found (TIFF::tiff)")

# Bison/Flex for the .lxs parser.
FIND_PACKAGE(BISON REQUIRED)
FIND_PACKAGE(FLEX REQUIRED)

BISON_TARGET(Lux2Parser ${CMAKE_CURRENT_SOURCE_DIR}/core/luxparse.y
	${CMAKE_CURRENT_BINARY_DIR}/luxparse.cpp)
FLEX_TARGET(Lux2Lexer ${CMAKE_CURRENT_SOURCE_DIR}/core/luxlex.l
	${CMAKE_CURRENT_BINARY_DIR}/luxlex.cpp)
ADD_FLEX_BISON_DEPENDENCY(Lux2Lexer Lux2Parser)

######################
# Set compiler flags #
######################

# Same native/vectorization policy as src/lux.
ADD_DEFINITIONS(-march=native -ftree-vectorize -funroll-loops -DHAVE_PTHREAD_H)
add_compile_options(-w)

# Where the finished binaries are collected.
if(NOT DEFINED LUX2_RELEASE_DIR)
	set(LUX2_RELEASE_DIR ${CMAKE_BINARY_DIR})
endif()

#######################
# Build lux2 binaries #
#######################

# lux2 core type headers.
set(LUX2_CORE_HEADERS
	core/lux2.h
	core/vecp.h
	core/geometry.h
	core/spectrum.h
	core/color.h
	core/ray.h
	core/rng.h
)

# Build lux2corecheck.
ADD_EXECUTABLE(lux2corecheck
	tests/core_check.cpp
	${LUX2_CORE_HEADERS}
)

TARGET_INCLUDE_DIRECTORIES(lux2corecheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

# Run the check automatically after each build of the check target.
add_test(NAME lux2corecheck COMMAND lux2corecheck)

# lux2 foundation sources.
set(LUX2_FOUNDATION_SRC
	core/paramset.cpp
	core/color.cpp
	core/rply.cpp
	core/context2.cpp
	core/scene.cpp
	core/dynload.cpp
	core/plymesh.cpp
	core/embree2.cpp
	core/fresnel.cpp
	core/schlick_distribution.cpp
	core/threadpool.cpp
)

set(LUX2_FOUNDATION_HEADERS
	core/scene.h
	core/paramset.h
	core/context2.h
	core/graphicsstate.h
	core/transform_stack.h
	core/transform.h
	core/dynload.h
	core/plymesh.h
	core/embree2.h
)

# Built as a SHARED library (liblux2.so).
set(LUX2_CORE_SRC
	${LUX2_FOUNDATION_SRC}
	core/api.cpp
	shapes/sphere.cpp
	shapes/plymesh.cpp
	textures/constant.cpp
	textures/fresnelcolor.cpp
	materials/null.cpp
	materials/matte.cpp
	materials/metal2.cpp
	materials/glass.cpp
	samplers/ldsampler.cpp
	filters/gaussian.cpp
	cameras/perspective.cpp
	lights/area.cpp
	lights/infinite.cpp
	film/null.cpp
	film/fleximage.cpp
	integrators/path.cpp
	core/colorsystem.cpp
	core/tonemap.cpp
	core/exrio.cpp
	core/pngio.cpp
	core/jpegio.cpp
	core/tiffio.cpp
	tonemaps/lineartonemap.cpp
	${BISON_Lux2Parser_OUTPUTS}
	${FLEX_Lux2Lexer_OUTPUTS}
)

ADD_LIBRARY(lux2 SHARED
	${LUX2_CORE_SRC}
)

TARGET_INCLUDE_DIRECTORIES(lux2 PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
)

# TBB shared scheduler also used by Embree; linking it here keeps
# a single libtbb in the process.
TARGET_LINK_LIBRARIES(lux2 PUBLIC
	${LUX2_EMBREE_TARGET} TBB::tbb
	OpenEXR::OpenEXR OpenEXR::Iex Imath::Imath PNG::PNG
	libjpeg-turbo::jpeg-static TIFF::tiff)

# Build lux2foundationcheck.
ADD_EXECUTABLE(lux2foundationcheck
	tests/foundation_check.cpp
	${LUX2_FOUNDATION_HEADERS}
)

TARGET_INCLUDE_DIRECTORIES(lux2foundationcheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

TARGET_LINK_LIBRARIES(lux2foundationcheck PRIVATE lux2)

add_test(NAME lux2foundationcheck
	COMMAND lux2foundationcheck
	${CMAKE_CURRENT_SOURCE_DIR}/../../test/cornell_box/Scene/00001/floor_0000_m000.ply
)

# Shape tessellation (sphere + plymesh) and Scene::Commit wiring.
ADD_EXECUTABLE(lux2shapecheck
	tests/shape_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2shapecheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2shapecheck PRIVATE
	lux2
	${LUX2_EMBREE_TARGET}
	Threads::Threads
)
add_test(NAME lux2shapecheck
	COMMAND lux2shapecheck
	${CMAKE_CURRENT_SOURCE_DIR}/../../test/cornell_box/Scene/00001/floor_0000_m000.ply
)

# BSDF dispatch substrate.
ADD_EXECUTABLE(lux2bsdfdispatchcheck
	tests/bsdf_dispatch_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2bsdfdispatchcheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2bsdfdispatchcheck PRIVATE
	lux2
	Threads::Threads
)
add_test(NAME lux2bsdfdispatchcheck COMMAND lux2bsdfdispatchcheck)

# Texture plugins + Fresnel math.
ADD_EXECUTABLE(lux2texturecheck
	tests/texture_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2texturecheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2texturecheck PRIVATE
	lux2
	Threads::Threads
)
add_test(NAME lux2texturecheck COMMAND lux2texturecheck)

# Material kernels (D.1 matte: Lambertian + Oren--Nayar).
ADD_EXECUTABLE(lux2materialcheck
	tests/material_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2materialcheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2materialcheck PRIVATE
	lux2
	Threads::Threads
)
add_test(NAME lux2materialcheck COMMAND lux2materialcheck)

# Embree packet intersect/occlude/shade.
ADD_EXECUTABLE(lux2embreecheck
	tests/embree_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2embreecheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2embreecheck PRIVATE
	lux2
	${LUX2_EMBREE_TARGET}
	Threads::Threads
)
add_test(NAME lux2embreecheck COMMAND lux2embreecheck)

# Phase 1: shared TBB scheduler cap + render arena coexisting with Embree.
ADD_EXECUTABLE(lux2threadpoolcheck
	tests/threadpool_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2threadpoolcheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2threadpoolcheck PRIVATE
	lux2
	${LUX2_EMBREE_TARGET}
	TBB::tbb
	Threads::Threads
)
add_test(NAME lux2threadpoolcheck COMMAND lux2threadpoolcheck)

# Stage 0: QMC substrate (radical_inverse_2 / sobol_2 / permute / tea).
ADD_EXECUTABLE(lux2qmccheck
	tests/qmc_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2qmccheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME lux2qmccheck COMMAND lux2qmccheck)

# Stage 2: LDSampler (0-2 sequence) streaming interface.
ADD_EXECUTABLE(lux2samplercheck
	tests/sampler_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2samplercheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2samplercheck PRIVATE lux2)
add_test(NAME lux2samplercheck COMMAND lux2samplercheck)

# Stage 3: Gaussian pixel filter (LuxRender semantics).
ADD_EXECUTABLE(lux2filtercheck
	tests/filter_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2filtercheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2filtercheck PRIVATE lux2)
add_test(NAME lux2filtercheck COMMAND lux2filtercheck)

# Stage 4: Perspective camera (legacy +Z convention, BDPT endpoint).
ADD_EXECUTABLE(lux2cameracheck
	tests/camera_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2cameracheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2cameracheck PRIVATE lux2)
add_test(NAME lux2cameracheck COMMAND lux2cameracheck)

# Stage 5: Area light (uniform triangle sampling, area-CDF, Pdf_L).
ADD_EXECUTABLE(lux2arealightcheck
	tests/area_light_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2arealightcheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2arealightcheck PRIVATE lux2)
add_test(NAME lux2arealightcheck COMMAND lux2arealightcheck)

# Stage 6: Infinite (environment) light, uniform-sphere sampling.
ADD_EXECUTABLE(lux2infinitelightcheck
	tests/infinite_light_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2infinitelightcheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2infinitelightcheck PRIVATE lux2)
add_test(NAME lux2infinitelightcheck COMMAND lux2infinitelightcheck)

# FlexImageFilm core.
ADD_EXECUTABLE(lux2filmcheck
	tests/film_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2filmcheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2filmcheck PRIVATE lux2)
add_test(NAME lux2filmcheck COMMAND lux2filmcheck)

# Stage D: linear/autolinear tonemap kernels + registry.
ADD_EXECUTABLE(lux2tonemapcheck
	tests/tonemap_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2tonemapcheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2tonemapcheck PRIVATE lux2)
add_test(NAME lux2tonemapcheck COMMAND lux2tonemapcheck)

# Stage D: EXR/PNG writers and readers.
ADD_EXECUTABLE(lux2imageiocheck
	tests/imageio_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2imageiocheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2imageiocheck PRIVATE lux2)
add_test(NAME lux2imageiocheck COMMAND lux2imageiocheck
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Stage 7: Scene::Commit wiring (camera/sampler/filter/lights/area geometry).
ADD_EXECUTABLE(lux2scenewiringcheck
	tests/scene_wiring_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2scenewiringcheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2scenewiringcheck PRIVATE
	lux2
	${LUX2_EMBREE_TARGET}
	Threads::Threads
)
add_test(NAME lux2scenewiringcheck COMMAND lux2scenewiringcheck)

# Stage 1: NullFilm + PathIntegrator skeleton + registry construction.
ADD_EXECUTABLE(lux2pathcheck
	tests/path_check.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2pathcheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2pathcheck PRIVATE
	lux2
	${LUX2_EMBREE_TARGET}
	Threads::Threads
)
add_test(NAME lux2pathcheck COMMAND lux2pathcheck)

# SoA SPD/CIE pipeline cross-checked against the scalar colorref reference
# (tests/color_ref/, compiled into this target only).
ADD_EXECUTABLE(lux2colorcheck
	tests/color_check.cpp
	tests/color_ref/color_ref.cpp
)
TARGET_INCLUDE_DIRECTORIES(lux2colorcheck PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
TARGET_LINK_LIBRARIES(lux2colorcheck PRIVATE
	lux2
)
add_test(NAME lux2colorcheck COMMAND lux2colorcheck)

ADD_EXECUTABLE(lux2console
	console/lux2console.cpp
	${LUX2_CORE_HEADERS}
)

TARGET_INCLUDE_DIRECTORIES(lux2console PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

TARGET_LINK_LIBRARIES(lux2console PRIVATE
	lux2
	${LUX2_EMBREE_TARGET}
	Threads::Threads
)

SET_TARGET_PROPERTIES(lux2console PROPERTIES
	BUILD_WITH_INSTALL_RPATH TRUE
	INSTALL_RPATH "$ORIGIN"
)

# Collect the binary next to the regular LuxRender binaries.
ADD_CUSTOM_COMMAND(TARGET lux2console POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${LUX2_RELEASE_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:lux2console> "${LUX2_RELEASE_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:lux2> "${LUX2_RELEASE_DIR}/"
)
