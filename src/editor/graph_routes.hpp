#pragma once
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <queue>
#include <vector>

namespace GraphRoutes {
struct Box {
    ImVec2 low, high;
};
// Bounded orthogonal routing. Expanded node bounds are obstacles; previously
// routed segments carry a high cost to separate parallel/reciprocal edges.
// Arbitrary non-planar graphs can require crossings, so crossings are penalized
// rather than making such graphs impossible to draw.
class Router {
    float step_ = 12;
    ImVec2 origin_;
    int w_ = 1, h_ = 1;
    std::vector<unsigned char> blocked_;
    std::vector<unsigned short> used_;
    int cell(ImVec2 p) const {
        int x = std::clamp(int(std::round((p.x - origin_.x) / step_)), 0, w_ - 1);
        int y = std::clamp(int(std::round((p.y - origin_.y) / step_)), 0, h_ - 1);
        return y * w_ + x;
    }
    ImVec2 point(int i) const {
        return {origin_.x + (i % w_) * step_, origin_.y + (i / w_) * step_};
    }

  public:
    Router(const std::vector<Box>& boxes, float zoom) {
        step_ = std::max(6.f, 12.f * zoom);
        ImVec2 low{0, 0}, high{200, 200};
        if (!boxes.empty()) {
            low = boxes.front().low;
            high = boxes.front().high;
        }
        for (const auto& b : boxes) {
            low.x = std::min(low.x, b.low.x);
            low.y = std::min(low.y, b.low.y);
            high.x = std::max(high.x, b.high.x);
            high.y = std::max(high.y, b.high.y);
        }
        origin_ = {low.x - 100 * zoom, low.y - 100 * zoom};
        // Bound memory and routing work for widely scattered imported graphs.
        step_ = std::max(
            {step_, (high.x - low.x + 200 * zoom) / 350.f, (high.y - low.y + 200 * zoom) / 250.f});
        w_ = std::clamp(int((high.x - origin_.x + 100 * zoom) / step_) + 2, 1, 352);
        h_ = std::clamp(int((high.y - origin_.y + 100 * zoom) / step_) + 2, 1, 252);
        blocked_.resize(w_ * h_);
        used_.resize(w_ * h_);
        for (const auto& b : boxes) {
            const int x0 =
                std::clamp(int(std::floor((b.low.x - 4 * zoom - origin_.x) / step_)), 0, w_ - 1);
            const int x1 =
                std::clamp(int(std::ceil((b.high.x + 4 * zoom - origin_.x) / step_)), 0, w_ - 1);
            const int y0 =
                std::clamp(int(std::floor((b.low.y - 4 * zoom - origin_.y) / step_)), 0, h_ - 1);
            const int y1 =
                std::clamp(int(std::ceil((b.high.y + 4 * zoom - origin_.y) / step_)), 0, h_ - 1);
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const auto p = point(y * w_ + x);
                    if (p.x >= b.low.x - 4 * zoom && p.y >= b.low.y - 4 * zoom &&
                        p.x <= b.high.x + 4 * zoom && p.y <= b.high.y + 4 * zoom) {
                        blocked_[y * w_ + x] = 1;
                    }
                }
            }
        }
    }
    std::vector<ImVec2> route(ImVec2 a, ImVec2 b) {
        // Ports face out from right and into left; retain exact endpoints.
        ImVec2 start{a.x + step_ * 1.5f, a.y}, end{b.x - step_ * 1.5f, b.y};
        int from = cell(start), to = cell(end);
        std::vector<int> cost(w_ * h_, 1000000000), parent(w_ * h_, -1);
        using Item = std::pair<int, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
        auto heuristic = [&](int i) {
            return (std::abs(i % w_ - to % w_) + std::abs(i / w_ - to / w_)) * 10;
        };
        cost[from] = 0;
        queue.push({heuristic(from), from});
        while (!queue.empty()) {
            auto [score, i] = queue.top();
            queue.pop();
            if (score != cost[i] + heuristic(i)) {
                continue;
            }
            if (i == to) {
                break;
            }
            const int x = i % w_, y = i / w_;
            for (int next : {x > 0 ? i - 1 : -1,
                             x + 1 < w_ ? i + 1 : -1,
                             y > 0 ? i - w_ : -1,
                             y + 1 < h_ ? i + w_ : -1}) {
                if (next < 0 || (blocked_[next] && next != to)) {
                    continue;
                }
                const int candidate = cost[i] + 10 + used_[next] * 1000;
                if (candidate < cost[next]) {
                    cost[next] = candidate;
                    parent[next] = i;
                    queue.push({candidate + heuristic(next), next});
                }
            }
        }
        if (from != to && parent[to] < 0) {
            // Clearly separated fallback lane for pathological overlapping nodes.
            float y = origin_.y + step_;
            return {a, start, {start.x, y}, {end.x, y}, end, b};
        }
        std::vector<int> cells;
        for (int i = to; i != -1; i = parent[i]) {
            cells.push_back(i);
            if (i == from) {
                break;
            }
        }
        std::reverse(cells.begin(), cells.end());
        std::vector<ImVec2> path{a, start};
        for (std::size_t n = 0; n < cells.size(); ++n) {
            int i = cells[n];
            used_[i] = std::min<unsigned short>(used_[i] + 1, 10);
            // Compress straight runs to keep draw/hit-test work small.
            if (n > 0 && n + 1 < cells.size() &&
                cells[n] - cells[n - 1] == cells[n + 1] - cells[n]) {
                continue;
            }
            path.push_back(point(i));
        }
        path.push_back(end);
        path.push_back(b);
        return path;
    }
};
inline float distance(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y, d = dx * dx + dy * dy;
    const float t = d ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / d, 0.f, 1.f) : 0;
    return std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
}
} // namespace GraphRoutes
