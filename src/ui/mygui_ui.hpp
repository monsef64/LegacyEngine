#pragma once

#include "core/math.hpp"
#include "ui/mygui_platform.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    constexpr Rect() = default;
    constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), width(w_), height(h_) {}
    bool contains(float px, float py) const {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
    Vec2 min() const {
        return {x, y};
    }
    Vec2 max() const {
        return {x + width, y + height};
    }
};

enum ColorRole : int {
    Col_Text = 0,
    Col_TextDisabled,
    Col_WindowBg,
    Col_ChildBg,
    Col_PopupBg,
    Col_Border,
    Col_FrameBg,
    Col_FrameBgHovered,
    Col_FrameBgActive,
    Col_TitleBg,
    Col_TitleBgActive,
    Col_TitleBgCollapsed,
    Col_MenuBarBg,
    Col_ScrollbarBg,
    Col_ScrollbarGrab,
    Col_ScrollbarGrabHovered,
    Col_ScrollbarGrabActive,
    Col_CheckMark,
    Col_SliderGrab,
    Col_SliderGrabActive,
    Col_Button,
    Col_ButtonHovered,
    Col_ButtonActive,
    Col_Header,
    Col_HeaderHovered,
    Col_HeaderActive,
    Col_Separator,
    Col_SeparatorHovered,
    Col_SeparatorActive,
    Col_ResizeGrip,
    Col_ResizeGripHovered,
    Col_ResizeGripActive,
    Col_Tab,
    Col_TabHovered,
    Col_TabSelected,
    Col_TabDimmed,
    Col_TabDimmedSelected,
    Col_TextSelectedBg,
    Col_NavCursor,
    Col_COUNT
};

enum StyleVar : int {
    StyleVar_WindowPadding = 0,
    StyleVar_WindowRounding,
    StyleVar_WindowBorderSize,
    StyleVar_FramePadding,
    StyleVar_FrameRounding,
};

struct Style {
    bool AntiAliasedLines = true;
    bool AntiAliasedLinesUseTex = true;
    bool AntiAliasedFill = true;
    Vec2 WindowPadding{10.0f, 8.0f};
    Vec2 FramePadding{6.0f, 4.0f};
    Vec2 ItemSpacing{8.0f, 6.0f};
    Vec2 ItemInnerSpacing{6.0f, 4.0f};
    float IndentSpacing = 16.0f;
    float ScrollbarSize = 12.0f;
    float GrabMinSize = 10.0f;
    float WindowRounding = 0.0f;
    float ChildRounding = 4.0f;
    float FrameRounding = 4.0f;
    float PopupRounding = 6.0f;
    float ScrollbarRounding = 6.0f;
    float GrabRounding = 3.0f;
    float TabRounding = 4.0f;
    float WindowBorderSize = 1.0f;
    float FrameBorderSize = 0.0f;
    float PopupBorderSize = 1.0f;
    Vec2 WindowTitleAlign{0.0f, 0.5f};
    std::array<Color, Col_COUNT> Colors{};
};

enum Cond : int {
    Cond_None = 0,
    Cond_Always = 1 << 0,
    Cond_Once = 1 << 1,
    Cond_FirstUseEver = 1 << 2,
    Cond_Appearing = 1 << 3,
};

enum Dir : int {
    Dir_Left = 0,
    Dir_Right,
    Dir_Up,
    Dir_Down,
};

enum MouseButton : int {
    MouseButton_Left = 0,
    MouseButton_Right = 1,
    MouseButton_Middle = 2,
};

enum Key : uint32_t {
    Key_None = 0,
    Key_Tab,
    Key_LeftArrow,
    Key_RightArrow,
    Key_UpArrow,
    Key_DownArrow,
    Key_Home,
    Key_End,
    Key_Delete,
    Key_Backspace,
    Key_Enter,
    Key_Escape,
    Key_A,
    Key_C,
    Key_D,
    Key_E,
    Key_F,
    Key_O,
    Key_P,
    Key_R,
    Key_S,
    Key_V,
    Key_W,
    Key_X,
    Key_Y,
    Key_Z,
    Key_F2,
    Key_F5,
    Key_F6,
    Key_F7,
    Key_COUNT,
    Mod_Ctrl = 1u << 16,
    Mod_Shift = 1u << 17,
    Mod_Alt = 1u << 18,
};

