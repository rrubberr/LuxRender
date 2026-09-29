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

#include "core/displaytimer.h"

#include "core/film.h"
#include "core/renderstats.h"

#include <chrono>

namespace lux2
{

    namespace
    {
        // Granularity of the timer loop.
        constexpr auto kTick = std::chrono::seconds(1);
    } // namespace

    DisplayTimer::DisplayTimer(Film &film, RenderStatistics *stats)
        : m_film(film), m_stats(stats)
    {
    }

    DisplayTimer::~DisplayTimer()
    {
        Stop();
    }

    void DisplayTimer::Start()
    {
        if (m_thread.joinable())
            return;
        m_stop.store(false);
        m_joined = false;
        m_thread = std::thread([this]
                               { Run(); });
    }

    void DisplayTimer::Stop()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop.store(true);
        }
        m_cv.notify_all();
        if (m_thread.joinable() && !m_joined)
        {
            m_thread.join();
            m_joined = true;
        }
    }

    void DisplayTimer::Run()
    {
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();

        // Seconds since the last refresh / write.
        double lastDisplay = 0.0;
        double lastWrite = 0.0;

        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait_for(lock, kTick, [this]
                              { return m_stop.load(); });
                if (m_stop.load())
                    return;
            }

            const double now =
                std::chrono::duration<double>(Clock::now() - start).count();

            const int displayInterval = m_film.DisplayInterval();
            if (displayInterval > 0 && now - lastDisplay >= displayInterval)
            {
                m_film.UpdateFrameBuffer();
                if (m_stats)
                    m_stats->UpdateWindow();
                lastDisplay = now;
            }

            const int writeInterval = m_film.WriteInterval();
            if (writeInterval > 0 && now - lastWrite >= writeInterval)
            {
                m_film.WriteImage(IMAGE_FILEOUTPUT);
                lastWrite = now;
            }
        }
    }

} // namespace lux2
