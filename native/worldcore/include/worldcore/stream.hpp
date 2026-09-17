// stream.hpp - camera-directed cell streaming (PLAN.md "Visible-world streaming
// design", next action 4). Engine-free: the host supplies the camera and turns
// finished cells into render resources.
//
//   camera frustum + turn margin
//     visible now      -> high-priority worker jobs -> capped attaches per frame
//     prefetch         -> low-priority jobs (far edge +1 cell, velocity wedge)
//     anything else    -> no job, no mesh; queued work for it is cancelled
//
// Per frame the host calls, on its own thread:
//   update(view, now)            recompute the wanted set, (re)queue, cancel
//   take_ready(budget, cost)     finished cells it may attach this frame
//   take_retirements(max)        cells it must dispose this frame
//
// GENERATIONS: every job carries a generation id. A worker result is accepted
// only if its cell still wants exactly that generation, so a cell that was
// evicted and requested again never shows the older build, and a result for a
// cell that left the view is dropped instead of flashing in.
#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "worldcore/chunk_mesh.hpp"
#include "worldcore/field.hpp"
#include "worldcore/vmath.hpp"

namespace worldcore {

struct CellKey {
    int32_t cx = 0, cz = 0;
    int lod = 0;
    bool operator==(const CellKey& o) const { return cx == o.cx && cz == o.cz && lod == o.lod; }
};

struct CellKeyHash {
    size_t operator()(const CellKey& k) const {
        return (static_cast<size_t>(static_cast<uint32_t>(k.cx)) * 73856093u) ^
               (static_cast<size_t>(static_cast<uint32_t>(k.cz)) * 19349663u) ^ (static_cast<size_t>(k.lod) * 83492791u);
    }
};

/// Plane with an OUTWARD normal: a point is outside when n . p - d > 0
/// (Godot's Plane convention, so Camera3D::get_frustum() maps directly).
struct FrustumPlane {
    Vec3 n;
    double d = 0;
    double distance(const Vec3& p) const { return n.dot(p) - d; }
};

struct StreamView {
    Vec3 eye;
    Vec3 forward{0, 0, 1};
    Vec3 velocity;                      // for the prefetch wedge; zero is fine
    std::vector<FrustumPlane> planes;   // 6 planes, any order
};

struct StreamConfig {
    int viewCells = 6;             // max Chebyshev radius considered at all
    double turnMarginM = 24;       // side planes pushed out: cells about to turn into view
    double prefetchSeconds = 2.5;  // velocity wedge reaches this far ahead
    double prefetchHalfAngle = 0.6;  // radians either side of the velocity direction
    double evictAge = 1.5;         // seconds unwanted before a live cell retires
    double minY = -20, maxY = 260;  // vertical extent used for culling tests
    int workers = 0;               // 0: hardware_concurrency - 2, clamped to [1, 4]
};

struct FrameBudget {
    int maxAttach = 4;
    size_t maxUploadBytes = 512 * 1024;
    int maxRetire = 4;
    double frameBudgetMs = 16.7;
    /// A visible cell missing longer than this may attach one cell even while
    /// the frame is over budget, so a slow machine degrades to slow streaming
    /// instead of never streaming.
    double starveSeconds = 1.0;
};

struct ReadyCell {
    CellKey key;
    uint64_t generation = 0;
    std::shared_ptr<ChunkMesh> mesh;  // terrain cells only (default builder); null otherwise
    std::shared_ptr<void> payload;    // whatever the finisher or custom builder produced on the worker
    size_t uploadBytes = 0;
};

/// A custom cell builder (e.g. vegetation), run on a worker thread.
struct BuiltCell {
    std::shared_ptr<void> payload;
    size_t uploadBytes = 0;
};

struct RetiredCell {
    CellKey key;
    uint64_t generation = 0;
};

struct StreamStats {
    size_t wantedVisible = 0, wantedPrefetch = 0;
    size_t queued = 0, building = 0, ready = 0, live = 0;
    size_t missingVisible = 0;  // visible cells with no live mesh at any LOD
    uint64_t jobsStarted = 0, jobsBuilt = 0, cancelledQueued = 0, droppedStale = 0;
    uint64_t attached = 0, retired = 0, deferredOverBudget = 0;
    double rollingP95Ms = 0;
};

class StreamScheduler {
public:
    /// Runs on a worker after the mesh is built: convert it to engine data there.
    using Finisher = std::function<std::shared_ptr<void>(const ChunkMesh&)>;
    /// Replaces terrain meshing entirely: build whatever a cell needs from its key.
    using Builder = std::function<BuiltCell(const Field&, const CellKey&)>;

