#include "worldcore/stream.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_set>

#include "worldcore/world.hpp"

namespace worldcore {

namespace {

uint64_t cell_id(int32_t cx, int32_t cz) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cz);
}

size_t upload_bytes(const ChunkMesh& m) {
    // position + normal + colour(rgba) + custom0 as the engine stores them, plus indices.
    return m.vertex_count() * (12 + 12 + 16 + 12) + m.indices.size() * 4;
}

}  // namespace

StreamScheduler::StreamScheduler(std::shared_ptr<const Field> field, StreamConfig config, Finisher finisher)
    : field_(std::move(field)), config_(config), finisher_(std::move(finisher)) {
    start_workers();
}

StreamScheduler::StreamScheduler(std::shared_ptr<const Field> field, StreamConfig config, Builder builder)
    : field_(std::move(field)), config_(config), builder_(std::move(builder)) {
    start_workers();
}

void StreamScheduler::start_workers() {
    int n = config_.workers;
    if (n <= 0) n = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 2, 1, 4);
    for (int i = 0; i < n; i++) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

StreamScheduler::~StreamScheduler() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) w.join();
}

/* ---------------------------------------------------------------- culling -- */

bool StreamScheduler::cell_in_frustum(int32_t cx, int32_t cz, const std::vector<FrustumPlane>& planes,
                                      double margin) const {
    const Vec3 lo{cx * kChunk, config_.minY, cz * kChunk};
    const Vec3 hi{lo.x + kChunk, config_.maxY, lo.z + kChunk};
    for (const auto& p : planes) {
        // The corner deepest inside this plane; if even it is outside, the box is.
        const Vec3 c{p.n.x > 0 ? lo.x : hi.x, p.n.y > 0 ? lo.y : hi.y, p.n.z > 0 ? lo.z : hi.z};
        if (p.distance(c) > margin) return false;
    }
    return true;
}

std::vector<FrustumPlane> make_frustum(const Vec3& eye, const Vec3& forward, double vfovRad, double aspect,
                                       double near, double far) {
    const Vec3 f = forward.normalized();
    Vec3 r = f.cross({0, 1, 0});
    if (r.length_sq() < 1e-9) r = {1, 0, 0};
    r = r.normalized();
    const Vec3 u = r.cross(f).normalized();
    const double hy = vfovRad / 2;
    const double hx = std::atan(std::tan(hy) * aspect);

    auto through_eye = [&](const Vec3& n) { return FrustumPlane{n, n.dot(eye)}; };
    std::vector<FrustumPlane> planes;
    planes.push_back({f * -1, (f * -1).dot(eye + f * near)});
    planes.push_back({f, f.dot(eye + f * far)});
    planes.push_back(through_eye(r * std::cos(hx) - f * std::sin(hx)));
    planes.push_back(through_eye(r * -std::cos(hx) - f * std::sin(hx)));
    planes.push_back(through_eye(u * std::cos(hy) - f * std::sin(hy)));
    planes.push_back(through_eye(u * -std::cos(hy) - f * std::sin(hy)));
    return planes;
}

/* ----------------------------------------------------------------- update -- */

