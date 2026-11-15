// Focused scalar versus SIMD model-transform microbenchmark. This is not an
// end-to-end frame benchmark; compare on each target CPU and compiler.
#include "models/model_pose.hpp"
#include <chrono>
#include <cstdio>
#include <vector>

int main() {
    std::vector<Vec3> input(1000000), output(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = {float(i % 137), float(i % 47), float(i % 79)};
    }
    auto run = [&](auto transform, const char* label) {
        Mat4 matrix{};
        for (int i = 0; i < 16; ++i) {
            matrix.m[i] = (i - 5) * .11f;
        }
        const auto start = std::chrono::steady_clock::now();
        for (int iteration = 0; iteration < 20; ++iteration) {
            for (std::size_t i = 0; i < input.size(); ++i) {
                output[i] = transform(matrix, input[i], iteration % 2 != 0);
            }
            matrix.m[0] += output[iteration].x * .000001f;
        }
        const auto ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        std::printf("%s: %.2f ms, checksum %.6f\n", label, ms, output[123].x);
    };
    run(modelPointScalar, "scalar");
    run(modelPoint, "SIMD (or scalar fallback)");
}
