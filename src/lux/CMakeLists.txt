cmake_minimum_required(VERSION 4.0)
find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
	set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
	message("Found ccache!")
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

IF (NOT BISON_NOT_AVAILABLE)
	FIND_PACKAGE(BISON REQUIRED)
	IF (NOT BISON_FOUND)
		MESSAGE(FATAL_ERROR "bison not found - aborting")
	ENDIF (NOT BISON_FOUND)
ENDIF (NOT BISON_NOT_AVAILABLE)

IF (NOT FLEX_NOT_AVAILABLE)
	FIND_PACKAGE(FLEX REQUIRED)
	IF (NOT FLEX_FOUND)
		MESSAGE(FATAL_ERROR "flex not found - aborting")
	ENDIF (NOT FLEX_FOUND)
ENDIF (NOT FLEX_NOT_AVAILABLE)

IF (NOT BISON_NOT_AVAILABLE AND NOT FLEX_NOT_AVAILABLE)
	# Create custom command for bison/yacc
	BISON_TARGET(LuxParser ${CMAKE_SOURCE_DIR}/core/luxparse.y ${CMAKE_BINARY_DIR}/luxparse.cpp)
	SET_SOURCE_FILES_PROPERTIES(${CMAKE_BINARY_DIR}/core/luxparse.cpp GENERATED)
	#SOURCE_GROUP("Parser Files" FILES core/luxparse.y)

	# Create custom command for flex/lex
	FLEX_TARGET(
		LuxLexer
		${CMAKE_SOURCE_DIR}/core/luxlex.l
		${CMAKE_BINARY_DIR}/luxlex.cpp
		COMPILE_FLAGS "${FLEX_FLAGS}"
	)
	SET_SOURCE_FILES_PROPERTIES(${CMAKE_BINARY_DIR}/luxlex.cpp GENERATED)
	#SOURCE_GROUP("Parser Files" FILES core/luxlex.l)
	SET(lux_parser_src
		core/luxparse.y
		core/luxlex.l)
	SOURCE_GROUP("Parser Files" FILES ${lux_core_parser_src})

	ADD_FLEX_BISON_DEPENDENCY(LuxLexer LuxParser)
ENDIF (NOT BISON_NOT_AVAILABLE AND NOT FLEX_NOT_AVAILABLE)

SET(lux_core_generated_src
	${CMAKE_BINARY_DIR}/luxparse.cpp
	${CMAKE_BINARY_DIR}/luxlex.cpp
)
SOURCE_GROUP("Source Files\\Core\\Generated" FILES ${lux_core_generated_src})

SET(lux_core_src
	core/api.cpp
	core/asyncstream.cpp
	core/camera.cpp
	core/cameraresponse.cpp
	core/context.cpp
	core/contribution.cpp
	core/partialcontribution.cpp
	core/dynload.cpp
	core/exrio.cpp
	core/filedata.cpp
	core/film.cpp
	core/igiio.cpp
	core/imagereader.cpp
	core/light.cpp
	core/material.cpp
	core/osfunc.cpp
	core/paramset.cpp
	core/photonmap.cpp
	core/pngio.cpp
	core/primitive.cpp
	core/rendererstatistics.cpp
	core/renderfarm.cpp
	core/renderinghints.cpp
	core/sampling.cpp
	core/scene.cpp
	core/shape.cpp
	core/texture.cpp
	core/tgaio.cpp
	core/timer.cpp
	core/tigerhash.cpp
	core/transport.cpp
	core/util.cpp
	core/volume.cpp
	core/scheduler.cpp
	server/renderserver.cpp
)
SOURCE_GROUP("Source Files\\Core" FILES ${lux_core_src})

SET(lux_core_geometry_src
	core/geometry/raydifferential.cpp
)
SOURCE_GROUP("Source Files\\Core\\Geometry" FILES ${lux_core_geometry_src})

SET(lux_core_queryable_src
	core/queryable/queryable.cpp
	core/queryable/queryableattribute.cpp
	core/queryable/queryableregistry.cpp
)
SOURCE_GROUP("Source Files\\Core\\Queryable" FILES ${lux_core_queryable_src})

SET(lux_core_reflection_src
	core/reflection/bxdf.cpp
	core/reflection/fresnel.cpp
	core/reflection/microfacetdistribution.cpp
)
SOURCE_GROUP("Source Files\\Core\\Reflection" FILES ${lux_core_reflection_src})