using KeyChord = uint32_t;
using ID = uint64_t;
using TextureID = unsigned;
inline constexpr KeyChord chord(uint32_t a, uint32_t b = 0, uint32_t c = 0) {
    return a | b | c;
}

#ifndef UI_COL32
#define UI_COL32(R, G, B, A) \
    (((uint32_t)(A) << 24) | ((uint32_t)(B) << 16) | ((uint32_t)(G) << 8) | (uint32_t)(R))
#define UI_COL32_WHITE UI_COL32(255, 255, 255, 255)
#endif

using WindowFlags = uint32_t;
constexpr WindowFlags WindowFlags_None = 0;
constexpr WindowFlags WindowFlags_NoTitleBar = 1u << 0;
constexpr WindowFlags WindowFlags_NoResize = 1u << 1;
constexpr WindowFlags WindowFlags_NoMove = 1u << 2;
constexpr WindowFlags WindowFlags_NoScrollbar = 1u << 3;
constexpr WindowFlags WindowFlags_NoScrollWithMouse = 1u << 4;
constexpr WindowFlags WindowFlags_NoCollapse = 1u << 5;
constexpr WindowFlags WindowFlags_AlwaysAutoResize = 1u << 6;
constexpr WindowFlags WindowFlags_NoSavedSettings = 1u << 7;
constexpr WindowFlags WindowFlags_HorizontalScrollbar = 1u << 8;
constexpr WindowFlags WindowFlags_NoFocusOnAppearing = 1u << 9;
constexpr WindowFlags WindowFlags_NoBringToFrontOnFocus = 1u << 10;
constexpr WindowFlags WindowFlags_NoNavFocus = 1u << 11;
constexpr WindowFlags WindowFlags_NoDocking = 1u << 12;
constexpr WindowFlags WindowFlags_NoDecoration =
    WindowFlags_NoTitleBar | WindowFlags_NoResize | WindowFlags_NoScrollbar | WindowFlags_NoCollapse;

using ChildFlags = uint32_t;
constexpr ChildFlags ChildFlags_None = 0;
constexpr ChildFlags ChildFlags_Borders = 1u << 0;
constexpr ChildFlags ChildFlags_AlwaysUseWindowPadding = 1u << 1;

using InputTextFlags = uint32_t;
constexpr InputTextFlags InputTextFlags_None = 0;
constexpr InputTextFlags InputTextFlags_EnterReturnsTrue = 1u << 0;
constexpr InputTextFlags InputTextFlags_AutoSelectAll = 1u << 1;
constexpr InputTextFlags InputTextFlags_CallbackCompletion = 1u << 2;
constexpr InputTextFlags InputTextFlags_CallbackAlways = 1u << 3;
constexpr InputTextFlags InputTextFlags_HiddenText = 1u << 4;

using SelectableFlags = uint32_t;
constexpr SelectableFlags SelectableFlags_None = 0;
constexpr SelectableFlags SelectableFlags_AllowDoubleClick = 1u << 0;
constexpr SelectableFlags SelectableFlags_AllowOverlap = 1u << 1;

using TreeNodeFlags = uint32_t;
constexpr TreeNodeFlags TreeNodeFlags_None = 0;
constexpr TreeNodeFlags TreeNodeFlags_DefaultOpen = 1u << 0;
constexpr TreeNodeFlags TreeNodeFlags_SpanAvailWidth = 1u << 1;

using SliderFlags = uint32_t;
constexpr SliderFlags SliderFlags_None = 0;
constexpr SliderFlags SliderFlags_AlwaysClamp = 1u << 0;

using HoveredFlags = uint32_t;
constexpr HoveredFlags HoveredFlags_None = 0;
constexpr HoveredFlags HoveredFlags_ChildWindows = 1u << 0;
constexpr HoveredFlags HoveredFlags_AllowWhenDisabled = 1u << 1;
constexpr HoveredFlags HoveredFlags_ForTooltip = 1u << 2;

using FocusedFlags = uint32_t;
constexpr FocusedFlags FocusedFlags_None = 0;
constexpr FocusedFlags FocusedFlags_RootAndChildWindows = 1u << 0;