void StreamScheduler::update(const StreamView& view, double now, bool visibleOnly) {
    const int32_t ecx = to_chunk(view.eye.x), ecz = to_chunk(view.eye.z);
    const int R = config_.viewCells + 1;
    Vec3 flatFwd{view.forward.x, 0, view.forward.z};
    flatFwd = flatFwd.normalized();
    const Vec3 flatVel{view.velocity.x, 0, view.velocity.z};
    const double speed = flatVel.length();
    const Vec3 velDir = speed > 1e-6 ? flatVel * (1 / speed) : Vec3{};
    const double wedgeReach = speed * config_.prefetchSeconds + kChunk * 0.75;

    struct Want {
        CellKey key;
        bool visible;
        double score;
    };
    std::vector<Want> wanted;
    for (int dz = -R; dz <= R; dz++) {
        for (int dx = -R; dx <= R; dx++) {
            const int32_t cx = ecx + dx, cz = ecz + dz;
            const Vec3 centre{(cx + 0.5) * kChunk, view.eye.y, (cz + 0.5) * kChunk};
            const Vec3 to = centre - view.eye;
            const double distM = std::hypot(to.x, to.z);
            const double dist = distM / kChunk;
            const Vec3 dir = distM > 1e-6 ? Vec3{to.x / distM, 0, to.z / distM} : flatFwd;

            bool visible = false, prefetch = false;
            const bool inView = cell_in_frustum(cx, cz, view.planes, 0);
            if (inView && dist <= config_.viewCells + 0.5) {
                visible = true;
            } else if (inView && dist <= config_.viewCells + 1.5) {
                prefetch = true;  // one cell past the far edge
            } else if (dist <= config_.viewCells + 0.5 && cell_in_frustum(cx, cz, view.planes, config_.turnMarginM)) {
                prefetch = true;  // about to turn into view
            } else if (speed > 1.0 && dist <= config_.viewCells + 0.5) {
                const double along = to.x * velDir.x + to.z * velDir.z;
                const double angle = std::acos(std::clamp(dir.dot(velDir), -1.0, 1.0));
                // The cell the eye is in always counts: its centre can sit behind the eye.
                if ((along >= -kChunk * 0.5 && along <= wedgeReach && angle <= config_.prefetchHalfAngle) || dist < 0.75) {
                    prefetch = true;
                }
            }
            if (!visible && (!prefetch || visibleOnly)) continue;

            const double depth = std::max(0.0, to.x * flatFwd.x + to.z * flatFwd.z) / kChunk;
            const double score = (visible ? 0 : 1000) + depth + 0.25 * dist - 0.5 * dir.dot(flatFwd);
            wanted.push_back({{cx, cz, lod_for_distance(dist)}, visible, score});
        }
    }

    wantedVisible_.clear();
    wantedPrefetch_.clear();
    std::unordered_map<CellKey, const Want*, CellKeyHash> wantedMap;
    for (const Want& w : wanted) {
        wantedMap[w.key] = &w;
        (w.visible ? wantedVisible_ : wantedPrefetch_).push_back(w.key);
    }

    {
        std::lock_guard lock(mutex_);
        counters_.wantedVisible = wantedVisible_.size();
        counters_.wantedPrefetch = wantedPrefetch_.size();

        // Cancel whatever the view no longer wants, at any stage short of attached.
        for (auto it = requests_.begin(); it != requests_.end();) {
            if (wantedMap.count(it->first)) {
                ++it;
                continue;
            }
            const CellKey key = it->first;
            const uint64_t gen = it->second.generation;
            switch (it->second.state) {
                case State::Queued:
                    std::erase_if(queue_, [&](const Job& j) { return j.key == key && j.generation == gen; });
                    counters_.cancelledQueued++;
                    break;
                case State::Building:
                    break;  // the worker finds its generation gone and drops the result
                case State::Ready:
                    std::erase_if(ready_, [&](const ReadyCell& r) { return r.key == key && r.generation == gen; });
                    counters_.droppedStale++;
                    break;
            }
            it = requests_.erase(it);
        }

        bool queuedAny = false;
        for (const Want& w : wanted) {
            if (auto live = live_.find(w.key); live != live_.end()) {
                live->second.lastWanted = now;
                continue;
            }
            if (auto req = requests_.find(w.key); req != requests_.end()) {
                req->second.score = w.score;
                req->second.visible = w.visible;
                for (Job& j : queue_) {
                    if (j.key == w.key) j.score = w.score;
                }
                continue;
            }
            const uint64_t gen = nextGeneration_++;
            requests_[w.key] = {gen, State::Queued, w.visible, w.score};
            queue_.push_back({w.key, gen, w.score});
            queuedAny = true;
        }
        if (queuedAny) cv_.notify_all();
    }

    // Coverage bookkeeping for the starvation guard and the hole metric.
    std::unordered_set<uint64_t> visibleCells;
    for (const CellKey& k : wantedVisible_) visibleCells.insert(cell_id(k.cx, k.cz));
    std::unordered_set<uint64_t> covered;
    for (const auto& [k, l] : live_) covered.insert(cell_id(k.cx, k.cz));
    for (uint64_t id : visibleCells) {
        if (covered.count(id)) missingSince_.erase(id);
        else missingSince_.try_emplace(id, now);
    }
    std::erase_if(missingSince_, [&](const auto& e) { return !visibleCells.count(e.first); });
}