SET(lux_core_reflection_bsdf_src
	core/reflection/bsdf/doublesidebsdf.cpp
	core/reflection/bsdf/layeredbsdf.cpp
	core/reflection/bsdf/mixbsdf.cpp
	core/reflection/bsdf/multibsdf.cpp
	core/reflection/bsdf/schlickbsdf.cpp
	core/reflection/bsdf/singlebsdf.cpp
)
SOURCE_GROUP("Source Files\\Core\\Reflection\\BSDF" FILES ${lux_core_reflection_bsdf_src})

SET(lux_core_reflection_bxdf_src
	core/reflection/bxdf/asperity.cpp
	core/reflection/bxdf/brdftobtdf.cpp
	core/reflection/bxdf/cooktorrance.cpp
	core/reflection/bxdf/fresnelblend.cpp
	core/reflection/bxdf/irawan.cpp
	core/reflection/bxdf/lafortune.cpp
	core/reflection/bxdf/lambertian.cpp
	core/reflection/bxdf/microfacet.cpp
	core/reflection/bxdf/nulltransmission.cpp
	core/reflection/bxdf/orennayar.cpp
	core/reflection/bxdf/schlickbrdf.cpp
	core/reflection/bxdf/schlickscatter.cpp
	core/reflection/bxdf/schlicktranslucentbtdf.cpp
	core/reflection/bxdf/specularreflection.cpp
	core/reflection/bxdf/speculartransmission.cpp
)
SOURCE_GROUP("Source Files\\Core\\Reflection\\BxDF" FILES ${lux_core_reflection_bxdf_src})

SET(lux_core_reflection_fresnel_src
	core/reflection/fresnel/fresnelcauchy.cpp
	core/reflection/fresnel/fresnelconductor.cpp
	core/reflection/fresnel/fresneldielectric.cpp
	core/reflection/fresnel/fresnelgeneral.cpp
	core/reflection/fresnel/fresnelnoop.cpp
	core/reflection/fresnel/fresnelslick.cpp
)
SOURCE_GROUP("Source Files\\Core\\Reflection\\Fresnel" FILES ${lux_core_reflection_fresnel_src})

SET(lux_core_reflection_microfacetdistribution_src
	core/reflection/microfacetdistribution/anisotropic.cpp
	core/reflection/microfacetdistribution/beckmann.cpp
	core/reflection/microfacetdistribution/blinn.cpp
	core/reflection/microfacetdistribution/schlickdistribution.cpp
	core/reflection/microfacetdistribution/wardisotropic.cpp
)
SOURCE_GROUP("Source Files\\Core\\Reflection\\Microfacet Distribution"
	FILES ${lux_core_reflection_microfacetdistribution_src}
)

SET(lux_core_all_src
	${lux_core_generated_src}
	${lux_core_src}
	${lux_core_geometry_src}
	${lux_core_queryable_src}
	${lux_core_reflection_src}
	${lux_core_reflection_bsdf_src}
	${lux_core_reflection_bxdf_src}
	${lux_core_reflection_fresnel_src}
	${lux_core_reflection_microfacetdistribution_src}
)

#############################################################################

SET(lux_accelerators_src
	accelerators/bruteforce.cpp
	accelerators/qbvhaccel.cpp
	accelerators/tabreckdtree.cpp
)
SOURCE_GROUP("Source Files\\Accelerators" FILES ${lux_accelerators_src})

# These experimental sources REQUIRE host AVX2 support.

SET(lux_experimental_src
	accelerators/bvhaccel.cpp
)
SOURCE_GROUP("Source Files\\Accelerators" FILES ${lux_experimental_src})

SET(lux_cameras_src
	cameras/environment.cpp
	cameras/perspective.cpp
	cameras/orthographic.cpp
)
SOURCE_GROUP("Source Files\\Cameras" FILES ${lux_cameras_src})

SET(lux_films_src
	film/fleximage.cpp
)
SOURCE_GROUP("Source Files\\Films" FILES ${lux_films_src})

SET(lux_filters_src
	filters/box.cpp
	filters/gaussian.cpp
	filters/mitchell.cpp
	filters/sinc.cpp
	filters/triangle.cpp
	filters/catmullrom.cpp
	filters/blackmanharris.cpp
)
SOURCE_GROUP("Source Files\\Filters" FILES ${lux_filters_src})

