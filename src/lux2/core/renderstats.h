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

#ifndef LUX2_RENDERSTATS_H
#define LUX2_RENDERSTATS_H

#include "core/queryable.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

namespace lux2
{

    class Film;

    // Rendering statistics for the active scene.
    class RenderStatistics : public Queryable
    {
    public:
        // Rolling window length in seconds.
        static constexpr unsigned int statisticsWindowSize = 1;

        RenderStatistics(const Film &film, unsigned int threadCount);
        ~RenderStatistics() override;

        void Reset();
        void Start();
        void Stop();

        // Wall clock seconds since Start() accounting for pause intervals.
        double ElapsedTime() const;

        // Advance the samples/sec window.
        void UpdateWindow();

        // Derived getters.
        double HaltSpp() const;  // infinity when unset
        double HaltTime() const; // infinity when unset
        double PercentHaltTimeComplete() const;
        double PercentHaltSppComplete() const;
        double PercentComplete() const; // max of the two above
        double RemainingTime() const;
        double SampleCount() const;
        double PixelCount() const;
        double SamplesPerPixel() const;
        double SamplesPerSecond() const;
        double SamplesPerSecondWindow() const;
        unsigned int ThreadCount() const { return m_threadCount; }

        // TODO: sample contribution feedback.
        double Efficiency() const { return 0.0; }
        double EfficiencyWindow() const { return 0.0; }

        class Formatted : public Queryable
        {
        public:
            explicit Formatted(RenderStatistics *rs, std::string name);
            ~Formatted() override;

            // Resolve %token% placeholders against attributes.
            std::string GetRecommendedString() const;

        protected:
            virtual std::string GetRecommendedStringTemplate() const = 0;

            std::string FmtElapsedTime() const;
            std::string FmtRemainingTime() const;
            std::string FmtHaltTime() const;

            RenderStatistics *rs;
        };

        class FormattedLong : public Formatted
        {
        public:
            explicit FormattedLong(RenderStatistics *rs);

        protected:
            std::string GetRecommendedStringTemplate() const override;
            std::string FmtPercentComplete() const;
            std::string FmtPercentHaltTimeComplete() const;
            std::string FmtSamplesPerPixel() const;
            std::string FmtPercentHaltSppComplete() const;
            std::string FmtSamplesPerSecondWindow() const;
            std::string FmtThreadCount() const;
        };

        class FormattedShort : public Formatted
        {
        public:
            explicit FormattedShort(RenderStatistics *rs);

        protected:
            std::string GetRecommendedStringTemplate() const override;
            std::string FmtPercentComplete() const;
            std::string FmtPercentHaltTimeComplete() const;
            std::string FmtSamplesPerPixel() const;
            std::string FmtPercentHaltSppComplete() const;
            std::string FmtSamplesPerSecondWindow() const;
            std::string FmtThreadCount() const;
        };

        std::unique_ptr<FormattedLong> formattedLong;
        std::unique_ptr<FormattedShort> formattedShort;

    private:
        using Clock = std::chrono::steady_clock;

        const Film &m_film;
        unsigned int m_threadCount;

        mutable std::mutex m_mutex;
        // Accumulated holds completed run intervals.
        double m_accumulated = 0.0;
        Clock::time_point m_runStart{};
        bool m_running = false;

        double m_windowStartTime = 0.0;
        double m_windowSampleCount = 0.0;
        double m_exponentialMovingAverage = 0.0;
    };

} // namespace lux2

#endif // LUX2_RENDERSTATS_H
