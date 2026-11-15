#include "assets/background_worker.hpp"
#include "models/model_asset.hpp"
#include "rendering/streamed_texture.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <random>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
int main() {
    // SIMD and portable scalar model transforms must agree, including vector
    // transforms that must not apply translation.
    std::mt19937 random(0x2142);
    std::uniform_real_distribution<float> value(-100.f, 100.f);
    for (int sample = 0; sample < 10000; ++sample) {
        Mat4 matrix{};
        for (float& component : matrix.m) {
            component = value(random);
        }
        Vec3 point{value(random), value(random), value(random)};
        for (bool direction : {false, true}) {
            const auto scalar = modelPointScalar(matrix, point, direction);
            const auto simd = modelPoint(matrix, point, direction);
            assert(std::abs(scalar.x - simd.x) <= .0001f * std::max(1.f, std::abs(scalar.x)));
            assert(std::abs(scalar.y - simd.y) <= .0001f * std::max(1.f, std::abs(scalar.y)));
            assert(std::abs(scalar.z - simd.z) <= .0001f * std::max(1.f, std::abs(scalar.z)));
        }
    }
    BackgroundWorker worker(1);
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    auto first = worker.submit([&] {
        entered.set_value();
        gate.wait();
        return 42;
    });
    assert(first && entered.get_future().wait_for(5s) == std::future_status::ready);
    auto queued = worker.submit([] { return 7; });
    assert(queued);
    assert(!worker.submit([] { return 8; }));
    worker.cancelQueued();
    assert(queued->wait_for(1s) == std::future_status::ready);
    bool cancelled = false;
    try {
        queued->get();
    } catch (const std::future_error&) {
        cancelled = true;
    }
    assert(cancelled);
    release.set_value();
    assert(first->get() == 42);
    auto failure = worker.submit([]() -> int { throw std::runtime_error("decode failed"); });
    assert(failure);
    bool failed = false;
    try {
        failure->get();
    } catch (const std::runtime_error&) {
        failed = true;
    }
    assert(failed);
    auto id = worker.submit([] { return std::this_thread::get_id(); });
    assert(id && id->get() != std::this_thread::get_id());

    const auto root = std::filesystem::temp_directory_path() /
                      ("legacy-stream-test-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
    } cleanup{root};
    std::filesystem::create_directories(root);
    AssetManager assets;
    assets.root = root;
    // Two top-down rows, each 1 pixel: red then green, in TGA's BGR order.
    const unsigned char image[] = {0, 0, 2, 0, 0,  0,    0, 0, 0,   0, 0,   0,
                                   1, 0, 2, 0, 24, 0x20, 0, 0, 255, 0, 255, 0};
    {
        std::ofstream f(root / "image.tga", std::ios::binary);
        f.write(reinterpret_cast<const char*>(image), sizeof image);
    }
    auto texture = worker.submit([assets] { return decodeStreamedTexture(assets, "image.tga"); });
    assert(texture);
    const auto decoded = texture->get();
    assert(decoded.width == 1 && decoded.height == 2 && decoded.pixels.size() == 8);
    assert(decoded.pixels[0] == 0 && decoded.pixels[1] == 255 && decoded.pixels[2] == 0);
    assert(decoded.pixels[4] == 255 && decoded.pixels[5] == 0 && decoded.pixels[6] == 0);
    auto invalid =
        worker.submit([assets] { return decodeStreamedTexture(assets, "../image.tga"); });
    assert(invalid);
    failed = false;
    try {
        invalid->get();
    } catch (const std::exception&) {
        failed = true;
    }
    assert(failed);
    writeJsonFile(
        root / "triangle.lemesh",
        Json::parse(
            R"({"format":"LegacyEngineMesh","version":1,"vertices":[[0,0,0,0,0,1,0,0],[1,0,0,0,0,1,1,0],[0,1,0,0,0,1,0,1]]})"));
    auto mesh = worker.submit([assets] { return loadModelAsset(assets, "triangle.lemesh"); });
    assert(mesh && mesh->get().vertices.size() == 3);
}