SET(lux_integrators_src
	integrators/bidirectional.cpp
	integrators/directlighting.cpp
	integrators/emission.cpp
	integrators/exphotonmap.cpp
	integrators/multi.cpp
	integrators/path.cpp
	integrators/single.cpp
	integrators/none.cpp
	integrators/sppm.cpp
)
SOURCE_GROUP("Source Files\\Integrators" FILES ${lux_integrators_src})

SET(lux_lights_src
	lights/area.cpp
	lights/distant.cpp
	lights/infinite.cpp
	lights/infinitesample.cpp
	lights/pointlight.cpp
	lights/projection.cpp
	lights/sphericalfunction/photometricdata_ies.cpp
	lights/sphericalfunction/sphericalfunction.cpp
	lights/sphericalfunction/sphericalfunction_ies.cpp
	lights/spot.cpp
	lights/sky.cpp
	lights/sky2.cpp
	lights/sun.cpp
)
SOURCE_GROUP("Source Files\\Lights" FILES ${lux_lights_src})

SET(lux_lights_sphericalfunction_src
	lights/sphericalfunction/photometricdata_ies.cpp
	lights/sphericalfunction/sphericalfunction.cpp
	lights/sphericalfunction/sphericalfunction_ies.cpp
)
SOURCE_GROUP("Source Files\\Lights\\Spherical Functions" FILES ${lux_lights_sphericalfunction_src})

SET(lux_materials_src
	materials/carpaint.cpp
	materials/cloth.cpp
	materials/doubleside.cpp
	materials/glass.cpp
	materials/glass2.cpp
	materials/glossy.cpp
	materials/glossy2.cpp
	materials/glossytranslucent.cpp
	materials/layeredmaterial.cpp
	materials/matte.cpp
	materials/mattetranslucent.cpp
	materials/metal.cpp
	materials/metal2.cpp
	materials/mirror.cpp
	materials/mixmaterial.cpp
	materials/null.cpp
	materials/roughglass.cpp
	materials/scattermaterial.cpp
	materials/shinymetal.cpp
	materials/velvet.cpp
)
SOURCE_GROUP("Source Files\\Materials" FILES ${lux_materials_src})

SET(lux_pixelsamplers_src
	pixelsamplers/hilbertpx.cpp
	pixelsamplers/linear.cpp
	pixelsamplers/lowdiscrepancypx.cpp
	pixelsamplers/tilepx.cpp
	pixelsamplers/vegas.cpp
)
SOURCE_GROUP("Source Files\\Pixel Samplers" FILES ${lux_pixelsamplers_src})

SET(lux_samplers_src
	samplers/lowdiscrepancy.cpp
	samplers/metrosampler.cpp
	samplers/random.cpp
)
SOURCE_GROUP("Source Files\\Samplers" FILES ${lux_samplers_src})

SET(lux_renderers_src
	renderers/samplerrenderer.cpp
	renderers/sppmrenderer.cpp
	renderers/sppm/photonsampler.cpp
	renderers/sppm/lookupaccel.cpp
	renderers/sppm/hashgrid.cpp
	renderers/sppm/parallelhashgrid.cpp
	renderers/sppm/hitpoints.cpp
	renderers/sppm/hybridhashgrid.cpp
	renderers/sppm/kdtree.cpp
)
SOURCE_GROUP("Source Files\\Renderers" FILES ${lux_renderers_src})

SET(lux_rendererstatistics_src
	renderers/statistics/samplerstatistics.cpp
	renderers/statistics/sppmstatistics.cpp
)
SOURCE_GROUP("Source Files\\Renderers\\Statistics" FILES ${lux_rendererstatistics_src})

SET(lux_shapes_src
	shapes/cone.cpp
#	shapes/cyhair/cyHairFile.h
	shapes/cylinder.cpp
	shapes/deferred.cpp
	shapes/disk.cpp
	shapes/hairfile.cpp
	shapes/heightfield.cpp
	shapes/hyperboloid.cpp
	shapes/loopsubdiv.cpp
	shapes/mesh.cpp
	shapes/meshbarytriangle.cpp
	shapes/meshmicrodisplacementtriangle.cpp
	shapes/meshquadrilateral.cpp
	shapes/meshwaldtriangle.cpp
	shapes/mikktspace/mikktspace.c
	shapes/mikktspace/weldmesh.c
	shapes/nurbs.cpp
	shapes/paraboloid.cpp
	shapes/plymesh.cpp
	shapes/plymesh/rply.c
	shapes/sphere.cpp
	shapes/stlmesh.cpp
	shapes/torus.cpp
)
SOURCE_GROUP("Source Files\\Shapes" FILES ${lux_shapes_src})

