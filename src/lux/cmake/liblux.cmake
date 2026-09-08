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

#####################
# Custom Bison/Flex #
#####################

# Create custom command for bison/yacc
BISON_TARGET(LuxParser ${CMAKE_SOURCE_DIR}/core/luxparse.y ${CMAKE_BINARY_DIR}/luxparse.cpp)
SET_SOURCE_FILES_PROPERTIES(${CMAKE_BINARY_DIR}/core/luxparse.cpp GENERATED)

# Create custom command for flex/lex
FLEX_TARGET(LuxLexer ${CMAKE_SOURCE_DIR}/core/luxlex.l ${CMAKE_BINARY_DIR}/luxlex.cpp)
SET_SOURCE_FILES_PROPERTIES(${CMAKE_BINARY_DIR}/luxlex.cpp GENERATED)
SET(lux_parser_src
	core/luxparse.y
	core/luxlex.l)
SOURCE_GROUP("Parser Files" FILES ${lux_parser_src})

ADD_FLEX_BISON_DEPENDENCY(LuxLexer LuxParser)

# The generated parser sources do not exist at configure time, so they
# stay as explicit entries rather than being discovered by a glob.
SET(lux_core_generated_src
	${CMAKE_BINARY_DIR}/luxparse.cpp
	${CMAKE_BINARY_DIR}/luxlex.cpp
	)
SOURCE_GROUP("Source Files\\Core\\Generated" FILES ${lux_core_generated_src})

############################
# Source/header collection #
############################

# Used to build source group paths.
set(LUX_BS "\\")

# Each entry is "<relative-dir>|<Source Group display name>".
SET(LUX_LIB_DIR_GROUPS
	"accelerators|Accelerators"
	"cameras|Cameras"
	"core|Core"
	"cpp_api|C++ API"
	"film|Film"
	"filters|Filters"
	"integrators|Integrators"
	"lights|Lights"
	"materials|Materials"
	"pixelsamplers|Pixel Samplers"
	"renderers|Renderers"
	"samplers|Samplers"
	"server|Core"
	"shapes|Shapes"
	"textures|Textures"
	"tonemaps|Tonemaps"
	"volumes|Volumes"
)

# Subdirectories beneath the roots are not part of liblux.
SET(LUX_LIB_EXCLUDE_REGEXES
	"/cpp_api/test/"
	"/cpp_api/preview_scenes/"
)

# Logic to include/exclude the TBB renderer variants.
IF(LUX_USE_TBB)
	LIST(APPEND LUX_LIB_EXCLUDE_REGEXES
		"/core/scheduler\.cpp$"
		"/renderers/samplerrenderer\.cpp$"
		"/renderers/sppmrenderer\.cpp$"
		"/accelerators/bruteforce\.cpp$"
		"/accelerators/qbvhaccel\.cpp$"
		"/accelerators/tabreckdtree\.cpp$")
ELSE()
	LIST(APPEND LUX_LIB_EXCLUDE_REGEXES
		"/core/tbbscheduler\.cpp$"
		"/renderers/tbbsamplerrenderer\.cpp$"
		"/renderers/tbbsppmrenderer\.cpp$")
ENDIF()

SET(lux_lib_src "")
SET(lux_lib_hdr "")

