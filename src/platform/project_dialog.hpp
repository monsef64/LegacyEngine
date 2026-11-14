#pragma once
#include "models/model_import.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

// SDL may invoke file-dialog callbacks on a different thread, or after the
// editor closes. Requests own shared state; only the main thread touches ECS/UI.
class ProjectDialog {
  public:
    struct Result {
        bool saving = false;
        std::string path, error;
    };
    bool pending() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->busy;
    }
    void show(SDL_Window* window, bool saving, const std::string& path) {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->busy) {
                return;
            }
            state_->busy = true;
        }
        auto* request = new Request{state_, saving};
        static const SDL_DialogFileFilter filters[] = {{"LegacyEngine project", "legacy"}};
        if (saving) {
            SDL_ShowSaveFileDialog(callback,
                                   request,
                                   window,
                                   filters,
                                   1,
                                   path.empty() ? "project.legacy" : path.c_str());
        } else {
            SDL_ShowOpenFileDialog(callback,
                                   request,
                                   window,
                                   filters,
                                   1,
                                   path.empty() ? nullptr : path.c_str(),
                                   false);
        }
    }
    void showImport(SDL_Window* window) {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->busy) {
                return;
            }
            state_->busy = true;
        }
        static const std::string extensions = modelImportExtensions();
        static const SDL_DialogFileFilter filters[]{
            {"3D Models (all Assimp formats)", extensions.c_str()},
            {"glTF / GLB", "gltf;glb"},
            {"FBX", "fbx"},
            {"Wavefront OBJ", "obj"},
            {"Collada", "dae"},
            {"Blender", "blend"},
            {"STL / PLY", "stl;ply"},
            {"LegacyEngine Mesh", "lemesh"},
            {"Scripts", "ls;lua"},
            {"Images", "png;jpg;jpeg;bmp;tga;hdr"},
            {"Audio", "wav;mp3;ogg;flac"},
            {"All files", "*"}};
        SDL_ShowOpenFileDialog(callback,
                               new Request{state_, false},
                               window,
                               filters,
                               static_cast<int>(sizeof(filters) / sizeof(filters[0])),
                               nullptr,
                               false);
    }
    std::optional<Result> poll() {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->result) {
            return {};
        }
        auto result = std::move(state_->result);
        state_->result.reset();
        state_->busy = false;
        return result;
    }

  private:
    struct State {
        std::mutex mutex;
        bool busy = false;
        std::optional<Result> result;
    };
    struct Request {
        std::shared_ptr<State> state;
        bool saving;
    };
    std::shared_ptr<State> state_ = std::make_shared<State>();
    static void SDLCALL callback(void* userdata, const char* const* files, int) {
        std::unique_ptr<Request> request(static_cast<Request*>(userdata));
        Result result;
        result.saving = request->saving;
        if (!files) {
            result.error = SDL_GetError();
            if (result.error.empty()) {
                result.error = "System file picker could not be opened";
            }
        } else if (files[0]) {
            result.path = files[0]; // Empty list means user cancelled.
        }
        std::lock_guard<std::mutex> lock(request->state->mutex);
        request->state->result = std::move(result);
    }
};
