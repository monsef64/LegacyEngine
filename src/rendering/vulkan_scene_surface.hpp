// Vulkan offscreen mesh rendering for Scene, Game and preview views.
#pragma once

#include "rendering/mesh.hpp"
#include "rendering/vulkan_shader_layout.hpp"
#include "rendering/vulkan_texture.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

class VulkanSceneSurface {
  public:
    struct Draw {
        MeshHandle mesh;
        Mat4 viewProjectionModel; // engine clip convention
        VulkanShaderLayout::Float4 color;
        bool mirrored = false; // negative determinant of the model transform
        bool transparent = false;
        // When lit, model and normalMatrix are world-space matrices. Textures
        // are optional, belong to the same device and must outlive render().
        bool lit = false;
        Mat4 model{}, normalMatrix{};
        const VulkanTexture* materialTexture = nullptr;
        const VulkanTexture* normalTexture = nullptr;
        float specular = 0, normalStrength = 1, tilingX = 1, tilingY = 1;
        bool checkerboard = false;
        bool castsShadow = true;
        // Unlit, transparent line overlays may ignore depth (e.g. collider gizmos).
        bool depthAlways = false;
    };
    struct SkyDraw {
        Mat4 viewProjection{}; // engine clip convention
        VulkanShaderLayout::Float4 tintOpacity{1, 1, 1, 1};
        const VulkanTexture* texture = nullptr; // cubemap, or 2D HDR panorama
        bool hdr = false;
    };
    // Optional viewport/scissor in top-left image pixels, for Camera viewport rectangles.
    struct RenderRegion {
        uint32_t x = 0, y = 0, width = 0, height = 0;
    };
    struct ShadowLayer {
        Mat4 viewProjection{};   // engine clip convention
        uint32_t resolution = 0; // 0 means this layer is unused
    };
    struct ShadowPlan {
        uint32_t atlasResolution = 1024;
        bool soft = true;
        std::array<ShadowLayer, 12> layers{};
    };

    // Device, graphics queue and their owning context must outlive this surface.
    // The SPIR-V must be compiled from data/shaders/vulkan/unlit.vert and .frag.
    // Calls and external use of the queue/device must be synchronized by caller.
    // Optional meshOwner shares mesh handles with another surface on the same
    // device and queue; the owner must outlive this surface and be an owner itself.
    VulkanSceneSurface(VkPhysicalDevice physical,
                       VkDevice device,
                       VkQueue graphicsQueue,
                       uint32_t queueFamily,
                       const std::vector<uint32_t>& vertexShader,
                       const std::vector<uint32_t>& fragmentShader,
                       VulkanSceneSurface* meshOwner = nullptr);
    ~VulkanSceneSurface();
    VulkanSceneSurface(const VulkanSceneSurface&) = delete;
    VulkanSceneSurface& operator=(const VulkanSceneSurface&) = delete;