using PopupFlags = uint32_t;
constexpr PopupFlags PopupFlags_None = 0;
constexpr PopupFlags PopupFlags_MouseButtonRight = 1u << 0;
constexpr PopupFlags PopupFlags_NoOpenOverItems = 1u << 1;
constexpr PopupFlags PopupFlags_AnyPopupId = 1u << 2;

using InputFlags = uint32_t;
constexpr InputFlags InputFlags_None = 0;
constexpr InputFlags InputFlags_RouteFocused = 1u << 0;
constexpr InputFlags InputFlags_RouteGlobal = 1u << 1;

using TableFlags = uint32_t;
constexpr TableFlags TableFlags_None = 0;
constexpr TableFlags TableFlags_Resizable = 1u << 0;
constexpr TableFlags TableFlags_RowBg = 1u << 1;
constexpr TableFlags TableFlags_BordersInnerV = 1u << 2;
constexpr TableFlags TableFlags_Borders = 1u << 3;
constexpr TableFlags TableFlags_SizingStretchProp = 1u << 4;
constexpr TableFlags TableFlags_SizingStretchSame = 1u << 5;
constexpr TableFlags TableFlags_NoHostExtendX = 1u << 6;
constexpr TableFlags TableFlags_ScrollX = 1u << 7;
constexpr TableFlags TableFlags_ScrollY = 1u << 8;

using TableColumnFlags = uint32_t;
constexpr TableColumnFlags TableColumnFlags_None = 0;
constexpr TableColumnFlags TableColumnFlags_WidthFixed = 1u << 0;
constexpr TableColumnFlags TableColumnFlags_WidthStretch = 1u << 1;

struct IO {
    Vec2 DisplaySize{1280.0f, 720.0f};
    Vec2 DisplayFramebufferScale{1.0f, 1.0f};
    float DeltaTime = 1.0f / 60.0f;
    float Framerate = 60.0f;
    Vec2 MousePos{-1.0f, -1.0f};
    Vec2 MouseDelta{0.0f, 0.0f};
    float MouseWheel = 0.0f;
    float MouseWheelH = 0.0f;
    bool KeyCtrl = false;
    bool KeyShift = false;
    bool KeyAlt = false;
    bool WantCaptureMouse = false;
    bool WantCaptureKeyboard = false;
    bool WantTextInput = false;
    const char* IniFilename = nullptr;
};

struct Viewport {
    Vec2 Pos{0.0f, 0.0f};
    Vec2 Size{1280.0f, 720.0f};
    Vec2 WorkPos{0.0f, 0.0f};
    Vec2 WorkSize{1280.0f, 720.0f};
};

struct Payload {
    std::string type;
    std::vector<uint8_t> bytes;
    const void* data() const {
        return bytes.empty() ? nullptr : bytes.data();
    }
    std::size_t size() const {
        return bytes.size();
    }
};

struct InputTextCallbackData {
    char* buf = nullptr;
    std::size_t bufSize = 0;
    int bufTextLen = 0;
    int cursorPos = 0;
    InputTextFlags eventFlag = InputTextFlags_None;
    void* userData = nullptr;

    void deleteChars(int pos, int count);
    void insertChars(int pos, const char* text);
};

using InputTextCallback = int (*)(InputTextCallbackData* data);

class DrawList {
  public:
    DrawList() = default;

    void reset(float viewW, float viewH, LegacyMergedFont* font, MyGUI::ITexture* whiteTex);
    void pushClipRect(Vec2 minPt, Vec2 maxPt, bool intersect = false);
    void popClipRect();