SET(lux_blender_textures_src
	textures/blender_base.cpp
	textures/blender_blend.cpp
	textures/blender_clouds.cpp
	textures/blender_distortednoise.cpp
	textures/blender_magic.cpp
	textures/blender_marble.cpp
	textures/blender_musgrave.cpp
	textures/blender_noise.cpp	
	textures/blender_stucci.cpp
	textures/blender_texlib.cpp
	textures/blender_voronoi.cpp
	textures/blender_wood.cpp
)
SOURCE_GROUP("Source Files\\Textures\\Blender" FILES ${lux_blender_textures_src})

SET(lux_uniform_textures_src
	textures/blackbody.cpp
	textures/constant.cpp
	textures/equalenergy.cpp
	textures/frequencytexture.cpp
	textures/gaussiantexture.cpp
	textures/hitpointcolor.cpp
	textures/irregulardata.cpp
	textures/lampspectrum.cpp
	textures/regulardata.cpp
	textures/tabulateddata.cpp
)
SOURCE_GROUP("Source Files\\Textures\\Uniform" FILES ${lux_uniform_textures_src})

SET(lux_fresnel_textures_src
	textures/cauchytexture.cpp
	textures/fresnelcolor.cpp
	textures/sellmeiertexture.cpp
	textures/tabulatedfresnel.cpp
)
SOURCE_GROUP("Source Files\\Textures\\Fresnel" FILES ${lux_fresnel_textures_src})

SET(lux_textures_src
	textures/add.cpp
	textures/band.cpp
	textures/bilerp.cpp
	textures/brick.cpp
	textures/checkerboard.cpp
	textures/cloud.cpp
	textures/colordepth.cpp
	textures/densitygrid.cpp
	textures/dots.cpp
	textures/exponential.cpp
	textures/fbm.cpp
	textures/harlequin.cpp
	textures/imagemap.cpp
	textures/marble.cpp
	textures/mix.cpp
	textures/multimix.cpp
	textures/scale.cpp
	textures/subtract.cpp
	textures/uv.cpp
	textures/uvmask.cpp
	textures/windy.cpp
	textures/wrinkled.cpp
)
SOURCE_GROUP("Source Files\\Textures" FILES ${lux_textures_src})

SET(lux_textures_all_src
	${lux_uniform_textures_src}
	${lux_blender_textures_src}
	${lux_fresnel_textures_src}
	${lux_textures_src}
)

SET(lux_tonemaps_src
	tonemaps/contrast.cpp
	tonemaps/falsecolors.cpp
	tonemaps/lineartonemap.cpp
	tonemaps/maxwhite.cpp
	tonemaps/nonlinear.cpp
	tonemaps/reinhard.cpp
)
SOURCE_GROUP("Source Files\\Tonemaps" FILES ${lux_tonemaps_src})

SET(lux_volumes_src
	volumes/clearvolume.cpp
	volumes/cloudvolume.cpp
	volumes/exponentialvolume.cpp
	volumes/heterogeneous.cpp
	volumes/homogeneous.cpp
	volumes/volumegrid.cpp
)
SOURCE_GROUP("Source Files\\Volumes" FILES ${lux_volumes_src})

SET(lux_modules_src
	${lux_accelerators_src}
	${lux_cameras_src}
	${lux_films_src}
	${lux_filters_src}
	${lux_integrators_src}
	${lux_lights_src}
	${lux_lights_sphericalfunction_src}
	${lux_materials_src}
	${lux_pixelsamplers_src}
	${lux_renderers_src}
	${lux_rendererstatistics_src}
	${lux_samplers_src}
	${lux_shapes_src}
	${lux_textures_all_src}
	${lux_tonemaps_src}
	${lux_volumes_src}
)

IF(AVX_FOUND)
	MESSAGE(STATUS "AVX2 detected. Including experimental sources.")
	SET(lux_lib_src
		${lux_core_all_src}
		${lux_modules_src}
		${lux_experimental_src}
	)
ELSE()
	MESSAGE(STATUS "No AVX2 support. Excluding experimental sources.")
	SET(lux_lib_src
		${lux_core_all_src}
		${lux_modules_src}
	)
ENDIF()