    // With shadowVertexShader, fragmentShader must be the full lit.frag and
    // shadowVertexShader must be shadow.vert. Without it, fragmentShader must
    // be the LEGACY_NO_SHADOW variant. Call once before submitting lit draws.
    void enableLighting(const std::vector<uint32_t>& vertexShader,
                        const std::vector<uint32_t>& fragmentShader,
                        const std::vector<uint32_t>& shadowVertexShader = {});
    bool supportsShadows() const {
        return shadowRenderPass_ != VK_NULL_HANDLE;
    }
    // Installs the fullscreen skybox shaders. Required when rendering SkyDraws.
    void enableSkybox(const std::vector<uint32_t>& vertexShader,
                      const std::vector<uint32_t>& fragmentShader);
    bool supportsSkybox() const {
        return skyPipeline_ != VK_NULL_HANDLE;
    }
    MeshHandle createMesh(const MeshData& mesh);
    // Replace geometry while preserving the handle shared by all viewports.
    // Waits for device work before updating, and poisons the surface on failure.
    void updateMesh(MeshHandle handle, const MeshData& mesh);
    // Invalidates the handle across all surfaces sharing its mesh owner. The
    // ID is never reused, so stale handles cannot silently draw new geometry.
    void destroyMesh(MeshHandle handle);
    VkPhysicalDevice physicalDevice() const {
        return physical_;
    }
    VkDevice device() const {
        return device_;
    }
    VkQueue graphicsQueue() const {
        return queue_;
    }
    uint32_t graphicsFamily() const {
        return family_;
    }
    bool meshIsLines(MeshHandle handle) const;
    // Local-space bounds for editor selection and preview framing. Invalid,
    // destroyed and empty meshes return false.
    bool meshBounds(MeshHandle handle, Vec3& low, Vec3& high) const;
    bool sharesMeshesWith(const VulkanSceneSurface& other) const {
        return (meshOwner_ ? meshOwner_ : this) == (other.meshOwner_ ? other.meshOwner_ : &other);
    }
    // Resizes when necessary, clears, and renders in supplied order. The caller
    // sorts transparent meshes after opaque meshes, back-to-front. Waits for the
    // graphics queue before returning. The image is exclusive to this queue family;
    // callers must synchronize any other queues sampling it before every render,
    // resize, readback or destruction and handle queue-family ownership transfers.
    void render(uint32_t width,
                uint32_t height,
                const std::vector<Draw>& draws,
                VulkanShaderLayout::Float4 clear = {0.07f, 0.08f, 0.10f, 1.0f},
                const VulkanShaderLayout::Lighting* lighting = nullptr,
                const ShadowPlan* shadows = nullptr,
                const std::vector<SkyDraw>& skies = {},
                const RenderRegion* region = nullptr);
    // Prepare an image at this extent before registering it with ImGui. The
    // scene may then be rendered at the same extent later in the UI frame.
    // Never change extent after an ImGui draw command has referenced the image.
    void prepare(uint32_t width, uint32_t height);
    uint32_t width() const {
        return width_;
    }
    uint32_t height() const {
        return height_;
    }
    // RGBA8 top-to-bottom. Intended for tests; waits for the graphics queue.
    std::vector<uint8_t> readPixels();
    VkImageView imageView() const {
        return colorView_;
    }
    VkSampler sampler() const {
        return sampler_;
    }
    VkImageLayout imageLayout() const {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    // Changes whenever a resize replaces the image. An ImGui texture descriptor
    // must be re-registered when it changes, after in-flight users have finished.
    uint64_t imageGeneration() const {
        return imageGeneration_;
    }

  private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        bool coherent = false;
    };
    struct Mesh {
        Buffer vertices;
        uint32_t count = 0;
        bool lines = false, alive = false;
        Vec3 low{}, high{};
    };
    struct LightingFrame {
        Buffer uniforms;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> objects, materials;
    };
    struct SkyFrame {
        Buffer uniforms;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> sets;
    };
    SkyFrame prepareSky(const std::vector<SkyDraw>& skies);
    void destroySkyFrame(SkyFrame& frame) noexcept;
    void cleanupSky() noexcept;
    void initialize(const std::vector<uint32_t>& vertexShader,
                    const std::vector<uint32_t>& fragmentShader);
    LightingFrame prepareLighting(const std::vector<Draw>& draws,
                                  const VulkanShaderLayout::Lighting& lighting,
                                  bool softShadows);
    void initializeShadows(const std::vector<uint32_t>& vertexShader);
    void ensureShadowAtlas(uint32_t resolution);
    void destroyShadowAtlas() noexcept;
    void cleanupShadows() noexcept;
    void
    recordShadows(VkCommandBuffer commands, const std::vector<Draw>& draws, const ShadowPlan& plan);
    void destroyLightingFrame(LightingFrame& frame) noexcept;
    void cleanupLighting() noexcept;
    void resize(uint32_t width, uint32_t height);
    void destroyImages() noexcept;
    void cleanup() noexcept;
    Buffer makeBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
    void destroyBuffer(Buffer& buffer) noexcept;
    uint32_t memoryType(uint32_t bits,
                        VkMemoryPropertyFlags required,
                        VkMemoryPropertyFlags preferred = 0) const;
    void upload(Buffer& buffer, const void* data, VkDeviceSize size);
    VkCommandBuffer beginCommands();
    void submitCommands();

    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t family_ = 0;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer commands_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipelines_[7]{};
    VkDescriptorSetLayout litSetLayouts_[2]{};
    VkPipelineLayout litPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline litPipelines_[4]{};
    VkDescriptorSetLayout skySetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout skyPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline skyPipeline_ = VK_NULL_HANDLE;
    std::unique_ptr<VulkanTexture> skyWhiteCube_, skyWhitePanorama_;
    VkFormat shadowFormat_ = VK_FORMAT_UNDEFINED;
    VkRenderPass shadowRenderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline shadowPipelines_[2]{};
    VkImage shadowImage_ = VK_NULL_HANDLE;
    VkDeviceMemory shadowMemory_ = VK_NULL_HANDLE;
    VkImageView shadowArrayView_ = VK_NULL_HANDLE;
    std::array<VkImageView, 12> shadowLayerViews_{};
    std::array<VkFramebuffer, 12> shadowFramebuffers_{};
    VkSampler shadowSamplers_[2]{};
    uint32_t shadowResolution_ = 0;
    std::unique_ptr<VulkanTexture> white_, normal_, checker_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkImage color_ = VK_NULL_HANDLE, depth_ = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory_ = VK_NULL_HANDLE, depthMemory_ = VK_NULL_HANDLE;
    VkImageView colorView_ = VK_NULL_HANDLE, depthView_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    uint32_t width_ = 0, height_ = 0;
    uint64_t imageGeneration_ = 0;
    bool rendered_ = false, failed_ = false;
    VulkanSceneSurface* meshOwner_ = nullptr; // non-owning; nullptr for an owner
    std::vector<Mesh> meshes_;
};
