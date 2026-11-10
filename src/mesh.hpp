// Geometry: what a mesh is made of, and builders for a few basic shapes.
//
// This is plain data in normal memory. Renderer::createMesh() copies it to the
// graphics card and returns the MeshHandle that a MeshRenderer component uses.

#pragma once

#include "math.hpp"

#include <cstdint>
#include <vector>

struct Vertex {
    Vec3 position;
    Vec3 normal; // unit vector pointing away from the surface; lighting needs it
};

struct MeshData {
    enum class Topology {
        Triangles, // every three vertices form one triangle, and it is lit
        Lines,     // every two vertices form one line, drawn in a flat colour
    };

    Topology            topology = Topology::Triangles;
    std::vector<Vertex> vertices;
};

// Only the side of a triangle on which its vertices run counter-clockwise is
// drawn. That is the outside of the shapes below.

// A box centred on its own origin.
MeshData makeBox(Vec3 size);

// A flat rectangle facing up, centred on its own origin: `width` along X and
// `depth` along Z. It is not drawn when seen from below.
MeshData makePlane(float width, float depth);

// The lines of a square grid lying flat, centred on its own origin.
MeshData makeGrid(int cells, float cellSize);

// Refers to a mesh held by the Renderer. The default value is "no mesh".
struct MeshHandle {
    std::uint32_t id = UINT32_MAX;
};