SET(lux_cpp_api_src
	cpp_api/dllmain.cpp
	cpp_api/lux_api.cpp
	cpp_api/lux_wrapper_factories.cpp
)
SOURCE_GROUP("Source Files\\C++ API" FILES ${lux_cpp_api_src})

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

find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)
include_directories(${Qt6_INCLUDE_DIRS})
include_directories(${Qt6_INCLUDE_DIR})
include_directories(${QT_INCLUDE_DIR})

#set(CMCSD ${CMAKE_CURRENT_SOURCE_DIR})
#set(BLD_DIR ${CMCSD}/../../bin/build)
#set(BIN_DIR ${BLD_DIR}/bin)
#set(LIB_DIR ${LIB_DIR})
#set(INC_DIR ${BLD_DIR}/include)

include_directories(${INC_DIR}/luxrays)
include_directories(${INC_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${BLD_DIR}/pfx/src/lux-build)
include_directories(${INC_DIR}/Imath)
include_directories(${INC_DIR}/OpenEXR)
include_directories(${INC_DIR}/python3.5m)

find_package(TBB REQUIRED)
include_directories(${TBB_INCLUDE_DIRS})
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DEFLATE REQUIRED libdeflate)

find_package(LibLZMA REQUIRED)
find_package(zstd REQUIRED)
find_package(BZip2 REQUIRED)
find_package(EXPAT REQUIRED)
find_package(FFTW3 REQUIRED)

set(
	BOOST_LIBS
	${LIB_DIR}/libboost_thread.a
	${LIB_DIR}/libboost_filesystem.a
	${LIB_DIR}/libboost_iostreams.a
	${LIB_DIR}/libboost_serialization.a
	${LIB_DIR}/libboost_python35.a
	${LIB_DIR}/libboost_atomic.a
	${LIB_DIR}/libboost_chrono.a
	${LIB_DIR}/libboost_container.a
	${LIB_DIR}/libboost_date_time.a
	${LIB_DIR}/libboost_graph.a
	${LIB_DIR}/libboost_program_options.a
	${LIB_DIR}/libboost_random.a
	${LIB_DIR}/libboost_wserialization.a
)

set(
	MISC_STATIC_LIBS
	${LIB_DIR}/libOpenColorIO.a
	${LIB_DIR}/libOpenImageIO.a
	${LIB_DIR}/libOpenImageIO_Util.a

	${LIB_DIR}/libOpenEXRUtil-3_4.a
	${LIB_DIR}/libOpenEXRCore-3_4.a
	${LIB_DIR}/libOpenEXR-3_4.a
	${LIB_DIR}/libIex-3_4.a
	${LIB_DIR}/libImath-3_2.a
	${LIB_DIR}/libIlmThread-3_4.a
	${LIB_DIR}/libopenjph.a

	${LIB_DIR}/libpng.a
	${LIB_DIR}/libjpeg.a
	${LIB_DIR}/libpystring.a
	${LIB_DIR}/libz-ng.a
	${LIB_DIR}/libminizip.a
	${LIB_DIR}/libppmd.a
	${LIB_DIR}/libyaml-cpp.a
	${LIB_DIR}/libtiff.a
)

set(BIN_FLAGS -mavx -mavx2 -march=native)

set(LUX_SOURCES
	${lux_cpp_api_src}
	${lux_lib_src}
	${lux_parser_src}
)

add_library(lux SHARED ${LUX_SOURCES})
target_link_libraries(lux PRIVATE
	${LIB_DIR}/libluxrays.a

	${MISC_STATIC_LIBS}
	${BOOST_LIBS}

	${FFTW3_LIBRARIES}
	Threads::Threads
	TBB::tbb
	ZLIB::ZLIB
	LibLZMA::LibLZMA
	zstd::libzstd
	${BZIP2_LIBRARIES}
	EXPAT::EXPAT
	${DEFLATE_LIBRARIES}
)
target_compile_definitions(lux PRIVATE LUX_INTERNAL)
set_target_properties(lux PROPERTIES
	LINKER_LANGUAGE CXX
	BUILD_WITH_INSTALL_RPATH TRUE
	INSTALL_RPATH "$ORIGIN"
)

SET(LUXCONSOLE_SRCS
	console/commandline.cpp
	console/luxconsole.cpp
)
SOURCE_GROUP("Source Files\\Console" FILES ${LUXCONSOLE_SRCS})