    void addLine(Vec2 a, Vec2 b, uint32_t col, float thickness = 1.0f);
    void addRect(Vec2 a, Vec2 b, uint32_t col, float rounding = 0.0f, float thickness = 1.0f);
    void addRectFilled(Vec2 a, Vec2 b, uint32_t col, float rounding = 0.0f);
    void addQuad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, uint32_t col, float thickness = 1.0f);
    void addQuadFilled(Vec2 a, Vec2 b, Vec2 c, Vec2 d, uint32_t col);
    void addTriangle(Vec2 a, Vec2 b, Vec2 c, uint32_t col, float thickness = 1.0f);
    void addTriangleFilled(Vec2 a, Vec2 b, Vec2 c, uint32_t col);
    void addCircle(Vec2 center, float radius, uint32_t col, int segments = 0, float thickness = 1.0f);
    void addCircleFilled(Vec2 center, float radius, uint32_t col, int segments = 0);
    void pathArcTo(Vec2 center, float radius, float aMin, float aMax, int segments = 16);
    void pathStroke(uint32_t col, bool closed = false, float thickness = 1.0f);
    void addText(Vec2 pos, uint32_t col, const char* textBegin, const char* textEnd = nullptr);
    void addText(float fontSize,
                 Vec2 pos,
                 uint32_t col,
                 const char* textBegin,
                 const char* textEnd = nullptr);
    void addText(LegacyMergedFont* font,
                 float fontSize,
                 Vec2 pos,
                 uint32_t col,
                 const char* textBegin,
                 const char* textEnd = nullptr);
    void addImage(unsigned glTexture,
                  Vec2 a,
                  Vec2 b,
                  Vec2 uv0 = {0.0f, 0.0f},
                  Vec2 uv1 = {1.0f, 1.0f},
                  uint32_t col = 0xFFFFFFFFu);

    OverlayDrawBatch buildBatch(int layerOrder) const;
    const std::vector<MyGUI::Vertex>& vertices() const {
        return vertices_;
    }
    std::vector<MyGUI::Vertex>& mutableVertices() {
        return vertices_;
    }
    const std::vector<OverlayDrawCmd>& commands() const {
        return commands_;
    }
    Vec2 clipRectMin() const;
    Vec2 clipRectMax() const;

  private:
    MyGUI::Vertex makeVertex(float x, float y, float u, float v, uint32_t col) const;
    void pushTri(const MyGUI::Vertex& v0,
                 const MyGUI::Vertex& v1,
                 const MyGUI::Vertex& v2,
                 MyGUI::ITexture* tex,
                 unsigned extGlTex = 0,
                 bool flipV = false);
    void pushQuadRaw(Vec2 a,
                     Vec2 b,
                     Vec2 uv0,
                     Vec2 uv1,
                     uint32_t col,
                     MyGUI::ITexture* tex,
                     unsigned extGlTex = 0,
                     bool flipV = false);

    float viewW_ = 1280.0f;
    float viewH_ = 720.0f;
    LegacyMergedFont* font_ = nullptr;
    MyGUI::ITexture* whiteTex_ = nullptr;
    std::vector<DrawClipRect> clipStack_;
    std::vector<Vec2> path_;
    std::vector<MyGUI::Vertex> vertices_;
    std::vector<OverlayDrawCmd> commands_;
};

enum class GizmoOperation { Translate, Rotate, Scale };

struct GizmoResult {
    Mat4 matrix{};
    bool changed = false;
    bool hovered = false;
    bool usingHandle = false;
};

struct TextFilter {
    char inputBuf[256]{};
    void clear() {
        inputBuf[0] = '\0';
    }
    bool draw(const char* label = "Filter", float width = 0.0f);
    bool active() const {
        return inputBuf[0] != '\0';
    }
    bool passFilter(const char* text, const char* textEnd = nullptr) const;
};

struct PanelInfo {
    std::string name;
    Rect rect;
    uint32_t dockId = 0;
    uint32_t tabId = 0;
    std::vector<std::string> dockTabs;
    std::string activeDockTab;
    bool open = false;
    bool visible = false;
    MyGUI::Widget* widget = nullptr;
};

struct MultilineEditInfo {
    Rect innerRect;
    Vec2 textOrigin;
    float scrollX = 0.0f;
    float scrollY = 0.0f;
    bool active = false;
    DrawList* drawList = nullptr;
};

// ---------------------------------------------------------------------------
// Context & Platform Lifecycle (MyGUI)
// ---------------------------------------------------------------------------

bool createContext(bool headless = true);
bool initGl(SDL_Window* window);
void destroyContext();
bool hasContext();
LegacyMyGuiPlatform* platform();
MyGUI::Gui* myGui();

