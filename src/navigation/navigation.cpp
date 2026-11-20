#include "navigation/navigation.hpp"
#include "core/hierarchy.hpp"
#include "editor/rotation_editor.hpp"
#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Navigation {
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
Vec3 vector(const float* v) {
    return {v[0], v[1], v[2]};
}
} // namespace
struct System::Data {
    dtNavMesh* mesh = dtAllocNavMesh();
    dtNavMeshQuery* query = dtAllocNavMeshQuery();
    ~Data() {
        dtFreeNavMeshQuery(query);
        dtFreeNavMesh(mesh);
    }
    void initialize(unsigned char* bytes, int size) {
        if (!mesh || !query) {
            dtFree(bytes);
            throw std::runtime_error("Navigation allocation failed");
        }
        const auto result = mesh->init(bytes, size, DT_TILE_FREE_DATA);
        if (dtStatusFailed(result)) {
            dtFree(bytes);
            throw std::runtime_error("Cannot initialize navmesh");
        }
        require(!dtStatusFailed(query->init(mesh, 8192)), "Cannot initialize navmesh query");
    }
};
void Geometry::append(const MeshData& mesh, const Transform& t) {
    if (mesh.topology != MeshData::Topology::Triangles) {
        return;
    }
    require(mesh.vertices.size() % 3 == 0, "Invalid navigation triangles");
    require(vertices.size() / 3 + mesh.vertices.size() <= 6000000,
            "Navigation source limit: two million triangles");
    const int start = int(vertices.size() / 3);
    const auto model = transformMatrix(t.position, t.rotation, t.scale);
    for (const auto& v : mesh.vertices) {
        const auto p = v.position;
        const float values[]{model.m[0] * p.x + model.m[4] * p.y + model.m[8] * p.z + model.m[12],
                             model.m[1] * p.x + model.m[5] * p.y + model.m[9] * p.z + model.m[13],
                             model.m[2] * p.x + model.m[6] * p.y + model.m[10] * p.z + model.m[14]};
        for (float f : values) {
            require(std::isfinite(f) && std::abs(f) < 1e6f, "Invalid navigation geometry bounds");
            vertices.push_back(f);
        }
    }
    const bool mirrored = t.scale.x * t.scale.y * t.scale.z < 0;
    for (std::size_t i = 0; i < mesh.vertices.size(); i += 3) {
        triangles.insert(triangles.end(),
                         {start + int(i),
                          start + int(i) + (mirrored ? 2 : 1),
                          start + int(i) + (mirrored ? 1 : 2)});
    }
}
void System::clear() {
    data_.reset();
    obstacles_.clear();
    obstacleState_.clear();
    obstacleClock_ = 0;
    valid = false;
    debugMesh = {};
    ++revision_;
    status = "No baked navmesh";
}
void System::bake(const Geometry& geometry, const Settings& options) {
    for (float f : {options.cellSize,
                    options.cellHeight,
                    options.radius,
                    options.height,
                    options.climb,
                    options.slope}) {
        require(std::isfinite(f), "Invalid navigation bake settings");
    }
    require(options.cellSize >= .1f && options.cellSize <= 5 && options.cellHeight >= .05f &&
                options.cellHeight <= 1 && options.radius >= .05f && options.radius <= 5 &&
                options.height >= .2f && options.height <= 10 && options.climb >= 0 &&
                options.climb <= 2 && options.slope >= 0 && options.slope <= 80,
            "Navigation settings outside supported range");
    require(!geometry.triangles.empty(), "No static bake geometry; mark floors/terrain Static");
    rcContext context;
    rcConfig cfg{};
    cfg.cs = options.cellSize;
    cfg.ch = options.cellHeight;
    cfg.walkableSlopeAngle = options.slope;
    cfg.walkableHeight = std::max(3, int(std::ceil(options.height / cfg.ch)));
    cfg.walkableClimb = int(std::floor(options.climb / cfg.ch));
    cfg.walkableRadius = int(std::ceil(options.radius / cfg.cs));
    cfg.maxEdgeLen = int(12 / cfg.cs);
    cfg.maxSimplificationError = 1.3f;
    cfg.minRegionArea = 8 * 8;
    cfg.mergeRegionArea = 20 * 20;
    cfg.maxVertsPerPoly = 6;
    cfg.detailSampleDist = cfg.cs * 6;
    cfg.detailSampleMaxError = cfg.ch;
    const int nverts = int(geometry.vertices.size() / 3),
              ntris = int(geometry.triangles.size() / 3);
    rcCalcBounds(geometry.vertices.data(), nverts, cfg.bmin, cfg.bmax);
    cfg.bmin[1] -= cfg.ch * 2;
    cfg.bmax[1] += options.height + options.climb + cfg.ch * 2;
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
    require(cfg.width > 0 && cfg.height > 0 && cfg.width <= 4096 && cfg.height <= 4096 &&
                std::uint64_t(cfg.width) * cfg.height <= 4000000,
            "Navmesh grid too large; increase cell size or split the level");
    require((cfg.bmax[1] - cfg.bmin[1]) / cfg.ch < RC_SPAN_MAX_HEIGHT,
            "Navmesh height range too large; increase cell height");
    auto hf = std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)>(rcAllocHeightfield(),
                                                                           rcFreeHeightField);
    require(bool(hf) &&
                rcCreateHeightfield(
                    &context, *hf, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch),
            "Cannot allocate navigation heightfield");
    std::vector<unsigned char> areas(ntris, RC_NULL_AREA);
    rcMarkWalkableTriangles(&context,
                            cfg.walkableSlopeAngle,
                            geometry.vertices.data(),
                            nverts,
                            geometry.triangles.data(),
                            ntris,
                            areas.data());
    require(rcRasterizeTriangles(&context,
                                 geometry.vertices.data(),
                                 nverts,
                                 geometry.triangles.data(),
                                 areas.data(),
                                 ntris,
                                 *hf,
                                 cfg.walkableClimb),
            "Navmesh rasterization failed");
    rcFilterLowHangingWalkableObstacles(&context, cfg.walkableClimb, *hf);
    rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *hf);
    rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *hf);
    auto compact = std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)>(
        rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
    require(bool(compact) && rcBuildCompactHeightfield(
                                 &context, cfg.walkableHeight, cfg.walkableClimb, *hf, *compact),
            "Cannot compact navmesh");
    hf.reset();
    require(rcErodeWalkableArea(&context, cfg.walkableRadius, *compact),
            "Cannot erode agent clearance");
    require(rcBuildDistanceField(&context, *compact) &&
                rcBuildRegions(&context, *compact, 0, cfg.minRegionArea, cfg.mergeRegionArea),
            "Cannot build navigation regions");
    auto contours = std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)>(rcAllocContourSet(),
                                                                               rcFreeContourSet);
    require(bool(contours) &&
                rcBuildContours(
                    &context, *compact, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours),
            "Cannot build navigation contours");
    auto poly =
        std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)>(rcAllocPolyMesh(), rcFreePolyMesh);
    require(bool(poly) && rcBuildPolyMesh(&context, *contours, cfg.maxVertsPerPoly, *poly),
            "Cannot build navigation polygons");
    require(poly->npolys > 0 && poly->nverts < 65535,
            "No walkable polygons or navmesh too complex");
    auto detail = std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)>(
        rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);
    require(
        bool(detail) &&
            rcBuildPolyMeshDetail(
                &context, *poly, *compact, cfg.detailSampleDist, cfg.detailSampleMaxError, *detail),
        "Cannot build navigation detail");
    for (int i = 0; i < poly->npolys; ++i) {
        poly->flags[i] = 1;
        if (poly->areas[i] == RC_WALKABLE_AREA) {
            poly->areas[i] = 0;
        }
    }
    dtNavMeshCreateParams params{};
    params.verts = poly->verts;
    params.vertCount = poly->nverts;
    params.polys = poly->polys;
    params.polyAreas = poly->areas;
    params.polyFlags = poly->flags;
    params.polyCount = poly->npolys;
    params.nvp = poly->nvp;
    params.detailMeshes = detail->meshes;
    params.detailVerts = detail->verts;
    params.detailVertsCount = detail->nverts;
    params.detailTris = detail->tris;
    params.detailTriCount = detail->ntris;
    params.walkableHeight = options.height;
    params.walkableRadius = options.radius;
    params.walkableClimb = options.climb;
    std::memcpy(params.bmin, poly->bmin, sizeof(params.bmin));
    std::memcpy(params.bmax, poly->bmax, sizeof(params.bmax));
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.buildBvTree = true;
    unsigned char* bytes = nullptr;
    int size = 0;
    require(dtCreateNavMeshData(&params, &bytes, &size), "Cannot encode navmesh");
    auto next = std::make_shared<Data>();
    next->initialize(bytes, size);
    data_ = std::move(next);
    settings = options;
    valid = true;
    ++revision_;
    status = "Baked " + std::to_string(poly->npolys) + " walkable polygons";
    rebuildDebug();
}
void System::save(const std::filesystem::path& path, const std::string& signature) const {
    require(bool(data_), "No navmesh to save");
    const auto* tile = static_cast<const dtNavMesh*>(data_->mesh)->getTile(0);
    require(tile && tile->dataSize > 0 && tile->dataSize <= 8 * 1024 * 1024,
            "Navmesh data exceeds 8 MiB");
    Json doc{{"format", "LegacyEngineNavmesh"},
             {"version", 1},
             {"signature", signature},
             {"settings",
              {settings.cellSize,
               settings.cellHeight,
               settings.radius,
               settings.height,
               settings.climb,
               settings.slope}},
             {"data", std::vector<unsigned char>(tile->data, tile->data + tile->dataSize)}};
    writeJsonFile(path, doc);
}
std::string System::load(const std::filesystem::path& path) {
    const auto doc = readJsonFile(path, 64 * 1024 * 1024);
    require(doc.at("format") == "LegacyEngineNavmesh" && doc.at("version") == 1,
            "Invalid navmesh format");
    const auto bytes = doc.at("data").get<std::vector<unsigned char>>();
    require(bytes.size() >= sizeof(dtMeshHeader) && bytes.size() <= 8 * 1024 * 1024,
            "Invalid navmesh size");
    dtMeshHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    require(header.magic == DT_NAVMESH_MAGIC && header.version == DT_NAVMESH_VERSION &&
                header.polyCount > 0 && header.polyCount < 65536 && header.vertCount > 0 &&
                header.vertCount < 65536 && header.detailMeshCount == header.polyCount &&
                header.offMeshBase == header.polyCount,
            "Invalid navmesh header");
    // Validate exact packed layout before Detour forms pointers into untrusted data.
    auto aligned = [](std::uint64_t n) { return (n + 3) & ~std::uint64_t(3); };
    require(header.maxLinkCount >= header.polyCount * 3 && header.detailVertCount >= 0 &&
                header.detailTriCount >= 0 && header.bvNodeCount >= 0 &&
                header.offMeshConCount == 0,
            "Unsupported navmesh layout");
    const auto expected = aligned(sizeof(dtMeshHeader)) +
                          aligned(sizeof(float) * 3ull * header.vertCount) +
                          aligned(sizeof(dtPoly) * std::uint64_t(header.polyCount)) +
                          aligned(sizeof(dtLink) * std::uint64_t(header.maxLinkCount)) +
                          aligned(sizeof(dtPolyDetail) * std::uint64_t(header.detailMeshCount)) +
                          aligned(sizeof(float) * 3ull * header.detailVertCount) +
                          aligned(4ull * header.detailTriCount) +
                          aligned(sizeof(dtBVNode) * std::uint64_t(header.bvNodeCount));
    require(expected == bytes.size(), "Truncated navmesh data");
    // Validate indices and offsets before addTile connects polygons or a query
    // can dereference the project's serialized tile data.
    std::size_t offset = std::size_t(aligned(sizeof(dtMeshHeader)));
    for (int i = 0; i < header.vertCount * 3; ++i) {
        float v;
        std::memcpy(&v, bytes.data() + offset + i * sizeof(float), sizeof(float));
        require(std::isfinite(v) && std::abs(v) < 1e6f, "Invalid navmesh vertex");
    }
    offset += std::size_t(aligned(sizeof(float) * 3ull * header.vertCount));
    std::vector<dtPoly> polygons(header.polyCount);
    for (int i = 0; i < header.polyCount; ++i) {
        std::memcpy(&polygons[i], bytes.data() + offset + i * sizeof(dtPoly), sizeof(dtPoly));
        const auto& p = polygons[i];
        require(p.vertCount >= 3 && p.vertCount <= DT_VERTS_PER_POLYGON &&
                    p.getType() == DT_POLYTYPE_GROUND,
                "Invalid navmesh polygon");
        for (int j = 0; j < p.vertCount; ++j) {
            require(p.verts[j] < header.vertCount && p.neis[j] <= header.polyCount,
                    "Invalid navmesh polygon index");
        }
    }
    offset += std::size_t(aligned(sizeof(dtPoly) * std::uint64_t(header.polyCount))) +
              std::size_t(aligned(sizeof(dtLink) * std::uint64_t(header.maxLinkCount)));
    std::vector<dtPolyDetail> details(header.detailMeshCount);
    for (int i = 0; i < header.detailMeshCount; ++i) {
        std::memcpy(
            &details[i], bytes.data() + offset + i * sizeof(dtPolyDetail), sizeof(dtPolyDetail));
        const auto& d = details[i];
        require(std::uint64_t(d.vertBase) + d.vertCount <= std::uint64_t(header.detailVertCount) &&
                    std::uint64_t(d.triBase) + d.triCount <= std::uint64_t(header.detailTriCount),
                "Invalid navigation detail range");
    }
    offset += std::size_t(aligned(sizeof(dtPolyDetail) * std::uint64_t(header.detailMeshCount)));
    for (int i = 0; i < header.detailVertCount * 3; ++i) {
        float v;
        std::memcpy(&v, bytes.data() + offset + i * sizeof(float), sizeof(float));
        require(std::isfinite(v) && std::abs(v) < 1e6f, "Invalid navmesh detail vertex");
    }
    offset += std::size_t(aligned(sizeof(float) * 3ull * header.detailVertCount));
    for (int i = 0; i < header.detailMeshCount; ++i) {
        const auto& d = details[i];
        for (unsigned j = 0; j < d.triCount; ++j) {
            for (int k = 0; k < 3; ++k) {
                require(bytes[offset + (std::size_t(d.triBase) + j) * 4 + k] <
                            polygons[i].vertCount + d.vertCount,
                        "Invalid navigation detail index");
            }
        }
    }
    offset += std::size_t(aligned(4ull * header.detailTriCount));
    for (int i = 0; i < header.bvNodeCount; ++i) {
        dtBVNode node;
        std::memcpy(&node, bytes.data() + offset + i * sizeof(node), sizeof(node));
        require((node.i >= 0 && node.i < header.polyCount) ||
                    (node.i < 0 && -std::int64_t(node.i) <= header.bvNodeCount - i),
                "Invalid navigation BV tree");
    }
    for (int i = 0; i < 3; ++i) {
        require(std::isfinite(header.bmin[i]) && std::isfinite(header.bmax[i]) &&
                    header.bmin[i] <= header.bmax[i],
                "Invalid navmesh bounds");
    }
    require(std::isfinite(header.bvQuantFactor) && header.bvQuantFactor > 0,
            "Invalid navmesh quantization");
    auto* raw = static_cast<unsigned char*>(dtAlloc(bytes.size(), DT_ALLOC_PERM));
    require(raw, "Navigation allocation failed");
    std::memcpy(raw, bytes.data(), bytes.size());
    auto next = std::make_shared<Data>();
    next->initialize(raw, int(bytes.size()));
    const auto s = doc.at("settings").get<std::vector<float>>();
    require(s.size() == 6, "Invalid navmesh settings");
    for (float f : s) {
        require(std::isfinite(f) && f >= 0 && f <= 100, "Invalid navmesh settings");
    }
    require(s[2] >= .05f && s[3] >= .2f, "Invalid baked agent dimensions");
    data_ = std::move(next);
    settings = {s[0], s[1], s[2], s[3], s[4], s[5]};
    valid = true;
    ++revision_;
    status = "Loaded navmesh";
    rebuildDebug();
    return doc.at("signature").get<std::string>();
}
bool System::basePath(Vec3 start, Vec3 destination, std::vector<Vec3>& corners) const {
    corners.clear();
    if (!valid || !data_) {
        return false;
    }
    dtQueryFilter filter;
    const float ext[]{
        settings.radius * 2 + .5f, settings.height + settings.climb, settings.radius * 2 + .5f};
    dtPolyRef from = 0, to = 0;
    float a[3], b[3];
    if (dtStatusFailed(data_->query->findNearestPoly(&start.x, ext, &filter, &from, a)) || !from ||
        dtStatusFailed(data_->query->findNearestPoly(&destination.x, ext, &filter, &to, b)) ||
        !to) {
        return false;
    }
    dtPolyRef polys[512];
    int count = 0;
    const auto result = data_->query->findPath(from, to, a, b, &filter, polys, &count, 512);
    if (dtStatusFailed(result) || dtStatusDetail(result, DT_PARTIAL_RESULT) ||
        dtStatusDetail(result, DT_BUFFER_TOO_SMALL) || !count || polys[count - 1] != to) {
        return false;
    }
    float points[512 * 3];
    unsigned char flags[512];
    dtPolyRef refs[512];
    int n = 0;
    const auto straight =
        data_->query->findStraightPath(a, b, polys, count, points, flags, refs, &n, 512);
    if (dtStatusFailed(straight) || dtStatusDetail(straight, DT_BUFFER_TOO_SMALL) || n < 1) {
        return false;
    }
    for (int i = 0; i < n; ++i) {
        corners.push_back(vector(points + i * 3));
    }
    return true;
}
bool System::blocked(Vec3 a, Vec3 b) const {
    for (const auto& box : obstacles_) {
        const Vec3 low = box.low - Vec3{settings.radius, settings.height, settings.radius};
        const Vec3 high = box.high + Vec3{settings.radius, 0, settings.radius};
        float from = 0, to = 1;
        const float p[]{a.x, a.y, a.z}, d[]{b.x - a.x, b.y - a.y, b.z - a.z},
            lo[]{low.x, low.y, low.z}, hi[]{high.x, high.y, high.z};
        for (int k = 0; k < 3; ++k) {
            if (std::abs(d[k]) < 1e-7f) {
                if (p[k] <= lo[k] || p[k] >= hi[k]) {
                    from = 2;
                    break;
                }
            } else {
                float x = (lo[k] - p[k]) / d[k], y = (hi[k] - p[k]) / d[k];
                if (x > y) {
                    std::swap(x, y);
                }
                from = std::max(from, x);
                to = std::min(to, y);
            }
        }
        if (from < to && from < 1 && to > 0) {
            return true;
        }
    }
    return false;
}
bool System::path(Vec3 start, Vec3 destination, std::vector<Vec3>& corners) const {
    if (!basePath(start, destination, corners)) {
        return false;
    }
    bool obstructed = false;
    for (std::size_t i = 1; i < corners.size(); ++i) {
        obstructed |= blocked(corners[i - 1], corners[i]);
    }
    if (!obstructed) {
        return true;
    }
    if (corners.size() > 128 || obstacles_.size() > 32) {
        corners.clear();
        return false;
    }
    std::vector<Vec3> nodes = corners;
    const std::size_t goal = nodes.size() - 1;
    for (const auto& b : obstacles_) {
        if (start.y + settings.height < b.low.y || start.y > b.high.y) {
            continue;
        }
        const float r = settings.radius + .08f;
        for (int i = 0; i < 4; ++i) {
            nodes.push_back(
                {i & 1 ? b.high.x + r : b.low.x - r, start.y, i & 2 ? b.high.z + r : b.low.z - r});
        }
    }
    const float ext[]{
        settings.radius * 2 + .5f, settings.height + settings.climb, settings.radius * 2 + .5f};
    dtQueryFilter filter;
    std::vector<dtPolyRef> refs(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        float point[3];
        if (dtStatusFailed(
                data_->query->findNearestPoly(&nodes[i].x, ext, &filter, &refs[i], point)) ||
            !refs[i]) {
            refs[i] = 0;
            continue;
        }
        nodes[i] = vector(point);
    }
    std::vector<float> cost(nodes.size(), std::numeric_limits<float>::infinity());
    std::vector<int> previous(nodes.size(), -1);
    std::vector<bool> visited(nodes.size());
    cost[0] = 0;
    for (std::size_t pass = 0; pass < nodes.size(); ++pass) {
        int current = -1;
        float best = std::numeric_limits<float>::infinity();
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (!visited[i] && refs[i]) {
                const auto d = nodes[i] - nodes[goal];
                const float score = cost[i] + std::sqrt(dot(d, d));
                if (score < best) {
                    best = score;
                    current = int(i);
                }
            }
        }
        if (current < 0) {
            break;
        }
        if (std::size_t(current) == goal) {
            corners.clear();
            for (int i = current; i >= 0; i = previous[i]) {
                corners.push_back(nodes[i]);
            }
            std::reverse(corners.begin(), corners.end());
            return true;
        }
        visited[current] = true;
        for (std::size_t j = 0; j < nodes.size(); ++j) {
            if (visited[j] || !refs[j] || blocked(nodes[current], nodes[j])) {
                continue;
            }
            const auto delta = nodes[j] - nodes[current];
            const float next = cost[current] + std::sqrt(dot(delta, delta));
            if (next >= cost[j]) {
                continue;
            }
            float fraction = 0, normal[3];
            dtPolyRef crossed[512];
            int count = 0;
            const auto result = data_->query->raycast(refs[current],
                                                      &nodes[current].x,
                                                      &nodes[j].x,
                                                      &filter,
                                                      &fraction,
                                                      normal,
                                                      crossed,
                                                      &count,
                                                      512);
            if (dtStatusFailed(result) || dtStatusDetail(result, DT_BUFFER_TOO_SMALL) ||
                fraction < 1) {
                continue;
            }
            if (!count || crossed[count - 1] != refs[j]) {
                continue; // no shortcuts between stacked floors
            }
            cost[j] = next;
            previous[j] = current;
        }
    }
    corners.clear();
    return false;
}
bool System::constrain(Vec3 start, Vec3 end, Vec3& result) const {
    if (!valid || !data_ || blocked(start, end)) {
        return false;
    }
    dtQueryFilter filter;
    dtPolyRef from = 0;
    float a[3];
    const float ext[]{
        settings.radius * 2 + .5f, settings.height + settings.climb, settings.radius * 2 + .5f};
    if (dtStatusFailed(data_->query->findNearestPoly(&start.x, ext, &filter, &from, a)) || !from) {
        return false;
    }
    dtPolyRef visited[32];
    int n = 0;
    float p[3];
    const auto status =
        data_->query->moveAlongSurface(from, a, &end.x, &filter, p, visited, &n, 32);
    if (dtStatusFailed(status) || dtStatusDetail(status, DT_BUFFER_TOO_SMALL) || !n) {
        return false;
    }
    if (dtStatusFailed(data_->query->getPolyHeight(visited[n - 1], p, &p[1]))) {
        return false;
    }
    result = vector(p);
    return true;
}
void System::rebuildDebug() {
    debugMesh = {};
    debugMesh.topology = MeshData::Topology::Lines;
    if (!data_) {
        return;
    }
    const auto* tile = static_cast<const dtNavMesh*>(data_->mesh)->getTile(0);
    if (!tile || !tile->header) {
        return;
    }
    for (int i = 0; i < tile->header->polyCount; ++i) {
        if (debugMesh.vertices.size() > 200000) {
            break;
        }
        const auto& poly = tile->polys[i];
        for (int j = 0; j < poly.vertCount; ++j) {
            auto a = vector(tile->verts + poly.verts[j] * 3),
                 b = vector(tile->verts + poly.verts[(j + 1) % poly.vertCount] * 3);
            a.y += .04f;
            b.y += .04f;
            debugMesh.vertices.push_back({a, {0, 1, 0}});
            debugMesh.vertices.push_back({b, {0, 1, 0}});
        }
    }
}
void System::update(World& world, float seconds) {
    const float dt = std::clamp(seconds, 0.f, .1f);
    obstacles_.clear();
    std::ostringstream obstacleState;
    obstacleState << std::hexfloat;
    world.eachReadOnly<NavMeshObstacle, Transform>(
        [&](Entity e, const NavMeshObstacle& o, const Transform& t) {
            if (!o.enabled || !isActive(world, e)) {
                return;
            }
            const auto model = transformMatrix(t.position, t.rotation, t.scale);
            Vec3 lo{}, hi{};
            for (int i = 0; i < 8; ++i) {
                const auto p = o.center + Vec3{(i & 1 ? .5f : -.5f) * o.size.x,
                                               (i & 2 ? .5f : -.5f) * o.size.y,
                                               (i & 4 ? .5f : -.5f) * o.size.z};
                const Vec3 v{model.m[0] * p.x + model.m[4] * p.y + model.m[8] * p.z + model.m[12],
                             model.m[1] * p.x + model.m[5] * p.y + model.m[9] * p.z + model.m[13],
                             model.m[2] * p.x + model.m[6] * p.y + model.m[10] * p.z + model.m[14]};
                if (!i) {
                    lo = hi = v;
                } else {
                    lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
                    hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
                }
            }
            obstacles_.push_back({lo, hi});
            obstacleState << lo.x << ',' << lo.y << ',' << lo.z << ',' << hi.x << ',' << hi.y << ','
                          << hi.z << ';';
        });
    obstacleClock_ += dt;
    if (obstacleState_ != obstacleState.str() && obstacleClock_ >= .2f) {
        obstacleState_ = obstacleState.str();
        obstacleClock_ = 0;
        ++revision_;
    }
    world.each<NavMeshAgent, Transform>([&](Entity e, NavMeshAgent& a, Transform& t) {
        a.velocity = {};
        if (!a.enabled || !isActive(world, e)) {
            a.status = "Disabled";
            return;
        }
        if (a.stopped) {
            a.status = "Stopped";
            return;
        }
        if (!valid || !data_) {
            a.hasPath = false;
            a.pathPending = a.requested;
            a.status = "No valid navmesh";
            return;
        }
        if (world.has<RigidBody>(e) || world.has<CharacterController>(e) ||
            world.has<FPSCamera>(e) || world.has<TPSCamera>(e) || world.has<NavMeshObstacle>(e)) {
            a.hasPath = false;
            a.pathPending = false;
            a.status = "Remove competing physics/camera movement driver";
            return;
        }
        if (a.requested || (a.navigationRevision != revision_ && a.destinationSet)) {
            a.pathPending = false;
            a.requested = false;
            a.navigationRevision = revision_;
            a.hasPath = path(t.position, a.destination, a.corners);
            a.corner = 0;
            a.status = a.hasPath ? "Moving" : "Unreachable destination";
        }
        if (!a.hasPath || a.corner >= a.corners.size()) {
            return;
        }
        a.status = "Moving";
        const auto before = t.position;
        float budget = a.speed * dt;
        for (int steps = 0; steps < 16 && a.corner < a.corners.size(); ++steps) {
            const auto goal = a.corners[a.corner], delta = goal - t.position;
            const float distance = std::sqrt(dot(delta, delta));
            const bool last = a.corner + 1 == a.corners.size();
            const float tolerance = last ? a.stoppingDistance : .03f;
            if (distance <= tolerance) {
                ++a.corner;
                continue;
            }
            if (budget <= 0) {
                break;
            }
            const float travel = std::min(budget, std::max(0.f, distance - tolerance));
            Vec3 next;
            if (!constrain(t.position, t.position + delta * (travel / distance), next)) {
                a.hasPath = false;
                a.status = "Blocked / agent left navmesh";
                break;
            }
            t.position = next;
            budget -= travel;
            if (travel < distance - tolerance - .001f) {
                break;
            }
            if (std::sqrt(dot(t.position - goal, t.position - goal)) > tolerance + .01f) {
                break;
            }
            ++a.corner;
        }
        if (dt > 0) {
            a.velocity = (t.position - before) * (1 / dt);
        }
        if (a.updateRotation && dot(a.velocity, a.velocity) > .00001f) {
            const float yaw = std::atan2(-a.velocity.x, -a.velocity.z) * 180 / kPi;
            auto angles = rotationToEuler(t.rotation);
            const float difference = std::remainder(yaw - angles.y, 360.f);
            angles.y += std::clamp(difference, -a.angularSpeed * dt, a.angularSpeed * dt);
            t.rotation = rotationFromEuler(angles);
        }
        a.remainingDistance = 0;
        Vec3 p = t.position;
        for (auto i = a.corner; i < a.corners.size(); ++i) {
            const auto d = a.corners[i] - p;
            a.remainingDistance += std::sqrt(dot(d, d));
            p = a.corners[i];
        }
        if (a.corner >= a.corners.size()) {
            a.hasPath = false;
            a.status = "Arrived";
            a.remainingDistance = 0;
        }
        if (auto* parent = world.tryGet<Parent>(e);
            parent && world.has<Transform>(parent->entity)) {
            parent->previous = world.get<Transform>(parent->entity);
        }
    });
}
} // namespace Navigation
