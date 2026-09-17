// test_stream.cpp - StreamScheduler against the PLAN.md milestone-2 exit gate:
// "turning the camera never causes an all-ring load; stale cell jobs are
// discarded; no visible terrain holes", plus budgets, generations and the
// never-open-a-hole retirement rule. Frames are simulated; workers are real.
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <unordered_map>

#include "worldcore/stream.hpp"
#include "worldcore/world.hpp"

using namespace worldcore;

namespace {

int g_fail = 0, g_checks = 0;

void check(bool ok, const std::string& what) {
    g_checks++;
    if (!ok) {
        g_fail++;
        std::fprintf(stderr, "FAIL %s\n", what.c_str());
    }
}

constexpr double kFov = 70.0 * 3.14159265358979 / 180.0;
constexpr double kAspect = 16.0 / 9.0;

StreamView view_at(const Vec3& eye, double yaw, const Vec3& velocity = {}) {
    StreamView v;
    v.eye = eye;
    v.forward = {std::sin(yaw), -0.18, std::cos(yaw)};
    v.forward = v.forward.normalized();
    v.velocity = velocity;
    v.planes = make_frustum(v.eye, v.forward, kFov, kAspect, 0.1, 1400);
    return v;
}

bool contains(const std::vector<CellKey>& list, const CellKey& k) {
    for (const auto& x : list) {
        if (x == k) return true;
    }
    return false;
}

/// The host side of the contract: what is attached, checked on every frame.
struct Host {
    StreamScheduler& s;
    std::unordered_map<CellKey, uint64_t, CellKeyHash> attached;
    size_t maxLive = 0;
    int badAttach = 0, badRetire = 0, overBudget = 0;

    void frame(const StreamView& v, double now, double costMs, const FrameBudget& b) {
        s.update(v, now);
        const auto ready = s.take_ready(b, costMs, now);
        size_t bytes = 0;
        if (static_cast<int>(ready.size()) > b.maxAttach) overBudget++;
        for (const auto& r : ready) {
            bytes += r.uploadBytes;
            // Only cells the view wants right now may appear.
            if (!contains(s.wanted_visible(), r.key) && !contains(s.wanted_prefetch(), r.key)) badAttach++;
            attached[r.key] = r.generation;
        }
        if (ready.size() > 1 && bytes > b.maxUploadBytes) overBudget++;
        for (const auto& r : s.take_retirements(b.maxRetire, now)) {
            auto it = attached.find(r.key);
            if (it == attached.end() || it->second != r.generation) badRetire++;
            else attached.erase(it);
        }
        maxLive = std::max(maxLive, attached.size());
    }
};

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

}  // namespace

