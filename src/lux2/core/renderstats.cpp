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

#include "core/renderstats.h"

#include "core/film.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace lux2
{

    namespace
    {
        constexpr double kInfinity = std::numeric_limits<double>::infinity();

        // HH:MM:SS from a seconds count.
        std::string FormatHMS(double seconds)
        {
            if (!std::isfinite(seconds))
                return "--:--:--";
            unsigned long s = static_cast<unsigned long>(seconds);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                          s / 3600, (s % 3600) / 60, s % 60);
            return buf;
        }

        // Reduce magnitude into k/M/G units.
        double MagnitudeReduce(double n)
        {
            if (!std::isfinite(n))
                return n;
            double a = std::fabs(n);
            if (a >= 1e12)
                return n / 1e12;
            if (a >= 1e9)
                return n / 1e9;
            if (a >= 1e6)
                return n / 1e6;
            if (a >= 1e3)
                return n / 1e3;
            return n;
        }

        const char *MagnitudePrefix(double n)
        {
            if (!std::isfinite(n))
                return "";
            double a = std::fabs(n);
            if (a >= 1e12)
                return "T";
            if (a >= 1e9)
                return "G";
            if (a >= 1e6)
                return "M";
            if (a >= 1e3)
                return "k";
            return "";
        }

        std::string FmtUnit(double v, const char *unit)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.2f %s%s",
                          MagnitudeReduce(v), MagnitudePrefix(v), unit);
            return buf;
        }

        std::string FmtPercent(double p, const char *suffix)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.0f%%%s", p, suffix);
            return buf;
        }
    } // namespace

    // ---------------------------------------------------------------------------
    // RenderStatistics
    // ---------------------------------------------------------------------------
    RenderStatistics::RenderStatistics(const Film &film, unsigned int threadCount)
        : Queryable("renderer_statistics"), m_film(film), m_threadCount(threadCount)
    {
        AddDoubleAttribute(*this, "elapsedTime", "Elapsed rendering time",
                           [this]
                           { return ElapsedTime(); });
        AddDoubleAttribute(*this, "remainingTime", "Remaining rendering time",
                           [this]
                           { return RemainingTime(); });
        AddDoubleAttribute(*this, "haltTime", "Halt rendering after time",
                           [this]
                           { return HaltTime(); });
        AddDoubleAttribute(*this, "haltSamplesPerPixel",
                           "Average samples per pixel to complete before halting",
                           [this]
                           { return HaltSpp(); });
        AddDoubleAttribute(*this, "percentHaltTimeComplete",
                           "Percent of halt time completed",
                           [this]
                           { return PercentHaltTimeComplete(); });
        AddDoubleAttribute(*this, "percentHaltSppComplete",
                           "Percent of halt S/p completed",
                           [this]
                           { return PercentHaltSppComplete(); });
        AddDoubleAttribute(*this, "percentComplete", "Percent of render completed",
                           [this]
                           { return PercentComplete(); });
        AddDoubleAttribute(*this, "samplesPerPixel", "Average samples per pixel",
                           [this]
                           { return SamplesPerPixel(); });
        AddDoubleAttribute(*this, "samplesPerSecond", "Samples per second",
                           [this]
                           { return SamplesPerSecond(); });
        AddDoubleAttribute(*this, "samplesPerSecondWindow",
                           "Samples per second in current time window",
                           [this]
                           { return SamplesPerSecondWindow(); });
        AddIntAttribute(*this, "threadCount", "Number of rendering threads",
                        [this]
                        { return static_cast<int>(ThreadCount()); });
        AddDoubleAttribute(*this, "efficiency", "Efficiency of renderer (deferred)",
                           [this]
                           { return Efficiency(); });
        AddDoubleAttribute(*this, "efficiencyWindow",
                           "Efficiency of renderer in window (deferred)",
                           [this]
                           { return EfficiencyWindow(); });

        formattedLong = std::make_unique<FormattedLong>(this);
        formattedShort = std::make_unique<FormattedShort>(this);
    }

    RenderStatistics::~RenderStatistics() = default;

    void RenderStatistics::Reset()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_accumulated = 0.0;
        m_running = false;
        m_windowStartTime = 0.0;
        m_windowSampleCount = 0.0;
        m_exponentialMovingAverage = 0.0;
    }

    void RenderStatistics::Start()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running)
        {
            m_runStart = Clock::now();
            m_running = true;
        }
    }

    void RenderStatistics::Stop()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_running)
        {
            m_accumulated +=
                std::chrono::duration<double>(Clock::now() - m_runStart).count();
            m_running = false;
        }
    }

    double RenderStatistics::ElapsedTime() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        double t = m_accumulated;
        if (m_running)
            t += std::chrono::duration<double>(Clock::now() - m_runStart).count();
        return t;
    }

    void RenderStatistics::UpdateWindow()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        double now = m_accumulated;
        if (m_running)
            now += std::chrono::duration<double>(Clock::now() - m_runStart).count();
        // Convert to total samples to match legacy samples/sec.
        const double sampleCount = m_film.SampleCount() * PixelCount();
        const double dt = now - m_windowStartTime;
        if (dt != 0.0)
        {
            const double sps = (sampleCount - m_windowSampleCount) / dt;
            if (m_exponentialMovingAverage == 0.0)
                m_exponentialMovingAverage = sps;
            m_exponentialMovingAverage +=
                std::min(1.0, dt / statisticsWindowSize) *
                (sps - m_exponentialMovingAverage);
        }
        m_windowSampleCount = sampleCount;
        m_windowStartTime = now;
    }

    double RenderStatistics::HaltSpp() const
    {
        const int h = m_film.HaltSpp();
        return h > 0 ? double(h) : kInfinity;
    }

    double RenderStatistics::HaltTime() const
    {
        const int h = m_film.HaltTime();
        return h > 0 ? double(h) : kInfinity;
    }

    double RenderStatistics::PercentHaltTimeComplete() const
    {
        return (ElapsedTime() / HaltTime()) * 100.0;
    }

    double RenderStatistics::PercentHaltSppComplete() const
    {
        return (SamplesPerPixel() / HaltSpp()) * 100.0;
    }

    double RenderStatistics::PercentComplete() const
    {
        return std::max(PercentHaltTimeComplete(), PercentHaltSppComplete());
    }

    double RenderStatistics::RemainingTime() const
    {
        double remaining = std::max(0.0, HaltTime() - ElapsedTime());
        const double spsWindow = SamplesPerSecondWindow();
        if (spsWindow > 0.0 && std::isfinite(HaltSpp()))
        {
            const double remainingSamples =
                std::max(0.0, HaltSpp() - SamplesPerPixel()) * PixelCount();
            remaining = std::min(remaining, remainingSamples / spsWindow);
        }
        return remaining;
    }

    double RenderStatistics::SampleCount() const { return m_film.SampleCount(); }

    double RenderStatistics::PixelCount() const
    {
        return double(m_film.XCount()) * double(m_film.YCount());
    }

    double RenderStatistics::SamplesPerPixel() const
    {
        // Film::SampleCount() is already the average samples per pixel.
        return m_film.SampleCount();
    }

    double RenderStatistics::SamplesPerSecond() const
    {
        const double et = ElapsedTime();
        return et == 0.0 ? 0.0 : (m_film.SampleCount() * PixelCount()) / et;
    }

    double RenderStatistics::SamplesPerSecondWindow() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_exponentialMovingAverage;
    }

    // ---------------------------------------------------------------------------
    // Formatted
    // ---------------------------------------------------------------------------
    RenderStatistics::Formatted::Formatted(RenderStatistics *rs_, std::string name)
        : Queryable(std::move(name)), rs(rs_)
    {
        AddStringAttribute(*this, "_recommended_string",
                           "Recommended statistics string",
                           [this]
                           { return GetRecommendedString(); });
        AddStringAttribute(*this, "_recommended_string_template",
                           "Recommended statistics string template",
                           [this]
                           { return GetRecommendedStringTemplate(); });
        AddStringAttribute(*this, "elapsedTime", "Elapsed rendering time",
                           [this]
                           { return FmtElapsedTime(); });
        AddStringAttribute(*this, "remainingTime", "Remaining rendering time",
                           [this]
                           { return FmtRemainingTime(); });
        AddStringAttribute(*this, "haltTime", "Halt rendering after time",
                           [this]
                           { return FmtHaltTime(); });
    }

    RenderStatistics::Formatted::~Formatted() = default;

    std::string RenderStatistics::Formatted::GetRecommendedString() const
    {
        return FormatTemplate(*this, GetRecommendedStringTemplate());
    }

    std::string RenderStatistics::Formatted::FmtElapsedTime() const
    {
        return FormatHMS(rs->ElapsedTime());
    }

    std::string RenderStatistics::Formatted::FmtRemainingTime() const
    {
        return FormatHMS(rs->RemainingTime());
    }

    std::string RenderStatistics::Formatted::FmtHaltTime() const
    {
        return FormatHMS(rs->HaltTime());
    }

    // ---------------------------------------------------------------------------
    // FormattedLong
    // ---------------------------------------------------------------------------
    RenderStatistics::FormattedLong::FormattedLong(RenderStatistics *rs)
        : Formatted(rs, "renderer_statistics_formatted")
    {
        AddStringAttribute(*this, "percentComplete", "Percent of render completed",
                           [this]
                           { return FmtPercentComplete(); });
        AddStringAttribute(*this, "percentHaltTimeComplete",
                           "Percent of halt time completed",
                           [this]
                           { return FmtPercentHaltTimeComplete(); });
        AddStringAttribute(*this, "samplesPerPixel", "Average samples per pixel",
                           [this]
                           { return FmtSamplesPerPixel(); });
        AddStringAttribute(*this, "percentHaltSppComplete",
                           "Percent of halt S/p completed",
                           [this]
                           { return FmtPercentHaltSppComplete(); });
        AddStringAttribute(*this, "samplesPerSecondWindow",
                           "Samples per second in window",
                           [this]
                           { return FmtSamplesPerSecondWindow(); });
        AddStringAttribute(*this, "threadCount", "Number of rendering threads",
                           [this]
                           { return FmtThreadCount(); });
    }

    std::string RenderStatistics::FormattedLong::GetRecommendedStringTemplate() const
    {
        std::string t = "%elapsedTime%";
        if (rs->RemainingTime() != kInfinity)
            t += " [%remainingTime%]";
        if (rs->HaltTime() != kInfinity)
            t += " (%percentHaltTimeComplete%)";
        t += " - %threadCount%";
        t += ": %samplesPerPixel%";
        if (rs->HaltSpp() != kInfinity)
            t += " (%percentHaltSppComplete%)";
        t += " %samplesPerSecondWindow%";
        return t;
    }

    std::string RenderStatistics::FormattedLong::FmtPercentComplete() const
    {
        return FmtPercent(rs->PercentComplete(), "");
    }
    std::string RenderStatistics::FormattedLong::FmtPercentHaltTimeComplete() const
    {
        return FmtPercent(rs->PercentHaltTimeComplete(), " Time");
    }
    std::string RenderStatistics::FormattedLong::FmtSamplesPerPixel() const
    {
        return FmtUnit(rs->SamplesPerPixel(), "S/p");
    }
    std::string RenderStatistics::FormattedLong::FmtPercentHaltSppComplete() const
    {
        return FmtPercent(rs->PercentHaltSppComplete(), " S/p");
    }
    std::string RenderStatistics::FormattedLong::FmtSamplesPerSecondWindow() const
    {
        return FmtUnit(rs->SamplesPerSecondWindow(), "S/s");
    }
    std::string RenderStatistics::FormattedLong::FmtThreadCount() const
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%u Threads", rs->ThreadCount());
        return buf;
    }

    // ---------------------------------------------------------------------------
    // FormattedShort
    // ---------------------------------------------------------------------------
    RenderStatistics::FormattedShort::FormattedShort(RenderStatistics *rs)
        : Formatted(rs, "renderer_statistics_formatted_short")
    {
        AddStringAttribute(*this, "percentComplete", "Percent of render completed",
                           [this]
                           { return FmtPercentComplete(); });
        AddStringAttribute(*this, "percentHaltTimeComplete",
                           "Percent of halt time completed",
                           [this]
                           { return FmtPercentHaltTimeComplete(); });
        AddStringAttribute(*this, "samplesPerPixel", "Average samples per pixel",
                           [this]
                           { return FmtSamplesPerPixel(); });
        AddStringAttribute(*this, "percentHaltSppComplete",
                           "Percent of halt S/p completed",
                           [this]
                           { return FmtPercentHaltSppComplete(); });
        AddStringAttribute(*this, "samplesPerSecondWindow",
                           "Samples per second in window",
                           [this]
                           { return FmtSamplesPerSecondWindow(); });
        AddStringAttribute(*this, "threadCount", "Number of rendering threads",
                           [this]
                           { return FmtThreadCount(); });
    }

    std::string RenderStatistics::FormattedShort::GetRecommendedStringTemplate() const
    {
        std::string t = "%elapsedTime%";
        if (rs->RemainingTime() != kInfinity)
            t += " [%remainingTime%]";
        if (rs->HaltTime() != kInfinity)
            t += " (%percentHaltTimeComplete%)";
        t += " - %threadCount%";
        t += ": %samplesPerPixel%";
        if (rs->HaltSpp() != kInfinity)
            t += " (%percentHaltSppComplete%)";
        t += " %samplesPerSecondWindow%";
        return t;
    }

    std::string RenderStatistics::FormattedShort::FmtPercentComplete() const
    {
        return FmtPercent(rs->PercentComplete(), "");
    }
    std::string RenderStatistics::FormattedShort::FmtPercentHaltTimeComplete() const
    {
        return FmtPercent(rs->PercentHaltTimeComplete(), " T");
    }
    std::string RenderStatistics::FormattedShort::FmtSamplesPerPixel() const
    {
        return FmtUnit(rs->SamplesPerPixel(), "S/p");
    }
    std::string RenderStatistics::FormattedShort::FmtPercentHaltSppComplete() const
    {
        return FmtPercent(rs->PercentHaltSppComplete(), " S/p");
    }
    std::string RenderStatistics::FormattedShort::FmtSamplesPerSecondWindow() const
    {
        return FmtUnit(rs->SamplesPerSecondWindow(), "S/s");
    }
    std::string RenderStatistics::FormattedShort::FmtThreadCount() const
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%u T", rs->ThreadCount());
        return buf;
    }

} // namespace lux2
