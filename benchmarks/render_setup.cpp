// Optional CPU microbenchmark: isolates light collection, not OpenGL or total FPS.
#include "lighting/lighting.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>

namespace {
std::size_t allocations = 0;
}

void* operator new(std::size_t bytes) {
    ++allocations;
    if (void* memory = std::malloc(bytes ? bytes : 1)) {
        return memory;
    }
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept {
    std::free(memory);
}
void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

int main() {
    World world;
    for (int i = 0; i < 256; ++i) {
        const auto entity = world.create();
        world.add(entity, Transform{{float(i % 8) - 4, 0, -4}});
        world.add(entity, PointLight{});
    }
    const auto view =
        multiply(perspective(kPi / 2, 1, .1f, 100), lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0}));
    std::vector<Lighting::Light> scratch;
    Lighting::gather(world, view, {0, 0, 5}, scratch); // Warm up capacity outside measurement.
    constexpr int frames = 5000;
    std::size_t checksum = 0;
    for (bool reuse : {false, true}) {
        allocations = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < frames; ++frame) {
            if (reuse) {
                Lighting::gather(world, view, {0, 0, 5}, scratch);
                checksum += scratch.size();
            } else {
                const auto lights = Lighting::gather(world, view, {0, 0, 5});
                checksum += lights.size();
            }
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const auto count = allocations;
        std::cout << (reuse ? "Reused scratch" : "Value-returning collection") << ": "
                  << std::chrono::duration<double, std::milli>(elapsed).count() << " ms, " << count
                  << " allocations over " << frames << " collections\n";
        if (reuse && count != 0) {
            std::cerr << "Unexpected allocation in warmed collection path\n";
            return 1;
        }
    }
    return checksum == std::size_t(frames * Lighting::maxLights * 2) ? 0 : 1;
}