int main() {
    auto field = std::make_shared<Field>(1337);
    const Vec3 spawn{2.1, 29.9, -23.9};
    const int fullRing = (2 * 6 + 1) * (2 * 6 + 1);
    FrameBudget budget;

    /* ---- boot: visible cells only, never the ring, never prefetch ---- */
    {
        StreamConfig cfg;
        cfg.workers = 2;
        StreamScheduler s(field, cfg);
        const auto t0 = std::chrono::steady_clock::now();
        const auto boot = s.prime(view_at(spawn, 0), 0);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const auto st = s.stats();
        std::printf("boot: %zu cells (full ring would be %d) in %.1f ms, %llu jobs\n", boot.size(), fullRing, ms,
                    static_cast<unsigned long long>(st.jobsStarted));
        check(boot.size() == s.wanted_visible().size(), "boot returns exactly the visible set");
        check(boot.size() < static_cast<size_t>(fullRing) / 2, "boot is far smaller than the square ring");
        check(st.jobsStarted == boot.size(), "boot built no prefetch or extra cells");
        check(st.missingVisible == 0, "no holes after boot");
        int behind = 0;
        for (const auto& c : boot) {
            // The camera looks along +Z: a cell whose far edge is behind the eye is behind it.
            if ((c.key.cz + 1) * kChunk < spawn.z - 1) behind++;
        }
        check(behind == 0, "boot built nothing behind the camera");
        // Worker meshes are the same as a direct build.
        const auto direct = build_chunk_mesh(*field, boot[0].key.cx, boot[0].key.cz, boot[0].key.lod);
        check(direct.positions == boot[0].mesh->positions && direct.indices == boot[0].mesh->indices,
              "worker mesh identical to direct build");
    }

    /* ---- turning in place: bounded residency, stale work discarded, no lasting holes ---- */
    {
        StreamConfig cfg;
        cfg.workers = 1;  // deliberately starved so the turn outruns the builder
        StreamScheduler s(field, cfg);
        Host host{s};
        for (const auto& r : s.prime(view_at(spawn, 0), 0)) host.attached[r.key] = r.generation;

        double now = 0, yaw = 0;
        int holeFrames = 0, frames = 0;
        for (int i = 0; i < 60 * 8; i++) {  // 8 s at 60 Hz, 2 rad/s: faster than a player turns
            now += 1.0 / 60;
            yaw += 2.0 / 60;
            host.frame(view_at(spawn, yaw), now, 6.0, budget);
            if (s.stats().missingVisible > 0) holeFrames++;
            frames++;
            sleep_ms(2);
        }
        // Stop turning: every visible cell must fill in.
        int settle = 0;
        while (s.stats().missingVisible > 0 && settle < 600) {
            now += 1.0 / 60;
            host.frame(view_at(spawn, yaw), now, 6.0, budget);
            settle++;
            sleep_ms(2);
        }
        const auto st = s.stats();
        std::printf("turn: max live %zu (ring %d), jobs %llu, cancelled %llu, stale dropped %llu, hole frames %d/%d, settled in %d frames\n",
                    host.maxLive, fullRing, static_cast<unsigned long long>(st.jobsStarted),
                    static_cast<unsigned long long>(st.cancelledQueued), static_cast<unsigned long long>(st.droppedStale),
                    holeFrames, frames, settle);
        check(host.badAttach == 0, "never attached a cell the view did not want");
        check(host.badRetire == 0, "retirements match attached generations");
        check(host.overBudget == 0, "attach count and upload bytes within budget");
        check(host.maxLive < static_cast<size_t>(fullRing), "residency stays below the full ring while turning");
        check(st.cancelledQueued + st.droppedStale > 0, "stale jobs were cancelled or dropped");
        check(st.missingVisible == 0, "no holes once the camera stops");
    }

    /* ---- normal turn speed with enough workers: holes stay rare ---- */
    {
        StreamConfig cfg;
        cfg.workers = 3;
        StreamScheduler s(field, cfg);
        Host host{s};
        for (const auto& r : s.prime(view_at(spawn, 0), 0)) host.attached[r.key] = r.generation;
        double now = 0, yaw = 0;
        int holeFrames = 0;
        const int frames = 60 * 7;
        for (int i = 0; i < frames; i++) {
            now += 1.0 / 60;
            yaw += 0.9 / 60;  // turn-in-place-v1
            host.frame(view_at(spawn, yaw), now, 6.0, budget);
            if (s.stats().missingVisible > 0) holeFrames++;
            sleep_ms(4);
        }
        std::printf("turn-in-place-v1 speed: hole frames %d/%d, max live %zu\n", holeFrames, frames, host.maxLive);
        check(holeFrames * 20 < frames, "fewer than 5% of frames show a missing visible cell at 0.9 rad/s");
    }

    /* ---- frame budget: nothing attaches while p95 is over budget, unless starving ---- */
    {
        StreamConfig cfg;
        cfg.workers = 2;
        StreamScheduler s(field, cfg);
        const StreamView v = view_at(spawn, 0);
        double now = 0;
        size_t attachedSlow = 0;
        for (int i = 0; i < 30; i++) {  // 0.5 s: under the starvation threshold
            now += 1.0 / 60;
            s.update(v, now);
            attachedSlow += s.take_ready(budget, 40.0, now).size();
            sleep_ms(5);
        }
        check(attachedSlow == 0, "no attaches while over budget and not starving");
        size_t starvingMaxPerFrame = 0, starvingTotal = 0;
        for (int i = 0; i < 90; i++) {  // past 1 s missing: one per frame allowed
            now += 1.0 / 60;
            s.update(v, now);
            const size_t n = s.take_ready(budget, 40.0, now).size();
            starvingMaxPerFrame = std::max(starvingMaxPerFrame, n);
            starvingTotal += n;
            sleep_ms(5);
        }
        std::printf("over budget: %zu attached before starvation, %zu after (max %zu/frame), deferred %llu\n",
                    attachedSlow, starvingTotal, starvingMaxPerFrame,
                    static_cast<unsigned long long>(s.stats().deferredOverBudget));
        check(starvingTotal > 0 && starvingMaxPerFrame == 1, "starving streams one cell per frame");
    }

    /* ---- generations: a result for a cell that left and came back is never attached ---- */
    {
        StreamConfig cfg;
        cfg.workers = 2;
        StreamScheduler s(field, cfg);
        const StreamView here = view_at(spawn, 0);
        const StreamView away = view_at({spawn.x + 40 * kChunk, spawn.y, spawn.z}, 0);
        s.update(here, 0);
        const std::vector<CellKey> first = s.wanted_visible();
        for (int i = 0; i < 400 && s.stats().ready < first.size(); i++) sleep_ms(2);
        s.update(away, 1);  // everything built for `here` is now stale
        const auto afterAway = s.stats();
        check(afterAway.droppedStale >= first.size(), "ready results for abandoned cells dropped");
        s.update(here, 2);
        for (int i = 0; i < 400 && s.stats().ready < first.size(); i++) sleep_ms(2);
        FrameBudget all;
        all.maxAttach = 1000;
        all.maxUploadBytes = SIZE_MAX;
        const auto ready = s.take_ready(all, 1.0, 2);
        uint64_t minGen = UINT64_MAX;
        for (const auto& r : ready) minGen = std::min(minGen, r.generation);
        // Generations 1..|first| belonged to the first visit (plus prefetch); the
        // `away` visit consumed more; anything attached now must be newer than both.
        check(!ready.empty() && minGen > afterAway.wantedVisible + afterAway.wantedPrefetch + first.size(),
              "attached cells carry the new generation, not the abandoned one");
    }

    /* ---- retirement: capped per frame, all eventually, never the last cover of a visible cell ---- */
    {
        StreamConfig cfg;
        cfg.workers = 3;
        cfg.evictAge = 0.25;
        StreamScheduler s(field, cfg);
        Host host{s};
        for (const auto& r : s.prime(view_at(spawn, 0), 0)) host.attached[r.key] = r.generation;
        const size_t booted = host.attached.size();
        double now = 0;
        size_t maxRetiredPerFrame = 0;
        const StreamView far = view_at({spawn.x + 60 * kChunk, spawn.y, spawn.z}, 0);
        for (int i = 0; i < 240; i++) {
            now += 1.0 / 60;
            host.frame(far, now, 5.0, budget);
            sleep_ms(3);
        }
        FrameBudget tiny = budget;
        tiny.maxRetire = 2;
        // A second scheduler with a tight cap: count retirements per frame directly.
        StreamScheduler s2(field, cfg);
        Host h2{s2};
        for (const auto& r : s2.prime(view_at(spawn, 0), 0)) h2.attached[r.key] = r.generation;
        for (int i = 0; i < 120; i++) {
            now += 1.0 / 60;
            s2.update(far, now);
            const auto gone = s2.take_retirements(tiny.maxRetire, now);
            maxRetiredPerFrame = std::max(maxRetiredPerFrame, gone.size());
            for (const auto& g : gone) h2.attached.erase(g.key);
        }
        std::printf("retire: booted %zu, %zu attached after the jump; capped run max %zu/frame, %zu remaining\n",
                    booted, host.attached.size(), maxRetiredPerFrame, h2.attached.size());
        check(maxRetiredPerFrame <= 2, "retirements respect the per-frame cap");
        check(h2.attached.empty(), "all out-of-view cells eventually retire");
        bool oldCellsGone = true;
        for (const auto& [k, g] : host.attached) {
            if (std::abs(k.cx - to_chunk(spawn.x)) < 20) oldCellsGone = false;
        }
        check(oldCellsGone, "the jump retired every cell from the old view");

        // LOD change: approach a cell until its LOD rises; the old LOD must stay
        // live until the new one attaches.
        StreamConfig slow;
        slow.workers = 1;
        slow.evictAge = 0.0;
        StreamScheduler s3(field, slow);
        Host h3{s3};
        for (const auto& r : s3.prime(view_at(spawn, 0), 0)) h3.attached[r.key] = r.generation;
        int holes = 0;
        for (int i = 0; i < 300; i++) {
            now += 1.0 / 60;
            const Vec3 eye{spawn.x, spawn.y, spawn.z + i * 1.5};  // drive north at 90 m/s: LODs churn
            h3.frame(view_at(eye, 0, {0, 0, 90}), now, 5.0, budget);
            if (i > 30 && s3.stats().missingVisible > 3) holes++;
            sleep_ms(2);
        }
        check(h3.badRetire == 0, "LOD churn retirements consistent");
        std::printf("lod churn at 90 m/s: frames with >3 missing visible cells: %d/270\n", holes);
    }

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