/* ------------------------------------------------------------------ worker -- */

void StreamScheduler::worker_loop() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            auto best = std::min_element(queue_.begin(), queue_.end(),
                                         [](const Job& a, const Job& b) { return a.score < b.score; });
            job = *best;
            queue_.erase(best);
            auto req = requests_.find(job.key);
            if (req == requests_.end() || req->second.generation != job.generation) continue;
            req->second.state = State::Building;
            counters_.jobsStarted++;
        }

        std::shared_ptr<ChunkMesh> mesh;
        std::shared_ptr<void> payload;
        size_t bytes = 0;
        if (builder_) {
            BuiltCell built = builder_(*field_, job.key);
            payload = std::move(built.payload);
            bytes = built.uploadBytes;
        } else {
            mesh = std::make_shared<ChunkMesh>(build_chunk_mesh(*field_, job.key.cx, job.key.cz, job.key.lod));
            payload = finisher_ ? finisher_(*mesh) : nullptr;
            bytes = upload_bytes(*mesh);
        }

        std::lock_guard lock(mutex_);
        auto req = requests_.find(job.key);
        if (req == requests_.end() || req->second.generation != job.generation) {
            counters_.droppedStale++;
            continue;
        }
        req->second.state = State::Ready;
        counters_.jobsBuilt++;
        ready_.push_back({job.key, job.generation, std::move(mesh), std::move(payload), bytes});
    }
}

/* ------------------------------------------------------------ engine side -- */

std::vector<ReadyCell> StreamScheduler::take_ready(const FrameBudget& budget, double frameCostMs, double now) {
    frameCosts_[frameCostCount_ % frameCosts_.size()] = frameCostMs;
    frameCostCount_++;
    const size_t n = std::min(frameCostCount_, frameCosts_.size());
    std::array<double, 60> sorted{};
    std::copy_n(frameCosts_.begin(), n, sorted.begin());
    std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n));
    const double p95 = sorted[std::min(n - 1, static_cast<size_t>(std::ceil(0.95 * n)) - 1)];

    int maxAttach = budget.maxAttach;
    std::lock_guard lock(mutex_);
    counters_.rollingP95Ms = p95;
    if (p95 > budget.frameBudgetMs) {
        const bool starving = std::any_of(missingSince_.begin(), missingSince_.end(),
                                          [&](const auto& e) { return now - e.second >= budget.starveSeconds; });
        maxAttach = starving ? 1 : 0;
        if (!ready_.empty() && maxAttach == 0) counters_.deferredOverBudget++;
    }

    std::vector<ReadyCell> out;
    if (maxAttach <= 0 || ready_.empty()) return out;

    std::sort(ready_.begin(), ready_.end(), [&](const ReadyCell& a, const ReadyCell& b) {
        return requests_.at(a.key).score < requests_.at(b.key).score;
    });
    size_t bytes = 0;
    while (!ready_.empty() && static_cast<int>(out.size()) < maxAttach) {
        const ReadyCell& next = ready_.front();
        // Always allow one, even if a single cell is larger than the byte budget.
        if (!out.empty() && bytes + next.uploadBytes > budget.maxUploadBytes) break;
        bytes += next.uploadBytes;
        out.push_back(std::move(ready_.front()));
        ready_.pop_front();
    }

    for (const ReadyCell& c : out) {
        requests_.erase(c.key);
        // A new LOD supersedes the other LODs of the same cell, now that it can cover it.
        for (const auto& [k, l] : live_) {
            if (k.cx == c.key.cx && k.cz == c.key.cz && k.lod != c.key.lod) retireQueue_.push_back(k);
        }
        live_[c.key] = {c.generation, now};
        counters_.attached++;
    }
    return out;
}