    /// Terrain cells: build_chunk_mesh() then the optional finisher.
    StreamScheduler(std::shared_ptr<const Field> field, StreamConfig config, Finisher finisher = {});
    /// Any other per-cell content (vegetation, props): `builder` runs on the workers.
    StreamScheduler(std::shared_ptr<const Field> field, StreamConfig config, Builder builder);
    ~StreamScheduler();
    StreamScheduler(const StreamScheduler&) = delete;
    StreamScheduler& operator=(const StreamScheduler&) = delete;

    static int lod_for_distance(double cells) { return cells <= 1.5 ? 0 : cells <= 3.5 ? 1 : 2; }

    /// `visibleOnly` skips prefetch entirely (boot).
    void update(const StreamView& view, double now, bool visibleOnly = false);
    /// `frameCostMs` is this frame's measured work; the scheduler keeps the rolling p95.
    std::vector<ReadyCell> take_ready(const FrameBudget& budget, double frameCostMs, double now);
    std::vector<RetiredCell> take_retirements(int maxRetire, double now);
    /// Boot: block until every visible-now cell is ready, then hand them all over
    /// regardless of budget. Never builds prefetch or anything outside the view.
    std::vector<ReadyCell> prime(const StreamView& view, double now);

    StreamStats stats() const;
    bool is_live(const CellKey& k) const;
    const std::vector<CellKey>& wanted_visible() const { return wantedVisible_; }
    const std::vector<CellKey>& wanted_prefetch() const { return wantedPrefetch_; }

private:
    enum class State { Queued, Building, Ready };
    struct Job {
        CellKey key;
        uint64_t generation;
        double score;
    };
    struct Request {
        uint64_t generation;
        State state;
        bool visible;
        double score;
    };
    struct Live {
        uint64_t generation;
        double lastWanted;
    };

    void worker_loop();
    void start_workers();
    bool cell_in_frustum(int32_t cx, int32_t cz, const std::vector<FrustumPlane>& planes, double margin) const;
    size_t missing_visible_locked() const;

    std::shared_ptr<const Field> field_;
    StreamConfig config_;
    Finisher finisher_;
    Builder builder_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;                                         // guarded
    std::vector<Job> queue_;                                        // guarded
    std::unordered_map<CellKey, Request, CellKeyHash> requests_;    // guarded
    std::deque<ReadyCell> ready_;                                   // guarded
    std::unordered_map<CellKey, Live, CellKeyHash> live_;           // engine thread only
    std::vector<CellKey> retireQueue_;                              // engine thread only
    std::vector<CellKey> wantedVisible_, wantedPrefetch_;           // engine thread only
    std::unordered_map<uint64_t, double> missingSince_;             // engine thread only, key: cx/cz
    std::array<double, 60> frameCosts_{};
    size_t frameCostCount_ = 0;
    uint64_t nextGeneration_ = 1;
    StreamStats counters_;                                          // guarded

    // Plain std::thread + a stop flag: std::jthread/stop_token are not available
    // in every standard library this must build with (Apple libc++, older MSVC).
    std::vector<std::thread> workers_;
};

/// A symmetric perspective frustum (for tests and hosts without their own).
std::vector<FrustumPlane> make_frustum(const Vec3& eye, const Vec3& forward, double vfovRad, double aspect,
                                       double near, double far);

}  // namespace worldcore