bool processSdlEvent(const SDL_Event& event);
void setDisplaySize(float width, float height, float fbScaleX = 1.0f, float fbScaleY = 1.0f);
void setDeltaTime(float dt);
void injectMousePos(float x, float y);
void injectMouseButton(int button, bool down);
void injectMouseWheel(float wheel, float horizontal = 0.0f);
void injectKey(Key key, bool down);
void injectKeyMod(uint32_t modFlag, bool down);
void injectTextUtf8(const char* utf8);
void setClipboardCallbacks(std::function<void(const std::string&)> setter,
                           std::function<std::string()> getter);
void setClipboardText(const char* text);
std::string getClipboardText();

void newFrame();
void render();
std::size_t lastFrameVertexCount();
void renderStandaloneFpsOverlay(int fbWidth, int fbHeight, const char* label);

// ---------------------------------------------------------------------------
// Style & Theme
// ---------------------------------------------------------------------------

IO& io();
Style& style();
void styleColorsDark(Style* target = nullptr);
void styleColorsLight(Style* target = nullptr);
void syncThemeToMyGui(bool glossy = false);
Color styleColor(ColorRole role);
uint32_t colorU32(ColorRole role);
uint32_t colorU32(Color c);
Color colorFromU32(uint32_t abgr);

void pushStyleColor(ColorRole role, Color col);
void popStyleColor(int count = 1);
void pushStyleVar(StyleVar var, float val);
void pushStyleVar(StyleVar var, Vec2 val);
void popStyleVar(int count = 1);

// ---------------------------------------------------------------------------
// Viewport, Font, Timing
// ---------------------------------------------------------------------------

const Viewport* mainViewport();
float fontSize();
LegacyMergedFont* defaultFont();
Vec2 calcTextSize(const char* text, const char* textEnd = nullptr);
float textLineHeight();
float frameHeight();
float frameHeightWithSpacing();
double timeSeconds();
int frameCount();
float hoveredIdTimer();

// ---------------------------------------------------------------------------
// ID Stack
// ---------------------------------------------------------------------------

void pushId(const char* strId);
void pushId(int intId);
void pushId(const void* ptrId);
void popId();
uint64_t getId(const char* strId);
uint64_t hashStr(const char* str);

// ---------------------------------------------------------------------------
// Docking Workspace (MyGUI Panes & Tab Bars)
// ---------------------------------------------------------------------------

void dockWorkspaceBegin(Rect area, bool& reset, bool* panelOpenArray, int panelCount);
void dockWorkspaceEnd();
const PanelInfo* findPanel(const char* name);

// ---------------------------------------------------------------------------
// Windows, Children, Popups, Modals, Menus
// ---------------------------------------------------------------------------

void setNextWindowPos(Vec2 pos, Cond cond = Cond_Always, Vec2 pivot = {0.0f, 0.0f});
void setNextWindowSize(Vec2 size, Cond cond = Cond_Always);
void setNextWindowSizeConstraints(Vec2 minSize, Vec2 maxSize);
void setNextWindowFocus();
void setWindowFocus(const char* name);

bool beginWindow(const char* name, bool* open = nullptr, WindowFlags flags = WindowFlags_None);
void endWindow();

bool beginChild(const char* strId,
                Vec2 size = {0.0f, 0.0f},
                ChildFlags childFlags = ChildFlags_None,
                WindowFlags windowFlags = WindowFlags_None);
void endChild();

bool beginMainMenuBar();
void endMainMenuBar();
bool beginMenu(const char* label, bool enabled = true);
void endMenu();
bool menuItem(const char* label,
              const char* shortcutText = nullptr,
              bool selected = false,
              bool enabled = true);
bool menuItem(const char* label, const char* shortcutText, bool* selected, bool enabled = true);

void openPopup(const char* strId);
bool beginPopup(const char* strId, WindowFlags flags = WindowFlags_None);
bool beginPopupModal(const char* name, bool* open = nullptr, WindowFlags flags = WindowFlags_None);
bool beginPopupContextItem(const char* strId = nullptr,
                           PopupFlags flags = PopupFlags_MouseButtonRight);
bool beginPopupContextWindow(const char* strId = nullptr,
                             PopupFlags flags = PopupFlags_MouseButtonRight);
void endPopup();
void closeCurrentPopup();
bool isPopupOpen(const char* strId, PopupFlags flags = PopupFlags_None);
Rect topmostPopupRect();

bool beginTooltip();
void endTooltip();
void setTooltip(const char* fmt, ...);

