#include "rendering/skybox_renderer.hpp"
#include "assets/cubemap.hpp"
#include "core/worker_pool.hpp"
#include "rendering/shader_data.hpp"
#include <algorithm>
#include <glad/gl.h>
#include <iostream>
#include <memory>
#include <stb_image.h>
#include <vector>
namespace {
unsigned shader(unsigned type, const char* source) {
    auto id = glCreateShader(type);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);
    int ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(id, sizeof(log), nullptr, log);
        std::cerr << log << '\n';
        glDeleteShader(id);
        return 0;
    }
    return id;
}
bool inverse(const Mat4& m, Mat4& out) {
    double a[4][8]{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            a[r][c] = m.m[c * 4 + r];
            a[r][c + 4] = r == c;
        }
    }
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r) {
            if (std::abs(a[r][c]) > std::abs(a[pivot][c])) {
                pivot = r;
            }
        }
        if (std::abs(a[pivot][c]) < 1e-12) {
            return false;
        }
        for (int i = 0; i < 8; ++i) {
            std::swap(a[c][i], a[pivot][i]);
        }
        double scale = a[c][c];
        for (auto& v : a[c]) {
            v /= scale;
        }
        for (int r = 0; r < 4; ++r) {
            if (r != c) {
                double k = a[r][c];
                for (int i = 0; i < 8; ++i) {
                    a[r][i] -= k * a[c][i];
                }
            }
        }
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out.m[c * 4 + r] = float(a[r][c + 4]);
        }
    }
    return true;
}
std::vector<unsigned char> readImage(const AssetManager& assets, const std::string& path) {
    if (path.empty()) {
        throw std::runtime_error("Assign all six skybox faces, or an HDR panorama");
    }
    auto file = assets.resolve(path);
    auto bytes = std::filesystem::file_size(file);
    if (bytes > 64 * 1024 * 1024) {
        throw std::runtime_error("Skybox image exceeds 64 MiB");
    }
    std::ifstream in(file, std::ios::binary);
    std::vector<unsigned char> data(static_cast<std::size_t>(bytes));
    in.read(reinterpret_cast<char*>(data.data()), bytes);
    if (!in) {
        throw std::runtime_error("Cannot read skybox image");
    }
    return data;
}
std::string resourceStamp(const AssetManager& assets,
                          const std::string& key,
                          const Skybox& sky,
                          const std::vector<std::string>& paths) {
    std::string stamp = key;
    auto append = [&](const std::string& path) {
        stamp += '|';
        try {
            const auto file = assets.resolve(path);
            stamp +=
                std::to_string(std::filesystem::last_write_time(file).time_since_epoch().count()) +
                ':' + std::to_string(std::filesystem::file_size(file));
        } catch (...) {
            stamp += "missing";
        }
    };
    if (sky.type == 0 && !sky.cubemap.empty()) {
        append(sky.cubemap);
    }
    for (const auto& path : paths) {
        append(path);
    }
    return stamp;
}
SkyboxDecoded decodeSkybox(const Skybox& sky, const AssetManager& assets, const std::string& key) {
    SkyboxDecoded out;
    out.paths = sky.type == 1 ? std::vector<std::string>{sky.hdr}
                              : std::vector<std::string>{
                                    sky.right, sky.left, sky.top, sky.bottom, sky.back, sky.front};
    if (sky.type == 0 && !sky.cubemap.empty()) {
        const auto map = loadCubemap(assets, sky.cubemap);
        out.paths = {
            map.faces[0], map.faces[1], map.faces[2], map.faces[3], map.faces[5], map.faces[4]};
    }
    out.stamp = resourceStamp(assets, key, sky, out.paths);
    Vec3 sum{};
    float total = 0;
    int faceSize = 0;
    for (std::size_t face = 0; face < out.paths.size(); ++face) {
        auto encoded = readImage(assets, out.paths[face]);
        SkyboxDecoded::Image image;
        int channels = 0;
        auto& w = image.width;
        auto& h = image.height;
        if (!stbi_info_from_memory(encoded.data(), int(encoded.size()), &w, &h, &channels) ||
            w <= 0 || h <= 0) {
            throw std::runtime_error("Invalid skybox image: " + out.paths[face]);
        }
        if (sky.type == 1) {
            if (w > 4096 || h > 2048 ||
                !stbi_is_hdr_from_memory(encoded.data(), int(encoded.size()))) {
                throw std::runtime_error("HDR panorama must be Radiance .hdr, at most 4096 x 2048");
            }
            std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
                stbi_loadf_from_memory(encoded.data(), int(encoded.size()), &w, &h, &channels, 3),
                stbi_image_free);
            if (!pixels) {
                throw std::runtime_error("Cannot decode HDR panorama");
            }
            image.floats.assign(pixels.get(), pixels.get() + std::size_t(w) * h * 3);
            for (auto& value : image.floats) {
                value = std::isfinite(value) ? std::clamp(value, 0.f, 65408.f) : 0.f;
            }
            for (int y = 0; y < h; y += std::max(1, h / 32)) {
                for (int x = 0; x < w; x += std::max(1, w / 64)) {
                    const float lat = (.5f - (y + .5f) / h) * kPi;
                    const float weight = std::max(0.f, std::sin(lat) + .25f) * std::cos(lat);
                    const auto* rgb = image.floats.data() + (std::size_t(y) * w + x) * 3;
                    sum = sum + Vec3{rgb[0], rgb[1], rgb[2]} * weight;
                    total += weight;
                }
            }
        } else {
            if (w != h || w > 2048 || (faceSize && w != faceSize)) {
                throw std::runtime_error(
                    "Skybox faces must be equal square images, at most 2048 x 2048");
            }
            faceSize = w;
            std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
                stbi_load_from_memory(encoded.data(), int(encoded.size()), &w, &h, &channels, 4),
                stbi_image_free);
            if (!pixels) {
                throw std::runtime_error("Cannot decode skybox face");
            }
            image.bytes.assign(pixels.get(), pixels.get() + std::size_t(w) * h * 4);
            const float weight = face == 2 ? 1.f : face == 3 ? 0.f : .5f;
            for (int y = 0; y < h; y += std::max(1, h / 32)) {
                for (int x = 0; x < w; x += std::max(1, w / 32)) {
                    const auto* rgb = image.bytes.data() + (std::size_t(y) * w + x) * 4;
                    sum = sum + Vec3{std::pow(rgb[0] / 255.f, 2.2f),
                                     std::pow(rgb[1] / 255.f, 2.2f),
                                     std::pow(rgb[2] / 255.f, 2.2f)} *
                                    weight;
                    total += weight;
                }
            }
        }
        out.images.push_back(std::move(image));
    }
    if (total > 0) {
        out.ambient = sum * (1 / total);
        if (sky.type == 1) {
            out.ambient = {out.ambient.x / (1 + out.ambient.x),
                           out.ambient.y / (1 + out.ambient.y),
                           out.ambient.z / (1 + out.ambient.z)};
        }
    }
    return out;
}
} // namespace
bool SkyboxRenderer::init() {

    std::string vs, fs;
    try {
        vs = loadShaderData("skybox.vert");
        fs = loadShaderData("skybox.frag");
    } catch (const std::exception& e) {
        error = e.what();
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", e.what());
        return false;
    }
    auto v = shader(GL_VERTEX_SHADER, vs.c_str()), f = shader(GL_FRAGMENT_SHADER, fs.c_str());
    if (!v || !f) {
        if (v) {
            glDeleteShader(v);
        }
        if (f) {
            glDeleteShader(f);
        }
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, v);
    glAttachShader(program_, f);
    glLinkProgram(program_);
    glDeleteShader(v);
    glDeleteShader(f);
    int ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        shutdown();
        return false;
    }
    glGenVertexArrays(1, &vao_);
    inverseLocation_ = glGetUniformLocation(program_, "inverseVP");
    tintLocation_ = glGetUniformLocation(program_, "tint");
    opacityLocation_ = glGetUniformLocation(program_, "opacity");
    textureLocation_ = glGetUniformLocation(program_, "hasTexture");
    hdrLocation_ = glGetUniformLocation(program_, "isHDR");
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "faces"), 4);
    glUniform1i(glGetUniformLocation(program_, "panorama"), 5);
    return true;
}
void SkyboxRenderer::clear() {
    if (texture_) {
        glDeleteTextures(1, &texture_);
    }
    texture_ = 0;
    loaded_ = false;
    signature_.clear();
    requestKey_.clear();
    dependencies_.clear();
    pending_ = {};
    ready_ = false;
    ambient_ = {1, 1, 1};
    checked_ = {};
    error.clear();
}
void SkyboxRenderer::shutdown() {
    clear();
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
    }
    if (program_) {
        glDeleteProgram(program_);
    }
    vao_ = program_ = 0;
}
bool SkyboxRenderer::prepare(const Skybox& sky, const AssetManager& assets) {
    if (!validSkybox(sky)) {
        error = "Invalid Skybox properties";
        return false;
    }
    std::vector<std::string> paths =
        sky.type == 1 ? std::vector<std::string>{sky.hdr}
                      : std::vector<std::string>{
                            sky.right, sky.left, sky.top, sky.bottom, sky.back, sky.front};
    std::string key =
        assets.root.generic_string() + '|' + std::to_string(sky.type) + '|' + "RGB9_E5";
    for (const auto& path : paths) {
        key += '|' + std::to_string(path.size()) + ':' + path;
    }
    key += '|' + std::to_string(sky.cubemap.size()) + ':' + sky.cubemap;
    if (key != requestKey_) {
        clear();
        requestKey_ = key;
        dependencies_ = paths;
    }
    if (std::all_of(paths.begin(), paths.end(), [](const std::string& p) { return p.empty(); }) &&
        (sky.type == 1 || sky.cubemap.empty())) {
        ready_ = true;
        error.clear();
        return true;
    }
    if (pending_.valid()) {
        if (pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return true;
        }
        try {
            auto decoded = pending_.get();
            dependencies_ = std::move(decoded.paths);
            signature_ = std::move(decoded.stamp);
            const unsigned target = sky.type == 1 ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP;
            glGenTextures(1, &texture_);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(target, texture_);
            for (std::size_t i = 0; i < decoded.images.size(); ++i) {
                const auto& image = decoded.images[i];
                if (sky.type == 1) {
                    glTexImage2D(target,
                                 0,
                                 GL_RGB9_E5,
                                 image.width,
                                 image.height,
                                 0,
                                 GL_RGB,
                                 GL_FLOAT,
                                 image.floats.data());
                } else {
                    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + unsigned(i),
                                 0,
                                 GL_RGBA8,
                                 image.width,
                                 image.height,
                                 0,
                                 GL_RGBA,
                                 GL_UNSIGNED_BYTE,
                                 image.bytes.data());
                }
            }
            glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(
                target, GL_TEXTURE_WRAP_S, sky.type == 1 ? GL_REPEAT : GL_CLAMP_TO_EDGE);
            glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            if (sky.type == 0) {
                glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            }
            glActiveTexture(GL_TEXTURE0);
            ambient_ = decoded.ambient;
            ready_ = loaded_ = true;
            error.clear();
        } catch (const std::exception& e) {
            error = e.what();
            signature_.clear(); // Retry failures at the metadata-check cadence (also missing
                                // cubemap dependencies).
            checked_ = std::chrono::steady_clock::now();
            ready_ = false;
            if (texture_) {
                glDeleteTextures(1, &texture_);
            }
            texture_ = 0;
            glActiveTexture(GL_TEXTURE0);
        }
        return error.empty();
    }
    const auto now = std::chrono::steady_clock::now();
    if (checked_.time_since_epoch().count() && now - checked_ < std::chrono::seconds(1)) {
        return error.empty();
    }
    checked_ = now;
    const auto stamp = resourceStamp(assets, key, sky, dependencies_);
    if (stamp == signature_) {
        return error.empty();
    }
    // Abandoned jobs own all CPU data. Only the currently requested future can upload.
    if (texture_) {
        glDeleteTextures(1, &texture_);
    }
    texture_ = 0;
    loaded_ = ready_ = false;
    error.clear();
    ambient_ = {1, 1, 1};
    auto job =
        WorkerPool::shared().submit([sky, assets, key] { return decodeSkybox(sky, assets, key); });
    if (job) {
        pending_ = std::move(*job);
        signature_ = stamp;
    } else {
        checked_ = {};
    }
    return true;
}
void SkyboxRenderer::draw(const Skybox& sky,
                          const AssetManager& assets,
                          const Mat4& vp,
                          float opacity) {
    if (!validSkybox(sky)) {
        error = "Invalid Skybox properties";
        return;
    }
    prepare(sky, assets);
    if (opacity <= 0) {
        return;
    }
    Mat4 inv;
    if (!inverse(vp, inv)) {
        return;
    }
    glUseProgram(program_);
    glUniformMatrix4fv(inverseLocation_, 1, GL_FALSE, inv.m);
    glUniform3f(tintLocation_, sky.color.x, sky.color.y, sky.color.z);
    glUniform1f(opacityLocation_, opacity);
    glUniform1i(textureLocation_, loaded_);
    glUniform1i(hdrLocation_, sky.type == 1);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_CUBE_MAP, sky.type == 0 ? texture_ : 0);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, sky.type == 1 ? texture_ : 0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
}
