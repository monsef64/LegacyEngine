#include "rendering/mesh.hpp"

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
    const float uv[4][2]{{0,0},{1,0},{1,1},{0,1}};
    for (int index : {0, 1, 2, 0, 2, 3}) {
        mesh.vertices.push_back({corners[index], normal,uv[index][0],uv[index][1]});
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

MeshData makeCapsule(float radius, float height)
{
    MeshData mesh;
    constexpr int slices = 32, rings = 24;
    // Duplicate the equator so the cylinder has straight sides and radial normals.
    auto vertex = [&](int ring, int slice) {
        const int latitude = ring <= rings / 2 ? ring : ring - 1;
        const float angle = -kPi * 0.5f + kPi * static_cast<float>(latitude) / rings;
        const float longitude = 2.0f * kPi * static_cast<float>(slice) / slices;
        const Vec3 normal{std::cos(angle) * std::cos(longitude), std::sin(angle),
                          std::cos(angle) * std::sin(longitude)};
        Vec3 position = normal * radius;
        position.y += ring <= rings / 2 ? -height * 0.5f : height * 0.5f;
        return Vertex{position, normal,float(slice)/slices,float(ring)/(rings+1)};
    };
    for (int r = 0; r <= rings; ++r) {
        for (int i = 0; i < slices; ++i) {
            const Vertex a = vertex(r, i), b = vertex(r + 1, i);
            const Vertex c = vertex(r + 1, i + 1), d = vertex(r, i + 1);
            // Outside winding: latitude edge crossed with longitude edge.
            for (const Vertex& v : {a, b, c, a, c, d}) mesh.vertices.push_back(v);
        }
    }
    return mesh;
}

MeshData makeSphere(float radius) { return makeCapsule(radius, 0.0f); }

MeshData makeCone(float radius,float height) {
    MeshData mesh;
    const auto hull=coneHullVertices();
    const Vec3 scale{radius*2,height,radius*2};
    const Vec3 tip=hull[0]*scale;
    for(int i=0;i<kConeSegments;++i) {
        const Vec3 a=hull[1+i]*scale,b=hull[1+(i+1)%kConeSegments]*scale;
        const Vec3 n=normalize(cross(tip-a,b-a));
        mesh.vertices.push_back({a,n,float(i)/kConeSegments,0});mesh.vertices.push_back({tip,n,(i+.5f)/kConeSegments,1});mesh.vertices.push_back({b,n,float(i+1)/kConeSegments,0});
        for(Vec3 p:{a,b,Vec3{0,-height*0.5f,0}})mesh.vertices.push_back({p,{0,-1,0},.5f+p.x/std::max(2*radius,.001f),.5f+p.z/std::max(2*radius,.001f)});
    }
    return mesh;
}
