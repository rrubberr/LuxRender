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

# Helper: create a lux2 check executable + test registration.
# Usage: add_lux2_check(NAME <name> [SOURCES ...] [LINKS <target> ...] [WORKING_DIR <dir>])
function(add_lux2_check)
	cmake_parse_arguments(CHECK "" "NAME;WORKING_DIR" "SOURCES;LINKS" ${ARGN})

	add_executable(${CHECK_NAME} ${CHECK_SOURCES})
	target_include_directories(${CHECK_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
	target_link_libraries(${CHECK_NAME} PRIVATE ${CHECK_LINKS})

	add_test(NAME ${CHECK_NAME} COMMAND ${CHECK_NAME})
	if(CHECK_WORKING_DIR)
		set_tests_properties(${CHECK_NAME} PROPERTIES WORKING_DIRECTORY ${CHECK_WORKING_DIR})
	endif()
endfunction()

# --- Tests linking only lux2 ---
add_lux2_check(
	NAME lux2corecheck
	SOURCES core_check.cpp
	LINKS lux2
)

add_lux2_check(
	NAME lux2foundationcheck
	SOURCES foundation_check.cpp
	LINKS lux2
)

add_lux2_check(
	NAME lux2qmccheck
	SOURCES qmc_check.cpp
	LINKS lux2
)

add_lux2_check(
	NAME lux2samplercheck
	SOURCES sampler_check.cpp
	LINKS lux2
)

add_lux2_check(
	NAME lux2filtercheck
	SOURCES filter_check.cpp
	LINKS lux2
)

add_lux2_check(
	NAME lux2cameracheck
	SOURCES camera_check.cpp
	LINKS lux2
)

add_lux2_check(
	NAME lux2arealightcheck
	SOURCES area_light_check.cpp
	LINKS lux2
)

add_lux2_check(
	NAME lux2infinitelightcheck
	SOURCES infinite_light_check.cpp
	LINKS lux2
)

add_lux2_check(
	NAME lux2filmcheck
	SOURCES film_check.cpp
	LINKS lux2
)

add_lux2_check(
	NAME lux2tonemapcheck
	SOURCES tonemap_check.cpp
	LINKS lux2
)

add_lux2_check(
	NAME lux2statscheck
	SOURCES stats_check.cpp
	LINKS lux2 Threads::Threads
)

add_lux2_check(
	NAME lux2imageiocheck
	SOURCES imageio_check.cpp
	LINKS lux2
	WORKING_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

# --- Tests linking lux2 + Threads::Threads ---
add_lux2_check(
	NAME lux2bsdfdispatchcheck
	SOURCES bsdf_dispatch_check.cpp
	LINKS lux2 Threads::Threads
)

add_lux2_check(
	NAME lux2texturecheck
	SOURCES texture_check.cpp
	LINKS lux2 Threads::Threads
)

add_lux2_check(
	NAME lux2materialcheck
	SOURCES material_check.cpp
	LINKS lux2 Threads::Threads
)

# --- Tests linking lux2 + Embree + Threads::Threads ---
add_lux2_check(
	NAME lux2shapecheck
	SOURCES shape_check.cpp
	LINKS lux2 ${LUX2_EMBREE_TARGET} Threads::Threads
	WORKING_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../test/cornell_box/Scene/00001
)

add_lux2_check(
	NAME lux2embreecheck
	SOURCES embree_check.cpp
	LINKS lux2 ${LUX2_EMBREE_TARGET} Threads::Threads
)

add_lux2_check(
	NAME lux2scenewiringcheck
	SOURCES scene_wiring_check.cpp
	LINKS lux2 ${LUX2_EMBREE_TARGET} Threads::Threads
	WORKING_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../test/cornell_box/Scene/00001
)

add_lux2_check(
	NAME lux2pathcheck
	SOURCES path_check.cpp
	LINKS lux2 ${LUX2_EMBREE_TARGET} Threads::Threads
)

# --- Tests linking lux2 + Embree + TBB + Threads::Threads ---
add_lux2_check(
	NAME lux2threadpoolcheck
	SOURCES threadpool_check.cpp
	LINKS lux2 ${LUX2_EMBREE_TARGET} TBB::tbb Threads::Threads
)

add_lux2_check(
	NAME lux2renderercheck
	SOURCES renderer_check.cpp
	LINKS lux2 ${LUX2_EMBREE_TARGET} TBB::tbb Threads::Threads
)

# --- Special: colorcheck includes a reference implementation ---
add_executable(lux2colorcheck color_check.cpp color_ref/color_ref.cpp)
target_include_directories(lux2colorcheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(lux2colorcheck PRIVATE lux2)
add_test(NAME lux2colorcheck COMMAND lux2colorcheck)
