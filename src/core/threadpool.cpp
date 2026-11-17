/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   LuxRender is free software; you can redistribute it and/or modify     *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   any later version.                                                    *
 *                                                                         *
 *   LuxRender is distributed in the hope that it will be useful,          *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program. If not, see <http://www.gnu.org/licenses/>   *
 *                                                                         *
 *   This project is based on PBRT; see <http://www.pbrt.org>              *
 ***************************************************************************/

#include "core/threadpool.h"
#include "core/error.h"

#include <thread>

namespace lux2
{

    RenderThreadPool &RenderThreadPool::Get()
    {
        // Intentionally leaked.
        static RenderThreadPool *instance = new RenderThreadPool();
        return *instance;
    }

    void RenderThreadPool::Init(unsigned int nThreads)
    {
        if (nThreads == 0)
        {
            nThreads = std::thread::hardware_concurrency();
            if (nThreads == 0)
                nThreads = 1;
        }

        if (!m_control)
        {
            // First Init sets the cap.
            m_control = std::make_unique<tbb::global_control>(
                tbb::global_control::max_allowed_parallelism,
                static_cast<size_t>(nThreads));
            m_count = nThreads;
            LOG(LUX_INFO, LUX_NOERROR)
                << "lux2: TBB scheduler capped at " << nThreads
                << " worker(s) (shared with Embree).";
        }
        else
        {
            // The parallelism cap is fixed for the process.
            m_count = nThreads;
        }
    }

} // namespace lux2
