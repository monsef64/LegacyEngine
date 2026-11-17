#pragma once

#include <MyGUI.h>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct SDL_Window;

namespace ui {

struct Color {
    union { float r; float x; };
    union { float g; float y; };
    union { float b; float z; };
    union { float a; float w; };

    constexpr Color() : r(1.0f), g(1.0f), b(1.0f), a(1.0f) {}
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}

    uint32_t toAbgr() const;
    static Color fromAbgr(uint32_t abgr);
    MyGUI::Colour toMyGui() const {
        return MyGUI::Colour(r, g, b, a);
    }
};

constexpr uint32_t rgba32(int r, int g, int b, int a = 255) {
    return (static_cast<uint32_t>(a & 0xFF) << 24) | (static_cast<uint32_t>(b & 0xFF) << 16) |
           (static_cast<uint32_t>(g & 0xFF) << 8) | static_cast<uint32_t>(r & 0xFF);
}

struct SkinAtlasPalette {
    Color windowBg{0.10f, 0.12f, 0.15f, 1.0f};
    Color childBg{0.09f, 0.11f, 0.14f, 1.0f};
    Color popupBg{0.13f, 0.15f, 0.19f, 1.0f};
    Color border{0.22f, 0.26f, 0.32f, 1.0f};
    Color titleBg{0.08f, 0.10f, 0.13f, 1.0f};
    Color titleBgActive{0.14f, 0.19f, 0.26f, 1.0f};
    Color menuBarBg{0.08f, 0.10f, 0.13f, 1.0f};
    Color frameBg{0.16f, 0.19f, 0.24f, 1.0f};
    Color frameBgHovered{0.22f, 0.29f, 0.38f, 1.0f};
    Color frameBgActive{0.24f, 0.36f, 0.51f, 1.0f};
    Color button{0.18f, 0.25f, 0.34f, 1.0f};
    Color buttonHovered{0.24f, 0.41f, 0.61f, 1.0f};
    Color buttonActive{0.20f, 0.48f, 0.77f, 1.0f};
    Color header{0.18f, 0.27f, 0.38f, 1.0f};
    Color headerHovered{0.23f, 0.38f, 0.56f, 1.0f};
    Color headerActive{0.24f, 0.44f, 0.67f, 1.0f};
    Color checkMark{0.38f, 0.70f, 1.0f, 1.0f};
    Color scrollbarBg{0.08f, 0.10f, 0.13f, 1.0f};
    Color scrollbarGrab{0.27f, 0.32f, 0.40f, 1.0f};
    Color scrollbarGrabHovered{0.36f, 0.43f, 0.53f, 1.0f};
    Color scrollbarGrabActive{0.42f, 0.52f, 0.65f, 1.0f};
    Color tab{0.16f, 0.19f, 0.24f, 1.0f};
    Color tabHovered{0.24f, 0.41f, 0.61f, 1.0f};
    Color tabSelected{0.18f, 0.27f, 0.38f, 1.0f};
    Color text{0.88f, 0.91f, 0.95f, 1.0f};
    Color textDisabled{0.48f, 0.54f, 0.63f, 1.0f};
    float frameRounding = 4.0f;
    float frameBorderSize = 0.0f;
    bool glossy = false;
};

struct DrawClipRect {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 1e6f;
    float maxY = 1e6f;
};

struct OverlayDrawCmd {
    MyGUI::ITexture* texture = nullptr;
    unsigned externalGlTexture = 0;
    bool flipV = false;
    DrawClipRect clip{};
    bool hasClip = false;
    std::size_t vertexOffset = 0;
    std::size_t vertexCount = 0;
};

struct OverlayDrawBatch {
    int layerOrder = 0; // 0 = behind panels, 1 = over panels, 2 = over popups/tooltips
    std::vector<MyGUI::Vertex> vertices;
    std::vector<OverlayDrawCmd> commands;
};

class LegacyMyGuiDataManager final : public MyGUI::DataManager {
  public:
    void registerMemoryFile(const std::string& name, std::string content);
    void addSearchPath(const std::string& path);

    MyGUI::IDataStream* getData(const std::string& name) const override;
    void freeData(MyGUI::IDataStream* data) override;
    bool isDataExist(const std::string& name) const override;
    MyGUI::VectorString getDataListNames(const std::string& pattern) const override;
    std::string getDataPath(const std::string& name) const override;

  private:
    std::unordered_map<std::string, std::string> memoryFiles_;
    std::vector<std::string> searchPaths_;
};

