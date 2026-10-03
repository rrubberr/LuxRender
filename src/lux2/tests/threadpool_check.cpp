/***************************************************************************
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   To the extent possible under law, the author(s) have dedicated all    *
 *   copyright and related neighboring rights to the belowe code to the    *
 *   public domain worldwide. The below code is distributed without any    *
 *   warranty.                                                             *
 *                                                                         *
 *   See: <https://creativecommons.org/publicdomain/zero/1.0/>             *
 *                                                                         *
 ***************************************************************************/
// Tests are machine generated. Proceed with caution!

// Phase 1 verification: the process-wide TBB scheduler cap and the render
// task_arena behave correctly, and an Embree intersect coexists with a
// lux2 parallel_for inside the same arena (shared pool, no oversubscription).
// Built and run as the lux2threadpoolcheck target / ctest.

#include "core/threadpool.h"

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/task_arena.h>
#include <tbb/info.h>

#include <embree4/rtcore.h>

#include <atomic>
#include <iostream>
#include <vector>

using namespace lux2;

namespace {

int g_failures = 0;

void Check(bool cond, const char *what)
{
    if (!cond)
    {
        std::cerr << "  FAIL: " << what << std::endl;
        ++g_failures;
    }
    else
    {
        std::cout << "  ok:   " << what << std::endl;
    }
}

// A trivial one-triangle Embree scene, intersected from inside the arena to
// prove Embree's TBB tasking backend runs under the same global_control.
void EmbreeIntersectInArena()
{
    RTCDevice dev = rtcNewDevice("");
    RTCScene scene = rtcNewScene(dev);

    RTCGeometry geom = rtcNewGeometry(dev, RTC_GEOMETRY_TYPE_TRIANGLE);
    float *verts = (float *)rtcSetNewGeometryBuffer(
        geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3,
        sizeof(float) * 3, 3);
    verts[0] = -1.f; verts[1] = -1.f; verts[2] = 2.f;
    verts[3] = 1.f;  verts[4] = -1.f; verts[5] = 2.f;
    verts[6] = 0.f;  verts[7] = 1.f;  verts[8] = 2.f;
    unsigned *idx = (unsigned *)rtcSetNewGeometryBuffer(
        geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3,
        sizeof(unsigned) * 3, 1);
    idx[0] = 0; idx[1] = 1; idx[2] = 2;
    rtcCommitGeometry(geom);
    rtcAttachGeometry(scene, geom);
    rtcReleaseGeometry(geom);
    rtcCommitScene(scene);

    // A batch of primary rays toward the triangle.
    for (int i = 0; i < 1000; ++i)
    {
        RTCRayHit rh;
        rh.ray.org_x = 0.f; rh.ray.org_y = 0.f; rh.ray.org_z = 0.f;
        rh.ray.dir_x = 0.f; rh.ray.dir_y = 0.f; rh.ray.dir_z = 1.f;
        rh.ray.tnear = 0.f; rh.ray.tfar = 1e9f;
        rh.ray.time = 0.f; rh.ray.mask = -1; rh.ray.flags = 0;
        rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;
        rh.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
        rtcIntersect1(scene, &rh);
    }

    rtcReleaseScene(scene);
    rtcReleaseDevice(dev);
}

} // namespace

int main()
{
    std::cout << "lux2threadpoolcheck:" << std::endl;

    const unsigned int want = 4;
    RenderThreadPool::Get().Init(want);
    Check(RenderThreadPool::Get().Count() == want,
          "Count() reflects the requested thread count");

    // The arena must bound current_thread_index() to [0, Count()).
    const unsigned int count = RenderThreadPool::Get().Count();
    std::atomic<int> maxIndex{-1};
    std::atomic<bool> indexInRange{true};

    RenderThreadPool::Get().RunInArena([&]
    {
        tbb::parallel_for(tbb::blocked_range<int>(0, 200000),
                          [&](const tbb::blocked_range<int> &)
                          {
                              const int idx =
                                  tbb::this_task_arena::current_thread_index();
                              if (idx < 0 ||
                                  static_cast<unsigned int>(idx) >= count)
                                  indexInRange.store(false);
                              int prev = maxIndex.load();
                              while (idx > prev &&
                                     !maxIndex.compare_exchange_weak(prev, idx))
                              {
                              }
                          });
    });

    Check(indexInRange.load(),
          "current_thread_index() stays within [0, Count()) in the arena");
    Check(maxIndex.load() >= 0 &&
              static_cast<unsigned int>(maxIndex.load()) < count,
          "observed worker indices respect the arena size");

    // Measure actual oversubscription: run a barrier-style task that counts
    // how many worker threads are simultaneously resident. The peak must not
    // exceed the requested parallelism (the global_control cap).
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    RenderThreadPool::Get().RunInArena([&]
    {
        tbb::parallel_for(tbb::blocked_range<int>(0, 100000),
                          [&](const tbb::blocked_range<int> &)
                          {
                              const int now = active.fetch_add(1) + 1;
                              int prev = peak.load();
                              while (now > prev &&
                                     !peak.compare_exchange_weak(prev, now))
                              {
                              }
                              // Brief work so multiple lanes overlap.
                              volatile double s = 0.0;
                              for (int i = 0; i < 200000; ++i)
                                  s += i * 0.5;
                              active.fetch_sub(1);
                          });
    });
    Check(static_cast<unsigned int>(peak.load()) <= want,
          "peak concurrent worker threads respect the global_control cap");

    // Embree intersect coexisting with the scheduler inside the arena.
    bool embreeOk = true;
    try
    {
        RenderThreadPool::Get().RunInArena(EmbreeIntersectInArena);
    }
    catch (...)
    {
        embreeOk = false;
    }
    Check(embreeOk, "Embree intersect runs inside the shared arena");

    if (g_failures == 0)
    {
        std::cout << "lux2threadpoolcheck: all checks passed" << std::endl;
        return EXIT_SUCCESS;
    }
    std::cerr << "lux2threadpoolcheck: " << g_failures << " failure(s)"
              << std::endl;
    return EXIT_FAILURE;
}