// ---------------------------------------------------------------------------
// Layout & Cursor
// ---------------------------------------------------------------------------

void sameLine(float offsetFromStartX = 0.0f, float spacing = -1.0f);
void newLine();
void spacing();
void dummy(Vec2 size);
void separator();
void separatorText(const char* label);
void indent(float indentW = 0.0f);
void unindent(float indentW = 0.0f);
void alignTextToFramePadding();
void setNextItemWidth(float width);
void pushTextWrapPos(float wrapLocalPosX = 0.0f);
void popTextWrapPos();
void beginDisabled(bool disabled = true);
void endDisabled();

Vec2 cursorPos();
float cursorPosX();
void setCursorPos(Vec2 localPos);
void setCursorPosX(float localX);
Vec2 cursorScreenPos();
void setCursorScreenPos(Vec2 screenPos);
Vec2 contentRegionAvail();
Vec2 windowContentRegionMax();
Rect currentWindowInnerRect();
float scrollY();
float scrollMaxY();
void setScrollHereY(float centerRatio = 0.5f);

// ---------------------------------------------------------------------------
// Widgets (Retained MyGUI Widgets)
// ---------------------------------------------------------------------------

void textUnformatted(const char* text, const char* textEnd = nullptr);
void text(const char* fmt, ...);
void textDisabled(const char* fmt, ...);
void textColored(Color col, const char* fmt, ...);
void textWrapped(const char* fmt, ...);

bool button(const char* label, Vec2 size = {0.0f, 0.0f});
bool smallButton(const char* label);
bool invisibleButton(const char* strId, Vec2 size);
bool arrowButton(const char* strId, Dir dir);
bool checkbox(const char* label, bool* v);
bool radioButton(const char* label, bool active);
bool selectable(const char* label,
                bool selected = false,
                SelectableFlags flags = SelectableFlags_None,
                Vec2 size = {0.0f, 0.0f});

void setNextItemOpen(bool isOpen, Cond cond = Cond_Always);
bool treeNode(const char* label);
bool treeNodeEx(const char* strId, TreeNodeFlags flags, const char* fmt = nullptr, ...);
void treePop();
bool collapsingHeader(const char* label, TreeNodeFlags flags = TreeNodeFlags_None);

bool beginCombo(const char* label, const char* previewValue);
void endCombo();
bool combo(const char* label, int* currentItem, const char* itemsSeparatedByZeros);
bool combo(const char* label, int* currentItem, const char* const items[], int itemCount);

void setKeyboardFocusHere(int offset = 0);
bool inputText(const char* label,
               char* buf,
               std::size_t bufSize,
               InputTextFlags flags = InputTextFlags_None,
               InputTextCallback callback = nullptr,
               void* userData = nullptr);
bool inputTextMultiline(const char* label,
                        char* buf,
                        std::size_t bufSize,
                        Vec2 size = {0.0f, 0.0f},
                        InputTextFlags flags = InputTextFlags_None,
                        InputTextCallback callback = nullptr,
                        void* userData = nullptr,
                        MultilineEditInfo* outInfo = nullptr);

bool dragFloat(const char* label,
               float* v,
               float speed = 0.1f,
               float vMin = 0.0f,
               float vMax = 0.0f,
               const char* format = "%.3f",
               SliderFlags flags = SliderFlags_None);
bool dragFloat2(const char* label,
                float v[2],
                float speed = 0.1f,
                float vMin = 0.0f,
                float vMax = 0.0f,
                const char* format = "%.3f",
                SliderFlags flags = SliderFlags_None);
bool dragFloat3(const char* label,
                float v[3],
                float speed = 0.1f,
                float vMin = 0.0f,
                float vMax = 0.0f,
                const char* format = "%.3f",
                SliderFlags flags = SliderFlags_None);
bool dragFloat4(const char* label,
                float v[4],
                float speed = 0.1f,
                float vMin = 0.0f,
                float vMax = 0.0f,
                const char* format = "%.3f",
                SliderFlags flags = SliderFlags_None);
bool dragScalarNFloat(const char* label,
                      float* v,
                      int components,
                      float speed = 0.1f,
                      const float* vMin = nullptr,
                      const float* vMax = nullptr,
                      const char* format = "%.3f",
                      SliderFlags flags = SliderFlags_None);