class LegacyMyGuiVertexBuffer final : public MyGUI::IVertexBuffer {
  public:
    explicit LegacyMyGuiVertexBuffer(bool glEnabled);
    ~LegacyMyGuiVertexBuffer() override;

    void setVertexCount(size_t count) override;
    size_t getVertexCount() const override;
    MyGUI::Vertex* lock() override;
    void unlock() override;

    unsigned vao() const {
        return vao_;
    }
    const std::vector<MyGUI::Vertex>& cpuVertices() const {
        return vertices_;
    }
    void uploadDirect(const MyGUI::Vertex* data, std::size_t count);

  private:
    void ensureGlBuffers(std::size_t count);
    void destroyGlBuffers();

    bool glEnabled_ = false;
    std::vector<MyGUI::Vertex> vertices_;
    std::size_t requestedCount_ = 0;
    std::size_t gpuCapacity_ = 0;
    unsigned vao_ = 0;
    unsigned vbo_ = 0;
};

class LegacyMyGuiTexture final : public MyGUI::ITexture {
  public:
    LegacyMyGuiTexture(std::string name, bool glEnabled);
    ~LegacyMyGuiTexture() override;

    const std::string& getName() const override {
        return name_;
    }
    void createManual(int width,
                      int height,
                      MyGUI::TextureUsage usage,
                      MyGUI::PixelFormat format) override;
    void loadFromFile(const std::string& filename) override;
    void saveToFile(const std::string& filename) override;
    void setShader(const std::string& shaderName) override {
        shaderName_ = shaderName;
    }
    void destroy() override;
    void* lock(MyGUI::TextureUsage access) override;
    void unlock() override;
    bool isLocked() const override {
        return locked_;
    }
    int getWidth() const override {
        return width_;
    }
    int getHeight() const override {
        return height_;
    }
    MyGUI::PixelFormat getFormat() const override {
        return format_;
    }
    MyGUI::TextureUsage getUsage() const override {
        return usage_;
    }
    size_t getNumElemBytes() const override {
        return elemBytes_;
    }

    void wrapExternalGl(unsigned glId, int width, int height, bool flipV);
    void ensureGlUploaded();
    unsigned glTextureId() const {
        return external_ ? externalGlId_ : glId_;
    }
    bool flipV() const {
        return flipV_;
    }
    bool isExternal() const {
        return external_;
    }
    const std::vector<uint8_t>& cpuPixels() const {
        return pixels_;
    }

  private:
    void uploadToGl();

    std::string name_;
    std::string shaderName_;
    bool glEnabled_ = false;
    bool locked_ = false;
    bool external_ = false;
    bool flipV_ = false;
    int width_ = 0;
    int height_ = 0;
    std::size_t elemBytes_ = 4;
    MyGUI::PixelFormat format_ = MyGUI::PixelFormat::R8G8B8A8;
    MyGUI::TextureUsage usage_ = MyGUI::TextureUsage::Default;
    std::vector<uint8_t> pixels_;
    unsigned glId_ = 0;
    unsigned externalGlId_ = 0;
};

class LegacyMergedFont final : public MyGUI::IFont {
    MYGUI_RTTI_DERIVED(LegacyMergedFont)

  public:
    LegacyMergedFont() = default;
    ~LegacyMergedFont() override;

    void deserialization(MyGUI::xml::ElementPtr node, MyGUI::Version version) override;
    const MyGUI::GlyphInfo* getGlyphInfo(MyGUI::Char id) const override;
    MyGUI::ITexture* getTextureFont() const override {
        return texture_;
    }
    int getDefaultHeight() const override {
        return defaultHeight_;
    }

    bool hasGlyph(uint32_t codepoint) const;
    float measureWidth(std::string_view utf8, float fontSize = 0.0f) const;
    void buildEmbedded(float pixelSize = 18.0f);

  private:
    MyGUI::ITexture* texture_ = nullptr;
    int defaultHeight_ = 18;
    float pixelSize_ = 18.0f;
    std::unordered_map<uint32_t, MyGUI::GlyphInfo> glyphs_;
    MyGUI::GlyphInfo fallbackGlyph_{};
};

class LegacyMyGuiRenderManager final : public MyGUI::RenderManager, public MyGUI::IRenderTarget {
  public:
    explicit LegacyMyGuiRenderManager(bool headless);
    ~LegacyMyGuiRenderManager() override;

    bool initGl();
    void shutdown();

    MyGUI::IVertexBuffer* createVertexBuffer() override;
    void destroyVertexBuffer(MyGUI::IVertexBuffer* buffer) override;
    MyGUI::ITexture* createTexture(const std::string& name) override;
    void destroyTexture(MyGUI::ITexture* texture) override;
    MyGUI::ITexture* getTexture(const std::string& name) override;

