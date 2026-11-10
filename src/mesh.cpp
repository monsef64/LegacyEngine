#include "mesh.hpp"

namespace {

// Adds a rectangle made of two triangles. `right` and `up` are its half-size
// axes as seen from the front; the front is the side `right` x `up` points to.
void addQuad(MeshData& mesh, Vec3 centre, Vec3 right, Vec3 up)
{
    const Vec3 normal = normalize(cross(right, up));
    const Vec3 corners[4] = {
        centre - right - up,
        centre + right - up,
        centre + right + up,
        centre - right + up,
    };
    for (int index : {0, 1, 2, 0, 2, 3}) {
        mesh.vertices.push_back({corners[index], normal});
    }
}

} // namespace

MeshData makeBox(Vec3 size)
{
    const Vec3 half = size * 0.5f;
    const Vec3 x = {half.x, 0.0f, 0.0f};
    const Vec3 y = {0.0f, half.y, 0.0f};
    const Vec3 z = {0.0f, 0.0f, half.z};
    const Vec3 origin;

    MeshData mesh;
    addQuad(mesh, x, origin - z, y);          // +X face
    addQuad(mesh, origin - x, z, y);          // -X face
    addQuad(mesh, y, x, origin - z);          // +Y face
    addQuad(mesh, origin - y, x, z);          // -Y face
    addQuad(mesh, z, x, y);                   // +Z face
    addQuad(mesh, origin - z, origin - x, y); // -Z face
    return mesh;
}

MeshData makePlane(float width, float depth)
{
    MeshData mesh;
    addQuad(mesh, {}, {width * 0.5f, 0.0f, 0.0f}, {0.0f, 0.0f, -depth * 0.5f});
    return mesh;
}

MeshData makeGrid(int cells, float cellSize)
{
    const float half = static_cast<float>(cells) * cellSize * 0.5f;
    const Vec3  up   = {0.0f, 1.0f, 0.0f};

    MeshData mesh;
    mesh.topology = MeshData::Topology::Lines;
    for (int i = 0; i <= cells; ++i) {
        const float p = -half + static_cast<float>(i) * cellSize;
        mesh.vertices.push_back({{p, 0.0f, -half}, up}); // a line along Z
        mesh.vertices.push_back({{p, 0.0f, half}, up});
        mesh.vertices.push_back({{-half, 0.0f, p}, up}); // a line along X
        mesh.vertices.push_back({{half, 0.0f, p}, up});
    }
    return mesh;
}