bool dragInt(const char* label,
             int* v,
             float speed = 1.0f,
             int vMin = 0,
             int vMax = 0,
             const char* format = "%d",
             SliderFlags flags = SliderFlags_None);

bool sliderFloat(const char* label,
                 float* v,
                 float vMin,
                 float vMax,
                 const char* format = "%.3f",
                 SliderFlags flags = SliderFlags_None);
bool sliderInt(const char* label, int* v, int vMin, int vMax, const char* format = "%d");

bool inputFloat(const char* label,
                float* v,
                float step = 0.0f,
                float stepFast = 0.0f,
                const char* format = "%.3f");
bool inputInt(const char* label, int* v, int step = 1, int stepFast = 100);

bool colorEdit3(const char* label, float col[3]);

void image(unsigned glTexture,
           Vec2 size,
           Vec2 uv0 = {0.0f, 0.0f},
           Vec2 uv1 = {1.0f, 1.0f});
void progressBar(float fraction, Vec2 size = {-1.0f, 0.0f}, const char* overlay = nullptr);
void plotLines(const char* label,
               const float* values,
               int valuesCount,
               int valuesOffset = 0,
               const char* overlayText = nullptr,
               float scaleMin = 0.0f,
               float scaleMax = 1.0f,
               Vec2 graphSize = {0.0f, 0.0f});
void plotHistogram(const char* label,
                   const float* values,
                   int valuesCount,
                   int valuesOffset = 0,
                   const char* overlayText = nullptr,
                   float scaleMin = 0.0f,
                   float scaleMax = 1.0f,
                   Vec2 graphSize = {0.0f, 0.0f});

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

bool beginTable(const char* strId,
                int columns,
                TableFlags flags = TableFlags_None,
                Vec2 outerSize = {0.0f, 0.0f});
void endTable();
void tableSetupColumn(const char* label,
                      TableColumnFlags flags = TableColumnFlags_None,
                      float initWidth = 0.0f);
void tableSetupScrollFreeze(int cols, int rows);
void tableHeadersRow();
void tableNextRow(float minRowHeight = 0.0f);
bool tableNextColumn();
bool tableSetColumnIndex(int col);

// ---------------------------------------------------------------------------
// Drag & Drop
// ---------------------------------------------------------------------------

bool beginDragDropSource();
bool setDragDropPayload(const char* type, const void* data, std::size_t size);
void endDragDropSource();
bool beginDragDropTarget();
bool beginDragDropTargetCustom(Rect rect, uint64_t id);
const Payload* acceptDragDropPayload(const char* type);
void endDragDropTarget();

// ---------------------------------------------------------------------------
// Item, Window, Mouse, Keyboard Queries & Drawing
// ---------------------------------------------------------------------------

bool isItemHovered(HoveredFlags flags = HoveredFlags_None);
bool isItemActive();
bool isItemClicked(int button = MouseButton_Left);
bool isItemVisible();
Vec2 itemRectMin();
Vec2 itemRectMax();
bool isAnyItemActive();
bool isAnyItemHovered();
bool isWindowHovered(HoveredFlags flags = HoveredFlags_None);
bool isWindowFocused(FocusedFlags flags = FocusedFlags_None);
bool isMouseHoveringRect(Vec2 rMin, Vec2 rMax);
bool isMouseDown(int button);
bool isMouseClicked(int button);
bool isMouseReleased(int button);
bool isMouseDoubleClicked(int button);
bool isMouseDragging(int button, float threshold = -1.0f);
bool isMouseDragPastThreshold(int button, float threshold = -1.0f);
Vec2 mousePos();
bool isKeyPressed(Key key, bool repeat = true);
bool shortcut(KeyChord keyChord, InputFlags flags = InputFlags_RouteFocused);

DrawList* windowDrawList();
DrawList* backgroundDrawList();
DrawList* foregroundDrawList();
GizmoResult manipulateGizmo(DrawList* draw,
                            Rect viewport,
                            const Mat4& viewProjection,
                            Vec3 pivot,
                            Quat rotation,
                            Vec3 scale,
                            GizmoOperation operation,
                            bool localSpace,
                            bool snapEnabled,
                            float snapStep);
void cancelGizmo();

} // namespace ui