    const MyGUI::IntSize& getViewSize() const override {
        return viewSize_;
    }
    MyGUI::VertexColourType getVertexFormat() const override {
        return MyGUI::VertexColourType::ColourABGR;
    }
    bool isFormatSupported(MyGUI::PixelFormat format, MyGUI::TextureUsage usage) override;
    void setViewSize(int width, int height) override;
    void registerShader(const std::string& shaderName,
                        const std::string& vertexProgramFile,
                        const std::string& fragmentProgramFile) override;

    void begin() override;
    void end() override;
    void doRender(MyGUI::IVertexBuffer* buffer, MyGUI::ITexture* texture, size_t count) override;
    const MyGUI::RenderTargetInfo& getInfo() const override {
        return info_;
    }

    void setFramebufferSize(int fbWidth, int fbHeight);
    void setFramebufferScale(float sx, float sy);
    float fbScaleX() const {
        return fbScaleX_;
    }
    float fbScaleY() const {
        return fbScaleY_;
    }
    bool glReady() const {
        return glReady_;
    }

    MyGUI::ITexture* wrapExternalTexture(unsigned glId, int width, int height, bool flipV);
    MyGUI::ITexture* whiteTexture();
    void updateSkinAtlas(const SkinAtlasPalette& palette);

    void clearOverlays();
    void submitOverlay(OverlayDrawBatch batch);
    void drawOneFrame(float dt);
    void renderStandaloneOverlay(int fbWidth, int fbHeight, const OverlayDrawBatch& batch);

    std::size_t lastFrameVertexCount() const {
        return lastFrameVertexCount_;
    }

  private:
    struct SavedGlState {
        std::array<bool, 7> enabled{};
        std::array<int, 2> polygonMode{};
        std::array<int, 6> blend{};
        std::array<unsigned char, 4> colorMask{};
        unsigned char depthMask = 1;
        int program = 0;
        int vao = 0;
        int vbo = 0;
        int activeTexture = 0;
        int texture = 0;
        std::array<int, 4> viewport{};
        std::array<int, 4> scissor{};
    };

    void renderOverlayBatches(int targetLayerOrder);

    bool headless_ = true;
    bool glReady_ = false;
    bool needsUpdate_ = true;
    int beginDepth_ = 0;
    MyGUI::IntSize viewSize_{1280, 720};
    int fbWidth_ = 1280;
    int fbHeight_ = 720;
    float fbScaleX_ = 1.0f;
    float fbScaleY_ = 1.0f;
    MyGUI::RenderTargetInfo info_{};
    std::unordered_map<std::string, std::unique_ptr<LegacyMyGuiTexture>> textures_;
    std::vector<OverlayDrawBatch> overlays_;
    std::unique_ptr<LegacyMyGuiVertexBuffer> overlayVb_;
    unsigned shaderProgram_ = 0;
    int uYScale_ = -1;
    int uFlipV_ = -1;
    SavedGlState savedState_{};
    std::size_t lastFrameVertexCount_ = 0;
};

class LegacyMyGuiPlatform {
  public:
    explicit LegacyMyGuiPlatform(bool headless);
    ~LegacyMyGuiPlatform();

    bool initGl(SDL_Window* window);
    void setClipboardCallbacks(std::function<void(const std::string&)> setter,
                               std::function<std::string()> getter);
    void setViewSize(int width, int height, int fbWidth = 0, int fbHeight = 0);
    void updateSkinAtlas(const SkinAtlasPalette& palette);

    LegacyMyGuiDataManager& dataManager() {
        return *dataManager_;
    }
    LegacyMyGuiRenderManager& renderManager() {
        return *renderManager_;
    }
    MyGUI::Gui& gui() {
        return *gui_;
    }
    LegacyMergedFont* defaultFont() const {
        return defaultFont_;
    }

  private:
    void registerEmbeddedResources();
    void onClipboardChanged(std::string_view type, std::string_view data);
    void onClipboardRequested(std::string_view type, std::string& data);

    std::unique_ptr<MyGUI::LogManager> logManager_;
    std::unique_ptr<LegacyMyGuiDataManager> dataManager_;
    std::unique_ptr<LegacyMyGuiRenderManager> renderManager_;
    std::unique_ptr<MyGUI::Gui> gui_;
    LegacyMergedFont* defaultFont_ = nullptr;
    std::function<void(const std::string&)> clipboardSet_;
    std::function<std::string()> clipboardGet_;
};

} // namespace ui
