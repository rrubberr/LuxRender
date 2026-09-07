/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   LuxRender is free software; you can redistribute it and/or modify     *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   any later version                                                     *
 *                                                                         *
 *   LuxRender is distributed in the hope that it will be useful,          *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>  *
 *                                                                         *
 *   This project is based on PBRT; see http://www.pbrt.org                *
 ***************************************************************************/

#ifndef LUX_TBBRENDERCOMMON_H
#define LUX_TBBRENDERCOMMON_H

#ifdef LUX_USE_TBB

#include <tbb/global_control.h>
#include <tbb/task_arena.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

#include <atomic>

namespace lux {

// Establish a single tbb::global_control for the process
// before Embree device init, and destroyed after the render.

// Embree gets rtcNewDevice(nullptr) to avoid a competing global_control.
class TBBRenderScope {
public:
	explicit TBBRenderScope(unsigned threadCount)
		: control(tbb::global_control::max_allowed_parallelism,
				threadCount == 0u ? 1u : threadCount) {
	}

private:
	tbb::global_control control;
};

}//namespace lux

#endif // LUX_USE_TBB

#endif // LUX_TBBRENDERCOMMON_H