std::vector<RetiredCell> StreamScheduler::take_retirements(int maxRetire, double now) {
    std::unordered_set<uint64_t> visibleCells;
    for (const CellKey& k : wantedVisible_) visibleCells.insert(cell_id(k.cx, k.cz));
    auto wantedNow = [&](const CellKey& k) {
        return std::find(wantedVisible_.begin(), wantedVisible_.end(), k) != wantedVisible_.end() ||
               std::find(wantedPrefetch_.begin(), wantedPrefetch_.end(), k) != wantedPrefetch_.end();
    };
    // Never open a hole: the last live mesh covering a visible cell stays until
    // a replacement for that cell is live.
    auto soleCover = [&](const CellKey& k) {
        if (!visibleCells.count(cell_id(k.cx, k.cz))) return false;
        for (const auto& [o, l] : live_) {
            if (o.cx == k.cx && o.cz == k.cz && o.lod != k.lod) return false;
        }
        return true;
    };

    std::vector<RetiredCell> out;
    auto retire = [&](const CellKey& k) {
        auto it = live_.find(k);
        if (it == live_.end()) return;
        out.push_back({k, it->second.generation});
        live_.erase(it);
    };

    std::vector<CellKey> keep;
    for (const CellKey& k : retireQueue_) {
        if (static_cast<int>(out.size()) >= maxRetire) keep.push_back(k);
        else if (live_.count(k) && !wantedNow(k)) retire(k);
    }
    retireQueue_ = std::move(keep);

    if (static_cast<int>(out.size()) < maxRetire) {
        std::vector<std::pair<double, CellKey>> aged;
        for (const auto& [k, l] : live_) {
            if (now - l.lastWanted > config_.evictAge && !soleCover(k)) aged.push_back({l.lastWanted, k});
        }
        std::sort(aged.begin(), aged.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [t, k] : aged) {
            if (static_cast<int>(out.size()) >= maxRetire) break;
            retire(k);
        }
    }

    std::lock_guard lock(mutex_);
    counters_.retired += out.size();
    return out;
}

std::vector<ReadyCell> StreamScheduler::prime(const StreamView& view, double now) {
    update(view, now, true);  // boot builds the visible set only, never prefetch
    const std::vector<CellKey> visible = wantedVisible_;
    for (;;) {
        {
            std::lock_guard lock(mutex_);
            const bool done = std::all_of(visible.begin(), visible.end(), [&](const CellKey& k) {
                auto it = requests_.find(k);
                return live_.count(k) || (it != requests_.end() && it->second.state == State::Ready);
            });
            if (done) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    FrameBudget unlimited;
    unlimited.maxAttach = 1 << 20;
    unlimited.maxUploadBytes = SIZE_MAX;
    unlimited.frameBudgetMs = 1e9;
    return take_ready(unlimited, 0, now);
}

/* ------------------------------------------------------------------- stats -- */

size_t StreamScheduler::missing_visible_locked() const {
    std::unordered_set<uint64_t> covered;
    for (const auto& [k, l] : live_) covered.insert(cell_id(k.cx, k.cz));
    size_t missing = 0;
    for (const CellKey& k : wantedVisible_) {
        if (!covered.count(cell_id(k.cx, k.cz))) missing++;
    }
    return missing;
}

StreamStats StreamScheduler::stats() const {
    std::lock_guard lock(mutex_);
    StreamStats s = counters_;
    s.queued = queue_.size();
    s.building = static_cast<size_t>(std::count_if(requests_.begin(), requests_.end(),
                                                   [](const auto& e) { return e.second.state == State::Building; }));
    s.ready = ready_.size();
    s.live = live_.size();
    s.missingVisible = missing_visible_locked();
    return s;
}

bool StreamScheduler::is_live(const CellKey& k) const { return live_.count(k) != 0; }

}  // namespace worldcore