add_executable(luxconsole ${LUXCONSOLE_SRCS})
target_link_libraries(luxconsole PRIVATE
	lux

	${MISC_STATIC_LIBS}
	${BOOST_LIBS}

	${FFTW3_LIBRARIES}
	Threads::Threads
	TBB::tbb
	ZLIB::ZLIB
	LibLZMA::LibLZMA
	zstd::libzstd
	${BZIP2_LIBRARIES}
	EXPAT::EXPAT
	${DEFLATE_LIBRARIES}
)

SET(LUXQTGUI_SRCS
	qtgui/aboutdialog.cpp
	qtgui/advancedinfowidget.cpp
	qtgui/batchprocessdialog.cpp
	qtgui/colorspacewidget.cpp
	qtgui/gammawidget.cpp
	qtgui/guiutil.cpp
	qtgui/histogramview.cpp
	qtgui/histogramwidget.cpp
	qtgui/lenseffectswidget.cpp
	qtgui/lightgroupwidget.cpp
	qtgui/luxapp.cpp
	qtgui/main.cpp
	qtgui/mainwindow.cpp
	qtgui/noisereductionwidget.cpp
	qtgui/openexroptionsdialog.cpp
	qtgui/panewidget.cpp
	qtgui/queue.cpp
	qtgui/renderview.cpp
	qtgui/tonemapwidget.cpp
	console/commandline.cpp
)
SOURCE_GROUP("Source Files\\Qt GUI" FILES ${LUXQTGUI_SRCS})

SET(LUXQTGUI_MOC
	qtgui/aboutdialog.hxx
	qtgui/advancedinfowidget.hxx
	qtgui/batchprocessdialog.hxx
	qtgui/colorspacewidget.hxx
	qtgui/gammawidget.hxx
	qtgui/histogramview.hxx
	qtgui/histogramwidget.hxx
	qtgui/lenseffectswidget.hxx
	qtgui/lightgroupwidget.hxx
	qtgui/luxapp.hxx
	qtgui/mainwindow.hxx
	qtgui/noisereductionwidget.hxx
	qtgui/openexroptionsdialog.hxx
	qtgui/panewidget.hxx
	qtgui/queue.hxx
	qtgui/renderview.hxx
	qtgui/tonemapwidget.hxx
)
SOURCE_GROUP("Header Files\\Qt GUI" FILES ${LUXQTGUI_MOC} qtgui/quiutil.h console/commandline.h)

SET(LUXQTGUI_UIS
	qtgui/aboutdialog.ui
	qtgui/advancedinfo.ui
	qtgui/batchprocessdialog.ui
	qtgui/colorspace.ui
	qtgui/gamma.ui
	qtgui/histogram.ui
	qtgui/lenseffects.ui
	qtgui/lightgroup.ui
	qtgui/luxrender.ui
	qtgui/noisereduction.ui
	qtgui/openexroptionsdialog.ui
	qtgui/pane.ui
	qtgui/tonemap.ui
)
SOURCE_GROUP("UI Files\\Qt GUI" FILES ${LUXQTGUI_UIS})

SET(LUXQTGUI_RCS
	qtgui/icons.qrc
	qtgui/splash.qrc
	qtgui/images.qrc
)
SOURCE_GROUP("Resource Files\\Qt GUI" FILES ${LUXQTGUI_RCS})

QT_ADD_RESOURCES( LUXQTGUI_RC_SRCS ${LUXQTGUI_RCS})
QT_WRAP_UI( LUXQTGUI_UI_HDRS ${LUXQTGUI_UIS} )

QT_WRAP_CPP(
	LUXQTGUI_MOC_SRCS ${LUXQTGUI_MOC}
	OPTIONS -DBOOST_TT_HAS_OPERATOR_HPP_INCLUDED -DBOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION
)

qt_add_executable(
	luxrender ${GUI_TYPE} ${LUXQTGUI_SRCS} ${LUXQTGUI_MOC_SRCS} ${LUXQTGUI_RC_SRCS} ${LUXQTGUI_UI_HDRS}
)

target_link_libraries(luxrender PRIVATE
	Qt6::Core
	Qt6::Gui
	Qt6::Widgets

	lux

	${MISC_STATIC_LIBS}
	${BOOST_LIBS}
	
	${FFTW3_LIBRARIES}
	Threads::Threads
	TBB::tbb
	ZLIB::ZLIB
	LibLZMA::LibLZMA
	zstd::libzstd
	${BZIP2_LIBRARIES}
	EXPAT::EXPAT
	${DEFLATE_LIBRARIES}
)