FOREACH(entry ${LUX_LIB_DIR_GROUPS})
	string(REGEX REPLACE "^([^|]+)\\|(.*)$" "\\1" rel_dir "${entry}")
	string(REGEX REPLACE "^([^|]+)\\|(.*)$" "\\2" group_name "${entry}")

	file(GLOB_RECURSE dir_src CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/${rel_dir}/*.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/${rel_dir}/*.c")
	file(GLOB_RECURSE dir_hdr CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/${rel_dir}/*.h")

	# Drop excluded subdirectories from both lists.
	FOREACH(excl ${LUX_LIB_EXCLUDE_REGEXES})
		list(FILTER dir_src EXCLUDE REGEX "${excl}")
		list(FILTER dir_hdr EXCLUDE REGEX "${excl}")
	ENDFOREACH()

	list(APPEND lux_lib_src ${dir_src})
	list(APPEND lux_lib_hdr ${dir_hdr})

	# Preserve directory IDE grouping.
	FOREACH(f ${dir_src})
		file(RELATIVE_PATH f_rel "${CMAKE_CURRENT_SOURCE_DIR}/${rel_dir}" "${f}")
		get_filename_component(f_sub "${f_rel}" DIRECTORY)
		string(REPLACE "/" "${LUX_BS}" f_sub "${f_sub}")
		IF(f_sub STREQUAL "")
			SOURCE_GROUP("Source Files${LUX_BS}${group_name}" FILES ${f})
		ELSE()
			SOURCE_GROUP("Source Files${LUX_BS}${group_name}${LUX_BS}${f_sub}" FILES ${f})
		ENDIF()
	ENDFOREACH()
	FOREACH(f ${dir_hdr})
		file(RELATIVE_PATH f_rel "${CMAKE_CURRENT_SOURCE_DIR}/${rel_dir}" "${f}")
		get_filename_component(f_sub "${f_rel}" DIRECTORY)
		string(REPLACE "/" "${LUX_BS}" f_sub "${f_sub}")
		IF(f_sub STREQUAL "")
			SOURCE_GROUP("Header Files${LUX_BS}${group_name}" FILES ${f})
		ELSE()
			SOURCE_GROUP("Header Files${LUX_BS}${group_name}${LUX_BS}${f_sub}" FILES ${f})
		ENDIF()
	ENDFOREACH()
ENDFOREACH()

########################################
# LuxRays sources (merged into liblux) #
########################################

# Everything under luxrays/src is compiled into liblux.
file(GLOB_RECURSE luxrays_src CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/luxrays/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/luxrays/src/*.c")
SOURCE_GROUP("Source Files\\LuxRays" FILES ${luxrays_src})

# -fvisibility=default is required to export LuxRays symbols.
set_source_files_properties(${luxrays_src} PROPERTIES COMPILE_FLAGS "-fvisibility=default -fvisibility-inlines-hidden")

set(LUX_SOURCES
	${lux_core_generated_src}
	${lux_lib_src}
	${lux_lib_hdr}
	${lux_parser_src}
	${luxrays_src}
)

INCLUDE_DIRECTORIES(BEFORE SYSTEM
	${CMAKE_SOURCE_DIR}/core/external
	)
INCLUDE_DIRECTORIES(BEFORE
	${CMAKE_SOURCE_DIR}/core
	${CMAKE_SOURCE_DIR}/core/queryable
	${CMAKE_SOURCE_DIR}/core/reflection
	${CMAKE_SOURCE_DIR}/core/reflection/bsdf
	${CMAKE_SOURCE_DIR}/core/reflection/bxdf
	${CMAKE_SOURCE_DIR}/core/reflection/fresnel
	${CMAKE_SOURCE_DIR}/core/reflection/microfacetdistribution
	${CMAKE_SOURCE_DIR}/lights/sphericalfunction
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}
	)

###################################################
# Here we build the shared core library liblux.so #
###################################################

add_library(lux SHARED ${LUX_SOURCES})

target_link_libraries(lux PRIVATE
	OpenImageIO::OpenImageIO
	OpenEXR::OpenEXR
	OpenEXR::Iex
	Imath::Imath
	Boost::thread
	Boost::filesystem
	Boost::iostreams
	Boost::serialization
	Boost::python
	PNG::PNG
	JPEG::JPEG
	TIFF::TIFF
	FFTW3::fftw3
	Threads::Threads
	pystring::pystring
	embree
)

IF(LUX_USE_TBB)
	target_link_libraries(lux PRIVATE TBB::tbb)
ENDIF()

target_compile_definitions(lux PRIVATE LUX_INTERNAL)

set_target_properties(lux PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN"
)

#ADD_CUSTOM_TARGET(luxStatic SOURCES ${lux_lib_hdr})
