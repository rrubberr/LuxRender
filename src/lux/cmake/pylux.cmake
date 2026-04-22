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

# Find Python (interpreter + headers + lib)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

message(STATUS "Python executable: ${Python3_EXECUTABLE}")
message(STATUS "Python include dir: ${Python3_INCLUDE_DIRS}")
message(STATUS "Python libraries: ${Python3_LIBRARIES}")

# Include dirs
include_directories(BEFORE SYSTEM ${Python3_INCLUDE_DIRS})

IF(Python3_FOUND)

	SOURCE_GROUP("Source Files\\Python" FILES python/binding.cpp)
	SOURCE_GROUP("Header Files\\Python" FILES
		python/binding.h
		python/pycontext.h
		python/pydoc.h
		python/pydoc_context.h
		python/pydoc_renderserver.h
		python/pydynload.h
		python/pyfleximage.h
		python/pyrenderserver.h
		)

	ADD_LIBRARY(pylux MODULE python/binding.cpp)

		target_link_libraries(pylux PRIVATE
    		lux
    		Threads::Threads
    		Boost::python
			Boost::thread
			pystring::pystring
		)
		
		SET_TARGET_PROPERTIES(pylux PROPERTIES
		PREFIX ""
		BUILD_WITH_INSTALL_RPATH TRUE
		BUILD_RPATH "$ORIGIN"
)
		

ELSE(Python3_FOUND)
	MESSAGE( STATUS "Warning: could not find Python libraries - not building python module")
ENDIF(Python3_FOUND)
