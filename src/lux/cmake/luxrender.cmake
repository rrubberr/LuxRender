###########################################################################
#   Copyright (C) 1998-2013 by authors (see AUTHORS.txt)                  #
#                                                                         #
#   This file is part of Lux.                                             #
#                                                                         #
#   Lux is free software; you can redistribute it and/or modify           #
#   it under the terms of the GNU General Public License as published by  #
#   the Free Software Foundation; either version 3 of the License, or     #
#   (at your option) any later version.                                   #
#                                                                         #
#   Lux is distributed in the hope that it will be useful,                #
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        #
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
#   GNU General Public License for more details.                          #
#                                                                         #
#   You should have received a copy of the GNU General Public License     #
#   along with this program.  If not, see <http://www.gnu.org/licenses/>. #
#                                                                         #
#   Lux website: http://www.luxrender.net                                 #
###########################################################################

if(APPLE)
    # Check for both Apple Silicon and Intel Homebrew paths.
    set(QT_SEARCH_PATHS 
        "/opt/homebrew/opt/qt"
        "/opt/homebrew/opt/qt@6"
        "/usr/local/opt/qt"
        "/usr/local/opt/qt@6"
    )

	set(CMAKE_AUTOMOC ON)
	set(CMAKE_AUTOUIC ON)
	set(CMAKE_AUTORCC ON)

    foreach(PATH ${QT_SEARCH_PATHS})
        if(EXISTS "${PATH}")
            # Add to prefix path so find_package can see it.
            list(APPEND CMAKE_PREFIX_PATH "${PATH}")
            message(STATUS "Found Qt6 path: ${PATH}")
        endif()
    endforeach()

    # 2. Prevent CMake from only looking for system frameworks
    set(CMAKE_FIND_FRAMEWORK LAST)
endif()

FIND_PACKAGE(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

IF(Qt6_FOUND)
	MESSAGE(STATUS "Qt library directory: " ${QT_LIBRARY_DIR} )
	MESSAGE( STATUS "Qt include directory: " ${QT_INCLUDE_DIR} )

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

	if(APPLE)
    	set(GUI_TYPE MACOSX_BUNDLE)
	else()
    	# On Linux, no special keyword is needed for GUI apps.
    	set(GUI_TYPE "")
	endif()

	QT_ADD_RESOURCES( LUXQTGUI_RC_SRCS ${LUXQTGUI_RCS})
	QT_WRAP_UI( LUXQTGUI_UI_HDRS ${LUXQTGUI_UIS} )

	QT_WRAP_CPP( LUXQTGUI_MOC_SRCS ${LUXQTGUI_MOC} OPTIONS -DBOOST_TT_HAS_OPERATOR_HPP_INCLUDED -DBOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION )

	ADD_EXECUTABLE(luxrender ${GUI_TYPE} ${LUXQTGUI_SRCS} ${LUXQTGUI_MOC_SRCS} ${LUXQTGUI_RC_SRCS} ${LUXQTGUI_UI_HDRS})


	target_link_libraries(luxrender PRIVATE
    	Qt6::Core
    	Qt6::Gui
    	Qt6::Widgets
    	Boost::program_options
		Boost::filesystem
		Boost::thread
		lux
	)

	if(APPLE)
		set_target_properties(luxrender PROPERTIES
			MACOSX_BUNDLE_BUNDLE_NAME "LuxRender"
			MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/Info.plist
			
			# This is the "Magic" part:
			# It tells luxrender to look in its own 'Frameworks' folder for .so files
			INSTALL_RPATH "@executable_path"
			
			# This ensures the RPATH is actually built into the binary
			BUILD_WITH_INSTALL_RPATH TRUE
		)
	endif()

ELSE(Qt6_FOUND)
	MESSAGE( FATAL_ERROR "Warning : could not find Qt - not building Qt GUI")
ENDIF(Qt6_FOUND)
