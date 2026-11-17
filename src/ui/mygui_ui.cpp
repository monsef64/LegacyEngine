#include "ui/mygui_ui.hpp"

#include <MyGUI.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>

namespace ui {
namespace {
constexpr float kPi = 3.14159265358979323846f;
}

namespace {

constexpr float kDefaultFontSize = 18.0f;
constexpr float kDefaultLineHeight = 22.0f;
constexpr float kMinItemHeight = 18.0f;
constexpr int kTitleHeight = 26;
constexpr int kDockTabHeight = 25;

int clampInt(float value) {
    if (!std::isfinite(value)) {
        return 0;
    }
    return static_cast<int>(std::lround(value));
}

std::string formatString(const char* fmt, va_list args) {
    if (!fmt) {
        return {};
    }
    va_list copy;
    va_copy(copy, args);
    const int n = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (n <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(n), '\0');
    std::vsnprintf(result.data(), result.size() + 1, fmt, args);
    return result;
}

std::string formatString(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    auto result = formatString(fmt, args);
    va_end(args);
    return result;
}

std::string_view visibleLabel(const char* label) {
    if (!label) {
        return {};
    }
    std::string_view text(label);
    const auto hidden = text.find("##");
    return hidden == std::string_view::npos ? text : text.substr(0, hidden);
}

uint32_t foldHash(uint32_t value, std::string_view text) {
    for (unsigned char c : text) {
        value ^= c;
        value *= 16777619u;
    }
    return value;
}

uint32_t floatColor(float r, float g, float b, float a = 1.0f) {
    return UI_COL32(static_cast<int>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<int>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<int>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<int>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f));
}

uint32_t colorWithAlpha(uint32_t color, float alpha) {
    const float base = static_cast<float>((color >> 24) & 0xFFu) / 255.0f;
    return (color & 0x00FFFFFFu) |
           (static_cast<uint32_t>(std::clamp(base * alpha, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
}

uint32_t utf8Codepoint(const char*& p, const char* end) {
    if (p >= end) {
        return 0;
    }
    const auto first = static_cast<unsigned char>(*p++);
    if (first < 0x80) {
        return first;
    }
    if ((first & 0xE0) == 0xC0 && p < end) {
        const auto c1 = static_cast<unsigned char>(*p++);
        return ((first & 0x1Fu) << 6) | (c1 & 0x3Fu);
    }
    if ((first & 0xF0) == 0xE0 && p + 1 < end) {
        const auto c1 = static_cast<unsigned char>(*p++);
        const auto c2 = static_cast<unsigned char>(*p++);
        return ((first & 0x0Fu) << 12) | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
    }
    if ((first & 0xF8) == 0xF0 && p + 2 < end) {
        const auto c1 = static_cast<unsigned char>(*p++);
        const auto c2 = static_cast<unsigned char>(*p++);
        const auto c3 = static_cast<unsigned char>(*p++);
        return ((first & 0x07u) << 18) | ((c1 & 0x3Fu) << 12) | ((c2 & 0x3Fu) << 6) |
               (c3 & 0x3Fu);
    }
    return '?';
}

MyGUI::KeyCode::Enum myGuiKey(Key key) {
    switch (key) {
    case Key_Tab: return MyGUI::KeyCode::Tab;
    case Key_LeftArrow: return MyGUI::KeyCode::ArrowLeft;
    case Key_RightArrow: return MyGUI::KeyCode::ArrowRight;
    case Key_UpArrow: return MyGUI::KeyCode::ArrowUp;
    case Key_DownArrow: return MyGUI::KeyCode::ArrowDown;
    case Key_Home: return MyGUI::KeyCode::Home;
    case Key_End: return MyGUI::KeyCode::End;
    case Key_Delete: return MyGUI::KeyCode::Delete;
    case Key_Backspace: return MyGUI::KeyCode::Backspace;
    case Key_Enter: return MyGUI::KeyCode::Return;
    case Key_Escape: return MyGUI::KeyCode::Escape;
    case Key_A: return MyGUI::KeyCode::A;
    case Key_C: return MyGUI::KeyCode::C;
    case Key_D: return MyGUI::KeyCode::D;
    case Key_E: return MyGUI::KeyCode::E;
    case Key_F: return MyGUI::KeyCode::F;
    case Key_O: return MyGUI::KeyCode::O;
    case Key_P: return MyGUI::KeyCode::P;
    case Key_R: return MyGUI::KeyCode::R;
    case Key_S: return MyGUI::KeyCode::S;
    case Key_V: return MyGUI::KeyCode::V;
    case Key_W: return MyGUI::KeyCode::W;
    case Key_X: return MyGUI::KeyCode::X;
    case Key_Y: return MyGUI::KeyCode::Y;
    case Key_Z: return MyGUI::KeyCode::Z;
    case Key_F2: return MyGUI::KeyCode::F2;
    case Key_F5: return MyGUI::KeyCode::F5;
    case Key_F6: return MyGUI::KeyCode::F6;
    case Key_F7: return MyGUI::KeyCode::F7;
    default: return MyGUI::KeyCode::None;
    }
}

Key keyFromScancode(SDL_Scancode code) {
    switch (code) {
    case SDL_SCANCODE_TAB: return Key_Tab;
    case SDL_SCANCODE_LEFT: return Key_LeftArrow;
    case SDL_SCANCODE_RIGHT: return Key_RightArrow;
    case SDL_SCANCODE_UP: return Key_UpArrow;
    case SDL_SCANCODE_DOWN: return Key_DownArrow;
    case SDL_SCANCODE_HOME: return Key_Home;
    case SDL_SCANCODE_END: return Key_End;
    case SDL_SCANCODE_DELETE: return Key_Delete;
    case SDL_SCANCODE_BACKSPACE: return Key_Backspace;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return Key_Enter;
    case SDL_SCANCODE_ESCAPE: return Key_Escape;
    case SDL_SCANCODE_A: return Key_A;
    case SDL_SCANCODE_C: return Key_C;
    case SDL_SCANCODE_D: return Key_D;
    case SDL_SCANCODE_E: return Key_E;
    case SDL_SCANCODE_F: return Key_F;
    case SDL_SCANCODE_O: return Key_O;
    case SDL_SCANCODE_P: return Key_P;
    case SDL_SCANCODE_R: return Key_R;
    case SDL_SCANCODE_S: return Key_S;
    case SDL_SCANCODE_V: return Key_V;
    case SDL_SCANCODE_W: return Key_W;
    case SDL_SCANCODE_X: return Key_X;
    case SDL_SCANCODE_Y: return Key_Y;
    case SDL_SCANCODE_Z: return Key_Z;
    case SDL_SCANCODE_F2: return Key_F2;
    case SDL_SCANCODE_F5: return Key_F5;
    case SDL_SCANCODE_F6: return Key_F6;
    case SDL_SCANCODE_F7: return Key_F7;
    default: return Key_None;
    }
}

MyGUI::KeyCode::Enum keyCodeFromScancode(SDL_Scancode code) {
    switch (code) {
    case SDL_SCANCODE_LEFT: return MyGUI::KeyCode::ArrowLeft;
    case SDL_SCANCODE_RIGHT: return MyGUI::KeyCode::ArrowRight;
    case SDL_SCANCODE_UP: return MyGUI::KeyCode::ArrowUp;
    case SDL_SCANCODE_DOWN: return MyGUI::KeyCode::ArrowDown;
    case SDL_SCANCODE_RETURN: return MyGUI::KeyCode::Return;
    case SDL_SCANCODE_KP_ENTER: return MyGUI::KeyCode::NumpadEnter;
    case SDL_SCANCODE_LCTRL: return MyGUI::KeyCode::LeftControl;
    case SDL_SCANCODE_RCTRL: return MyGUI::KeyCode::RightControl;
    case SDL_SCANCODE_LSHIFT: return MyGUI::KeyCode::LeftShift;
    case SDL_SCANCODE_RSHIFT: return MyGUI::KeyCode::RightShift;
    case SDL_SCANCODE_LALT: return MyGUI::KeyCode::LeftAlt;
    case SDL_SCANCODE_RALT: return MyGUI::KeyCode::RightAlt;
    case SDL_SCANCODE_LGUI: return MyGUI::KeyCode::LeftWindows;
    case SDL_SCANCODE_RGUI: return MyGUI::KeyCode::RightWindows;
    case SDL_SCANCODE_ESCAPE: return MyGUI::KeyCode::Escape;
    case SDL_SCANCODE_TAB: return MyGUI::KeyCode::Tab;
    case SDL_SCANCODE_BACKSPACE: return MyGUI::KeyCode::Backspace;
    case SDL_SCANCODE_DELETE: return MyGUI::KeyCode::Delete;
    case SDL_SCANCODE_HOME: return MyGUI::KeyCode::Home;
    case SDL_SCANCODE_END: return MyGUI::KeyCode::End;
    case SDL_SCANCODE_PAGEUP: return MyGUI::KeyCode::PageUp;
    case SDL_SCANCODE_PAGEDOWN: return MyGUI::KeyCode::PageDown;
    case SDL_SCANCODE_A: return MyGUI::KeyCode::A;
    case SDL_SCANCODE_C: return MyGUI::KeyCode::C;
    case SDL_SCANCODE_D: return MyGUI::KeyCode::D;
    case SDL_SCANCODE_E: return MyGUI::KeyCode::E;
    case SDL_SCANCODE_F: return MyGUI::KeyCode::F;
    case SDL_SCANCODE_O: return MyGUI::KeyCode::O;
    case SDL_SCANCODE_P: return MyGUI::KeyCode::P;
    case SDL_SCANCODE_R: return MyGUI::KeyCode::R;
    case SDL_SCANCODE_S: return MyGUI::KeyCode::S;
    case SDL_SCANCODE_V: return MyGUI::KeyCode::V;
    case SDL_SCANCODE_W: return MyGUI::KeyCode::W;
    case SDL_SCANCODE_X: return MyGUI::KeyCode::X;
    case SDL_SCANCODE_Y: return MyGUI::KeyCode::Y;
    case SDL_SCANCODE_Z: return MyGUI::KeyCode::Z;
    case SDL_SCANCODE_F2: return MyGUI::KeyCode::F2;
    case SDL_SCANCODE_F5: return MyGUI::KeyCode::F5;
    case SDL_SCANCODE_F6: return MyGUI::KeyCode::F6;
    case SDL_SCANCODE_F7: return MyGUI::KeyCode::F7;
    default: return MyGUI::KeyCode::None;
    }
}

struct WidgetRecord {
    MyGUI::Widget* widget = nullptr;
    std::string key;
    std::string kind;
    std::string text;
    std::string inputValue;
    std::string inputBufferValue;
    MyGUI::Widget* parent = nullptr;
    Rect rect{};
    uint64_t id = 0;
    uint64_t lastFrame = 0;
    uint64_t clickFrame = 0;
    uint64_t doubleClickFrame = 0;
    uint64_t changedFrame = 0;
    uint64_t pressedFrame = 0;
    float hoverSince = -1.0f;
    float dragStartX = 0.0f;
    float dragStartValue = 0.0f;
    bool dragging = false;
    bool disabled = false;
    bool selected = false;
    bool internal = false;

    void onClickEvent(MyGUI::Widget* sender);
    void onDoubleClickEvent(MyGUI::Widget* sender);
    void onPressedEvent(MyGUI::Widget* sender, int, int, MyGUI::MouseButton button);
    void onEditChanged(MyGUI::EditBox* sender);
};

struct WindowRecord {
    MyGUI::Widget* root = nullptr;
    MyGUI::Widget* client = nullptr;
    MyGUI::Widget* titleBar = nullptr;
    MyGUI::TextBox* title = nullptr;
    MyGUI::Widget* closeButton = nullptr;
    MyGUI::Widget* resizeHandle = nullptr;
    std::string name;
    Rect rect{};
    DrawList drawList;
    uint64_t lastFrame = 0;
    bool* openPtr = nullptr;
    bool closeRequested = false;
    bool titleless = false;
    bool focused = false;
    bool docked = false;
    bool dragging = false;
    bool resizing = false;
    float dragStartX = 0.0f;
    float dragStartY = 0.0f;
    float startX = 0.0f;
    float startY = 0.0f;
    float startW = 0.0f;
    float startH = 0.0f;

    void onTitlePress(MyGUI::Widget*, int, int, MyGUI::MouseButton button);
    void onTitleDrag(MyGUI::Widget*, int left, int top, MyGUI::MouseButton button);
    void onTitleRelease(MyGUI::Widget*, int, int, MyGUI::MouseButton button);
    void onClose(MyGUI::Widget*);
    void onResizePress(MyGUI::Widget*, int, int, MyGUI::MouseButton button);
    void onResizeDrag(MyGUI::Widget*, int left, int top, MyGUI::MouseButton button);
    void onResizeRelease(MyGUI::Widget*, int, int, MyGUI::MouseButton button);
};

struct LayoutScope {
    MyGUI::Widget* parent = nullptr;
    WindowRecord* window = nullptr;
    Rect screen{};
    float width = 0.0f;
    float height = 0.0f;
    float cursorX = 0.0f;
    float cursorY = 0.0f;
    float lineStartX = 0.0f;
    float lineHeight = 0.0f;
    float nextWidth = 0.0f;
    float indent = 0.0f;
    float scrollX = 0.0f;
    float scrollY = 0.0f;
    float scrollMaxX = 0.0f;
    float scrollMaxY = 0.0f;
    float maxContentX = 0.0f;
    std::string scrollKey;
    bool child = false;
    bool tableCell = false;
    bool horizontalScroll = false;
    std::string id;
};

struct PopupRecord {
    MyGUI::Widget* root = nullptr;
    MyGUI::Widget* client = nullptr;
    MyGUI::TextBox* tooltipText = nullptr;
    std::string id;
    Rect rect{};
    uint64_t lastFrame = 0;
    bool open = false;
    bool modal = false;
    bool tooltip = false;
};

struct PanelGroup {
    uint32_t id = 0;
    Rect outer{};
    Rect content{};
    std::vector<std::string> names;
    std::string active;
};

struct TableState {
    LayoutScope saved;
    MyGUI::Widget* root = nullptr;
    std::string id;
    TableFlags flags = TableFlags_None;
    int columns = 0;
    int column = 0;
    int setupColumn = 0;
    int row = 0;
    int freezeColumns = 0;
    int freezeRows = 0;
    Rect bounds{};
    std::array<MyGUI::Widget*, 4> clipWidgets{};
    std::array<Rect, 4> clipBounds{};
    std::size_t currentRegionIndex = 3;
    float frozenWidth = 0.0f;
    float frozenHeight = 0.0f;
    float hostHeight = 0.0f;
    bool cellClipPushed = false;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float scrollX = 0.0f;
    float scrollY = 0.0f;
    float scrollMaxX = 0.0f;
    float scrollMaxY = 0.0f;
    float rowHeight = 26.0f;
    bool rowStarted = false;
    std::vector<float> rowHeights;
    std::vector<float> widths;
    std::vector<float> defaultWidths;
    std::vector<float> stretchWeights;
    std::vector<bool> fixedWidths;
    std::vector<std::string> headers;
};

struct ScrollState {
    float x = 0.0f;
    float y = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
};

struct FocusInput {
    WidgetRecord* record = nullptr;
    std::string scope;
    int index = 0;
    bool autoSelectAll = false;
};

struct MenuFrame {
    bool mainMenu = false;
    bool menu = false;
    bool popup = false;
    std::string id;
    std::size_t layoutDepth = 0;
    float restoreMenuBarX = 0.0f;
};

struct Context {
    std::unique_ptr<LegacyMyGuiPlatform> platform;
    IO io;
    Style style;
    Viewport viewport;
    bool headless = true;
    bool inFrame = false;
    bool disabled = false;
    int disabledDepth = 0;
    uint64_t frame = 0;
    float time = 0.0f;
    double seconds = 0.0;
    float lastFrameDt = 1.0f / 60.0f;
    int wheelAbsolute = 0;
    float pendingMouseWheel = 0.0f;
    float pendingMouseWheelH = 0.0f;
    bool mouseWheelConsumed = false;
    std::array<bool, 3> mouseDown{};
    std::array<bool, 3> mousePressed{};
    std::array<bool, 3> mouseReleased{};
    std::array<bool, 3> mouseDouble{};
    std::array<bool, 3> pendingDouble{};
    std::array<bool, 3> pendingPressed{};
    std::array<bool, 3> pendingReleased{};
    std::array<bool, Key_COUNT> keyDown{};
    std::array<bool, Key_COUNT> keyPressed{};
    std::array<bool, Key_COUNT> pendingKeyPressed{};
    std::array<bool, Key_COUNT> pendingKeyReleased{};
    Vec2 previousMouse{-1.0f, -1.0f};
    Vec2 pressPosition[3]{};
    double lastClickTime[3]{-1.0, -1.0, -1.0};
    Vec2 lastClickPosition[3]{};
    MyGUI::Widget* lastItem = nullptr;
    WidgetRecord* lastItemRecord = nullptr;
    Rect lastItemRect{};
    MyGUI::Widget* focusedRoot = nullptr;
    WindowRecord* currentWindow = nullptr;
    std::unordered_map<std::string, std::unique_ptr<WidgetRecord>> widgets;
    std::unordered_map<std::string, WindowRecord> windows;
    std::unordered_map<std::string, PopupRecord> popups;
    std::unordered_map<std::string, bool> treeState;
    std::unordered_map<std::string, bool> nextTreeState;
    std::unordered_map<std::string, ScrollState> scrollStates;
    std::unordered_map<std::string, std::vector<float>> tableColumnWidthOffsets;
    std::string resizingTable;
    int resizingTableColumn = -1;
    float resizeStartX = 0.0f;
    float resizeStartLeftWidth = 0.0f;
    float resizeStartRightWidth = 0.0f;
    std::unordered_map<std::string, int> focusInputIndices;
    std::vector<FocusInput> focusInputs;
    std::unordered_map<std::string, PanelInfo> panels;
    std::unordered_map<std::string, PanelGroup> groups;
    std::vector<LayoutScope> layouts;
    std::vector<WindowRecord*> windowStack;
    std::vector<PopupRecord*> popupStack;
    std::vector<MenuFrame> menuStack;
    std::vector<std::string> idStack;
    std::vector<std::pair<ColorRole, Color>> colorStack;
    std::vector<std::pair<StyleVar, std::variant<float, Vec2>>> styleStack;
    std::vector<float> wrapStack;
    std::vector<float> indentStack;
    std::vector<TableState> tables;
    DrawList background;
    DrawList foreground;
    WindowRecord* nextWindow = nullptr;
    Vec2 nextPos{};
    Vec2 nextSize{};
    Vec2 minSize{64, 48};
    Vec2 maxSize{10000, 10000};
    Vec2 nextPivot{};
    Cond nextPosCond = Cond_None;
    Cond nextSizeCond = Cond_None;
    bool hasNextPos = false;
    bool hasNextSize = false;
    bool hasNextConstraints = false;
    bool focusNext = false;
    bool focusInputPending = false;
    int focusInputTarget = -1;
    std::string focusInputScope;
    float nextItemWidth = 0.0f;
    float wrapLocalPosX = 0.0f;
    std::string popupPending;
    std::string openPopup;
    std::string currentPopup;
    std::string activeMenu;
    std::string activeTabByGroup[4]{"Hierarchy", "Inspector", "Scene", "Assets"};
    float menuBarX = 8.0f;
    MyGUI::Widget* menuBarRoot = nullptr;
    MyGUI::Widget* workspaceRoot = nullptr;
    Rect workspaceRect{};
    std::vector<std::string> workspacePanelNames;
    std::array<bool, 3> dragDropMouseDown{};
    std::string dragSource;
    std::string dragTarget;
    Payload payload;
    bool payloadPending = false;
    bool payloadDropped = false;
    bool payloadAccepted = false;
    uint64_t payloadFrame = 0;
    std::vector<std::string> contextIds;
    std::function<void(const std::string&)> clipboardSetter;
    std::function<std::string()> clipboardGetter;
    std::vector<std::string> filters;

    explicit Context(bool useHeadless) : headless(useHeadless) {
        styleColorsDark(&style);
        viewport.Size = io.DisplaySize;
        viewport.WorkSize = io.DisplaySize;
        platform = std::make_unique<LegacyMyGuiPlatform>(headless);
        platform->setViewSize(clampInt(io.DisplaySize.x), clampInt(io.DisplaySize.y));
        syncThemeToMyGui(false);
        if (auto* root = platform->gui().createWidgetT("Widget",
                                                        "LegacyMenuBarSkin",
                                                        MyGUI::IntCoord(0, 0, clampInt(io.DisplaySize.x), 28),
                                                        MyGUI::Align::Default,
                                                        "Main",
                                                        "LE_MenuBar")) {
            menuBarRoot = root;
            menuBarRoot->setVisible(false);
        }
    }

    LayoutScope& layout() {
        if (layouts.empty()) {
            layouts.emplace_back();
            layouts.back().width = io.DisplaySize.x;
            layouts.back().height = io.DisplaySize.y;
        }
        return layouts.back();
    }

    std::string currentPath() const {
        std::string result;
        for (const auto& part : idStack) {
            result.append(part);
            result.push_back('/');
        }
        if (currentWindow) {
            result.append(currentWindow->name);
            result.push_back('/');
        }
        for (const auto* popup : popupStack) {
            if (popup) {
                result.append("popup:");
                result.append(popup->id);
                result.push_back('/');
            }
        }
        return result;
    }

    bool modalOpen() const {
        for (const auto& pair : popups) {
            if (pair.second.open && pair.second.modal) {
                return true;
            }
        }
        return false;
    }
};

std::unique_ptr<Context> gContext;
Style gFallbackStyle;
uint64_t gFallbackTime = 0;

Context* context() {
    return gContext.get();
}

MyGUI::InputManager* inputManager() {
    if (!MyGUI::InputManager::getInstancePtr()) {
        return nullptr;
    }
    return MyGUI::InputManager::getInstancePtr();
}

bool widgetIsDescendant(MyGUI::Widget* candidate, MyGUI::Widget* ancestor) {
    if (!candidate || !ancestor) {
        return false;
    }
    for (auto* item = candidate; item; item = item->getParent()) {
        if (item == ancestor) {
            return true;
        }
    }
    return false;
}

bool focusInputRecord(Context& c, const FocusInput& input) {
    if (!input.record || !input.record->widget) return false;
    auto* edit = input.record->widget->castType<MyGUI::EditBox>(false);
    auto* manager = inputManager();
    if (!edit || !manager) return false;
    manager->setKeyFocusWidget(edit);
    if (input.autoSelectAll) {
        edit->setTextSelection(0, edit->getCaption().size());
    }
    return true;
}

bool registerFocusableInput(Context& c,
                            WidgetRecord* record,
                            bool autoSelectAll) {
    const std::string scope = c.currentPath();
    int& nextIndex = c.focusInputIndices[scope];
    const int index = nextIndex++;
    const FocusInput input{record, scope, index, autoSelectAll};
    c.focusInputs.push_back(input);
    if (c.focusInputPending && c.focusInputScope == scope && c.focusInputTarget == index) {
        c.focusInputPending = false;
        focusInputRecord(c, input);
        return true;
    }
    return false;
}

void requestKeyboardFocus(Context& c, int offset) {
    const std::string scope = c.currentPath();
    const auto found = c.focusInputIndices.find(scope);
    const int nextIndex = found == c.focusInputIndices.end() ? 0 : found->second;
    const int target = nextIndex + offset;
    c.focusInputPending = false;
    c.focusInputScope = scope;
    c.focusInputTarget = target;
    if (target < 0) return;
    if (target < nextIndex) {
        for (auto it = c.focusInputs.rbegin(); it != c.focusInputs.rend(); ++it) {
            if (it->scope == scope && it->index == target) {
                focusInputRecord(c, *it);
                return;
            }
        }
        return;
    }
    c.focusInputPending = true;
}

void restoreScrollState(Context& c, LayoutScope& layout, const std::string& key) {
    layout.scrollKey = key;
    const auto found = c.scrollStates.find(key);
    if (found == c.scrollStates.end()) {
        return;
    }
    layout.scrollX = found->second.x;
    layout.scrollY = found->second.y;
    layout.scrollMaxX = found->second.maxX;
    layout.scrollMaxY = found->second.maxY;
}

void saveScrollState(Context& c, const LayoutScope& layout) {
    if (layout.scrollKey.empty()) {
        return;
    }
    auto& state = c.scrollStates[layout.scrollKey];
    state.x = layout.scrollX;
    state.y = layout.scrollY;
    state.maxX = layout.scrollMaxX;
    state.maxY = layout.scrollMaxY;
}

void finishScrollableLayout(Context& c,
                            LayoutScope& layout,
                            bool allowHorizontal,
                            bool allowVertical) {
    layout.scrollMaxX = allowHorizontal ? std::max(0.0f, layout.maxContentX - layout.width) : 0.0f;
    layout.scrollMaxY = allowVertical
                            ? std::max(0.0f,
                                       layout.cursorY + c.style.WindowPadding.y - layout.height)
                            : 0.0f;
    layout.scrollX = std::clamp(layout.scrollX, 0.0f, layout.scrollMaxX);
    layout.scrollY = std::clamp(layout.scrollY, 0.0f, layout.scrollMaxY);

    auto* manager = inputManager();
    auto* focus = manager ? manager->getMouseFocusWidget() : nullptr;
    const Rect bounds{layout.screen.x, layout.screen.y, layout.width, layout.height};
    const bool pointerInside = bounds.contains(c.io.MousePos.x, c.io.MousePos.y);
    const bool widgetInside = layout.parent && widgetIsDescendant(focus, layout.parent);
    if (!c.mouseWheelConsumed && pointerInside && widgetInside) {
        float horizontal = c.io.MouseWheelH;
        if (c.io.KeyShift && allowHorizontal) {
            horizontal += c.io.MouseWheel;
        }
        if (allowHorizontal && horizontal != 0.0f) {
            const float old = layout.scrollX;
            layout.scrollX = std::clamp(layout.scrollX - horizontal * 30.0f,
                                        0.0f,
                                        layout.scrollMaxX);
            c.mouseWheelConsumed = layout.scrollX != old;
        }
        if (!c.mouseWheelConsumed && allowVertical && c.io.MouseWheel != 0.0f &&
            !(c.io.KeyShift && allowHorizontal)) {
            const float old = layout.scrollY;
            layout.scrollY = std::clamp(layout.scrollY - c.io.MouseWheel * 30.0f,
                                        0.0f,
                                        layout.scrollMaxY);
            c.mouseWheelConsumed = layout.scrollY != old;
        }
    }
    saveScrollState(c, layout);
}

WidgetRecord* recordForWidget(Context& c, MyGUI::Widget* widget) {
    if (!widget) {
        return nullptr;
    }
    for (auto& entry : c.widgets) {
        if (entry.second->widget == widget) {
            return entry.second.get();
        }
    }
    return nullptr;
}

uint64_t stableId(Context& c, std::string_view key) {
    uint32_t h = 2166136261u;
    const auto prefix = c.currentPath();
    h = foldHash(h, prefix);
    h = foldHash(h, key);
    return h;
}

std::string widgetKey(Context& c, const char* label, std::string_view suffix = {}) {
    std::string key = c.currentPath();
    key.append(label ? label : "");
    if (!suffix.empty()) {
        key.push_back('#');
        key.append(suffix);
    }
    return key;
}

MyGUI::Widget* createRootWidget(Context& c,
                                std::string_view type,
                                std::string_view skin,
                                const Rect& rect,
                                std::string_view layer,
                                std::string_view name) {
    if (!c.platform) {
        return nullptr;
    }
    return c.platform->gui().createWidgetT(type,
                                            skin,
                                            MyGUI::IntCoord(clampInt(rect.x),
                                                            clampInt(rect.y),
                                                            std::max(1, clampInt(rect.width)),
                                                            std::max(1, clampInt(rect.height))),
                                            MyGUI::Align::Default,
                                            layer,
                                            name);
}

void setWidgetRect(MyGUI::Widget* widget, const Rect& rect) {
    if (!widget) {
        return;
    }
    widget->setCoord(MyGUI::IntCoord(0, 0, std::max(1, clampInt(rect.width)), std::max(1, clampInt(rect.height))));
}

MyGUI::Widget* ensureRawWidget(Context& c,
                               const std::string& key,
                               std::string_view type,
                               std::string_view skin,
                               MyGUI::Widget* parent,
                               const MyGUI::IntCoord& coord,
                               std::string_view name = {}) {
    auto it = c.widgets.find(key);
    if (it != c.widgets.end() && (it->second->kind != type || it->second->parent != parent)) {
        if (it->second->widget && MyGUI::WidgetManager::getInstancePtr()) {
            MyGUI::WidgetManager::getInstance().destroyWidget(it->second->widget);
        }
        c.widgets.erase(it);
        it = c.widgets.end();
    }
    if (it == c.widgets.end()) {
        auto record = std::make_unique<WidgetRecord>();
        record->key = key;
        record->kind = std::string(type);
        record->parent = parent;
        record->id = stableId(c, key);
        const std::string widgetName = name.empty() ? "LE_" + std::to_string(record->id) : std::string(name);
        if (parent) {
            record->widget = parent->createWidgetT(type, skin, coord, MyGUI::Align::Default, widgetName);
        } else {
            record->widget = c.platform->gui().createWidgetT(type,
                                                              skin,
                                                              coord,
                                                              MyGUI::Align::Default,
                                                              "Main",
                                                              widgetName);
        }
        if (!record->widget) {
            return nullptr;
        }
        record->widget->eventMouseButtonClick +=
            MyGUI::newDelegate(record.get(), &WidgetRecord::onClickEvent);
        record->widget->eventMouseButtonDoubleClick +=
            MyGUI::newDelegate(record.get(), &WidgetRecord::onDoubleClickEvent);
        record->widget->eventMouseButtonPressed +=
            MyGUI::newDelegate(record.get(), &WidgetRecord::onPressedEvent);
        if (auto* edit = record->widget->castType<MyGUI::EditBox>(false)) {
            edit->eventEditTextChange += MyGUI::newDelegate(record.get(), &WidgetRecord::onEditChanged);
        }
        record->widget->setNeedMouseFocus(true);
        it = c.widgets.emplace(key, std::move(record)).first;
    }
    auto& record = *it->second;
    record.lastFrame = c.frame;
    record.rect = {static_cast<float>(coord.left), static_cast<float>(coord.top),
                   static_cast<float>(coord.width), static_cast<float>(coord.height)};
    record.widget->setCoord(coord);
    record.widget->setVisible(true);
    record.widget->setEnabled(!c.disabled);
    return record.widget;
}

WidgetRecord* getRecord(Context& c,
                        const std::string& key,
                        std::string_view type,
                        std::string_view skin,
                        MyGUI::Widget* parent,
                        const MyGUI::IntCoord& coord) {
    auto* widget = ensureRawWidget(c, key, type, skin, parent, coord);
    return recordForWidget(c, widget);
}

void setTextColour(MyGUI::Widget* widget, Color color) {
    if (auto* textBox = widget ? widget->castType<MyGUI::TextBox>(false) : nullptr) {
        textBox->setTextColour(color.toMyGui());
    }
}

void setCaption(MyGUI::Widget* widget, const std::string& text) {
    if (auto* textBox = widget ? widget->castType<MyGUI::TextBox>(false) : nullptr) {
        if (textBox->getCaption().asUTF8() != text) {
            textBox->setCaption(MyGUI::UString(text));
        }
    }
}

float measuredWidth(const Context& c, std::string_view text, float size = 0.0f) {
    auto* font = c.platform ? c.platform->defaultFont() : nullptr;
    return font ? font->measureWidth(text, size > 0.0f ? size : kDefaultFontSize)
                : static_cast<float>(text.size()) * 8.0f;
}

float currentFontHeight(const Context& c) {
    auto* font = c.platform ? c.platform->defaultFont() : nullptr;
    return font ? static_cast<float>(font->getDefaultHeight()) : kDefaultFontSize;
}

Rect screenRectFor(LayoutScope& layout, float x, float y, float w, float h) {
    return {layout.screen.x + x - layout.scrollX, layout.screen.y + y - layout.scrollY, w, h};
}

MyGUI::IntCoord widgetCoord(LayoutScope& layout, float x, float y, float width, float height) {
    return MyGUI::IntCoord(clampInt(x - layout.scrollX),
                           clampInt(y - layout.scrollY),
                           std::max(1, clampInt(width)),
                           std::max(1, clampInt(height)));
}

void updateTableClipRegions(Context& c, TableState& table) {
    float frozenWidth = 0.0f;
    for (int column = 0; column < table.freezeColumns; ++column) {
        frozenWidth += table.widths[static_cast<std::size_t>(column)];
    }
    table.frozenWidth = std::clamp(frozenWidth, 0.0f, table.width);

    const float contentHeight = std::accumulate(table.rowHeights.begin(),
                                                table.rowHeights.end(),
                                                0.0f);
    table.hostHeight = (table.flags & TableFlags_ScrollY)
                           ? table.height
                           : std::max(table.height, contentHeight);
    float frozenHeight = 0.0f;
    for (int row = 0; row < table.freezeRows; ++row) {
        frozenHeight += row < static_cast<int>(table.rowHeights.size())
                            ? table.rowHeights[static_cast<std::size_t>(row)]
                            : table.rowHeight;
    }
    table.frozenHeight = std::clamp(frozenHeight, 0.0f, table.hostHeight);

    const float rightWidth = table.width - table.frozenWidth;
    const float bottomHeight = table.hostHeight - table.frozenHeight;
    const Rect regions[] = {{0.0f, 0.0f, table.frozenWidth, table.frozenHeight},
                            {table.frozenWidth, 0.0f, rightWidth, table.frozenHeight},
                            {0.0f, table.frozenHeight, table.frozenWidth, bottomHeight},
                            {table.frozenWidth,
                             table.frozenHeight,
                             rightWidth,
                             bottomHeight}};
    const bool hasFrozenRegions = table.freezeColumns > 0 || table.freezeRows > 0;
    if (!hasFrozenRegions) {
        for (std::size_t i = 0; i < table.clipWidgets.size(); ++i) {
            if (table.clipWidgets[i] && table.clipWidgets[i] != table.root) {
                table.clipWidgets[i]->setVisible(false);
            }
            table.clipWidgets[i] = table.root;
            table.clipBounds[i] = regions[i];
        }
        return;
    }
    for (std::size_t i = 0; i < table.clipWidgets.size(); ++i) {
        table.clipBounds[i] = regions[i];
        auto* clip = ensureRawWidget(c,
                                     table.id + "/clip/" + std::to_string(i),
                                     "Widget",
                                     "PanelEmpty",
                                     table.root,
                                     MyGUI::IntCoord(clampInt(regions[i].x),
                                                     clampInt(regions[i].y),
                                                     std::max(1, clampInt(regions[i].width)),
                                                     std::max(1, clampInt(regions[i].height))));
        table.clipWidgets[i] = clip;
        if (clip) {
            clip->setNeedMouseFocus(false);
            clip->setInheritsPick(true);
            clip->setVisible(regions[i].width > 0.0f && regions[i].height > 0.0f);
        }
    }
}

void updateTableRowHeight(Context& c, LayoutScope& layout, float height) {
    if (!layout.tableCell || c.tables.empty()) return;
    auto& table = c.tables.back();
    if (table.row < 0) return;
    if (table.rowHeights.size() <= static_cast<std::size_t>(table.row)) {
        table.rowHeights.resize(static_cast<std::size_t>(table.row) + 1, 0.0f);
    }
    table.rowHeight = std::max(table.rowHeight, std::max(kMinItemHeight, height));
    auto& rowHeight = table.rowHeights[static_cast<std::size_t>(table.row)];
    rowHeight = std::max(rowHeight, table.rowHeight);
    updateTableClipRegions(c, table);
    if (table.cellClipPushed && layout.tableCell) {
        const auto& region = table.clipBounds[table.currentRegionIndex];
        layout.screen = {table.bounds.x + region.x,
                         table.bounds.y + region.y,
                         region.width,
                         region.height};
        auto* draw = c.currentWindow ? &c.currentWindow->drawList : &c.foreground;
        draw->popClipRect();
        draw->pushClipRect({layout.screen.x, layout.screen.y},
                           {layout.screen.x + layout.screen.width,
                            layout.screen.y + layout.screen.height},
                           true);
    }
}

float remainingWidth(const LayoutScope& layout, float padding = 0.0f, bool includeIndent = false) {
    const float right = layout.tableCell ? layout.lineStartX + layout.width : layout.width;
    return std::max(0.0f,
                    right - layout.cursorX - (includeIndent ? layout.indent : 0.0f) - padding);
}

void setLastItem(Context& c, WidgetRecord* record, const Rect& rect) {
    c.lastItemRecord = record;
    c.lastItem = record ? record->widget : nullptr;
    c.lastItemRect = rect;
    if (record) {
        record->rect = rect;
        record->lastFrame = c.frame;
        record->disabled = c.disabled;
        if (c.disabled) {
            record->widget->setEnabled(false);
        }
    }
}

WidgetRecord* item(Context& c,
                   const char* label,
                   std::string_view suffix,
                   std::string_view type,
                   std::string_view skin,
                   float width,
                   float height,
                   bool autoWidth) {
    auto& layout = c.layout();
    if (width <= 0.0f) {
        width = autoWidth ? measuredWidth(c, visibleLabel(label)) + c.style.FramePadding.x * 2.0f
                          : std::max(1.0f,
                                     remainingWidth(layout, c.style.WindowPadding.x, true));
    }
    if (layout.nextWidth > 0.0f) {
        width = layout.nextWidth;
        layout.nextWidth = 0.0f;
    }
    width = std::max(1.0f,
                     std::min(width, std::max(1.0f, remainingWidth(layout, 0.0f, true))));
    height = std::max(kMinItemHeight, height);
    const float x = layout.cursorX + layout.indent;
    const float y = layout.cursorY;
    auto* record = getRecord(c,
                             widgetKey(c, label, suffix),
                             type,
                             skin,
                             layout.parent,
                             widgetCoord(layout, x, y, width, height));
    const Rect rect = screenRectFor(layout, x, y, width, height);
    setLastItem(c, record, rect);
    updateTableRowHeight(c, layout, height);
    layout.maxContentX = std::max(layout.maxContentX, x + width);
    layout.cursorX = layout.lineStartX;
    layout.cursorY = y + height + c.style.ItemSpacing.y;
    layout.lineHeight = height;
    return record;
}

void setWidgetText(WidgetRecord* record, std::string_view value) {
    if (!record || !record->widget) {
        return;
    }
    std::string text(value);
    if (record->text != text) {
        setCaption(record->widget, text);
        record->text = std::move(text);
    }
}

void configureTextBox(Context& c, MyGUI::Widget* widget, Color color, bool disabled = false) {
    auto* textBox = widget ? widget->castType<MyGUI::TextBox>(false) : nullptr;
    if (!textBox) {
        return;
    }
    textBox->setFontName("Default");
    textBox->setFontHeight(static_cast<int>(std::lround(kDefaultFontSize)));
    textBox->setTextColour(disabled ? c.style.Colors[Col_TextDisabled].toMyGui() : color.toMyGui());
    textBox->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
}

bool hoveredWidget(Context& c, MyGUI::Widget* widget) {
    auto* manager = inputManager();
    return manager && widgetIsDescendant(manager->getMouseFocusWidget(), widget);
}

bool currentWindowHovered(Context& c) {
    return c.currentWindow && hoveredWidget(c, c.currentWindow->root);
}

bool itemHovered(Context& c) {
    return c.lastItem && hoveredWidget(c, c.lastItem);
}

bool itemClicked(Context& c, int button) {
    if (button < 0 || button >= static_cast<int>(c.mousePressed.size())) {
        return false;
    }
    return c.mousePressed[button] && itemHovered(c);
}

void advanceLine(Context& c, float height = -1.0f) {
    auto& layout = c.layout();
    if (height < 0.0f) {
        height = layout.lineHeight > 0.0f ? layout.lineHeight : frameHeight();
    }
    updateTableRowHeight(c, layout, height);
    layout.cursorX = layout.lineStartX;
    layout.cursorY += std::max(height, kMinItemHeight) + c.style.ItemSpacing.y;
    layout.lineHeight = 0.0f;
}

void finishLine(Context& c, float width, float height) {
    auto& layout = c.layout();
    updateTableRowHeight(c, layout, height);
    layout.maxContentX = std::max(layout.maxContentX,
                                  layout.cursorX + layout.indent + width);
    layout.cursorX = layout.lineStartX;
    layout.cursorY += height + c.style.ItemSpacing.y;
    layout.lineHeight = height;
}

void applyWindowRect(WindowRecord& window, const Rect& rect) {
    window.rect = rect;
    if (window.root) {
        window.root->setCoord(MyGUI::IntCoord(clampInt(rect.x),
                                              clampInt(rect.y),
                                              std::max(1, clampInt(rect.width)),
                                              std::max(1, clampInt(rect.height))));
    }
    if (window.titleBar) {
        window.titleBar->setCoord(MyGUI::IntCoord(1, 1, std::max(1, clampInt(rect.width) - 2), kTitleHeight));
    }
    if (window.title) {
        window.title->setCoord(MyGUI::IntCoord(8, 0, std::max(1, clampInt(rect.width) - 42), kTitleHeight));
    }
    if (window.closeButton) {
        window.closeButton->setCoord(MyGUI::IntCoord(std::max(0, clampInt(rect.width) - 24), 3, 20, 20));
    }
    if (window.client) {
        const int top = window.titleless ? 1 : kTitleHeight + 1;
        window.client->setCoord(MyGUI::IntCoord(1,
                                                top,
                                                std::max(1, clampInt(rect.width) - 2),
                                                std::max(1, clampInt(rect.height) - top - 1)));
    }
    if (window.resizeHandle) {
        window.resizeHandle->setCoord(MyGUI::IntCoord(std::max(0, clampInt(rect.width) - 14),
                                                      std::max(0, clampInt(rect.height) - 14),
                                                      12,
                                                      12));
    }
}

WindowRecord& ensureWindow(Context& c, const std::string& name, bool titleless, bool docked) {
    auto& record = c.windows[name];
    if (record.root) {
        return record;
    }
    record.name = name;
    record.titleless = titleless;
    record.docked = docked;
    const float defaultWidth = std::min(420.0f, std::max(240.0f, c.io.DisplaySize.x * .4f));
    const float defaultHeight = std::min(320.0f, std::max(180.0f, c.io.DisplaySize.y * .4f));
    Rect initial{std::max(12.0f, (c.io.DisplaySize.x - defaultWidth) * .5f),
                 std::max(36.0f, (c.io.DisplaySize.y - defaultHeight) * .5f),
                 defaultWidth,
                 defaultHeight};
    record.root = createRootWidget(c,
                                   "Widget",
                                   titleless ? "LegacyChildSkin" : "LegacyPanelSkin",
                                   initial,
                                   "Main",
                                   "LE_Window_" + std::to_string(hashStr(name.c_str())));
    if (!record.root) {
        return record;
    }
    record.root->setNeedMouseFocus(true);
    record.client = record.root->createWidgetT("Widget",
                                               "PanelEmpty",
                                               MyGUI::IntCoord(1,
                                                               titleless ? 1 : kTitleHeight + 1,
                                                               std::max(1, clampInt(initial.width) - 2),
                                                               std::max(1, clampInt(initial.height) - kTitleHeight - 2)),
                                               MyGUI::Align::Stretch,
                                               "LE_Client");
    record.client->setNeedMouseFocus(true);
    if (!titleless) {
        record.titleBar = record.root->createWidgetT("Widget",
                                                     "LegacyTitleActiveSkin",
                                                     MyGUI::IntCoord(1, 1, clampInt(initial.width) - 2, kTitleHeight),
                                                     MyGUI::Align::HStretch | MyGUI::Align::Top,
                                                     "LE_TitleBar");
        record.title = record.titleBar->createWidget<MyGUI::TextBox>("TextBoxSkin",
                                                                      MyGUI::IntCoord(8, 0, clampInt(initial.width) - 42, kTitleHeight),
                                                                      MyGUI::Align::HStretch | MyGUI::Align::Top,
                                                                      "LE_Caption");
        record.title->setFontName("Default");
        record.title->setFontHeight(static_cast<int>(kDefaultFontSize));
        record.title->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
        record.title->setTextColour(c.style.Colors[Col_Text].toMyGui());
        record.titleBar->eventMouseButtonPressed += MyGUI::newDelegate(&record, &WindowRecord::onTitlePress);
        record.titleBar->eventMouseDrag += MyGUI::newDelegate(&record, &WindowRecord::onTitleDrag);
        record.titleBar->eventMouseButtonReleased += MyGUI::newDelegate(&record, &WindowRecord::onTitleRelease);
        record.closeButton = record.root->createWidgetT("Button",
                                                        "LegacyCloseSkin",
                                                        MyGUI::IntCoord(clampInt(initial.width) - 24, 3, 20, 20),
                                                        MyGUI::Align::Right | MyGUI::Align::Top,
                                                        "LE_Close");
        record.closeButton->eventMouseButtonClick += MyGUI::newDelegate(&record, &WindowRecord::onClose);
        if (!docked) {
            record.resizeHandle = record.root->createWidgetT("Widget",
                                                              "LegacySolidSkin",
                                                              MyGUI::IntCoord(clampInt(initial.width) - 14,
                                                                              clampInt(initial.height) - 14,
                                                                              12,
                                                                              12),
                                                              MyGUI::Align::Right | MyGUI::Align::Bottom,
                                                              "LE_Resize");
            record.resizeHandle->eventMouseButtonPressed +=
                MyGUI::newDelegate(&record, &WindowRecord::onResizePress);
            record.resizeHandle->eventMouseDrag += MyGUI::newDelegate(&record, &WindowRecord::onResizeDrag);
            record.resizeHandle->eventMouseButtonReleased +=
                MyGUI::newDelegate(&record, &WindowRecord::onResizeRelease);
        }
    }
    record.drawList.reset(c.io.DisplaySize.x, c.io.DisplaySize.y, c.platform->defaultFont(),
                          c.platform->renderManager().whiteTexture());
    applyWindowRect(record, initial);
    return record;
}

void pushLayout(Context& c, MyGUI::Widget* parent, WindowRecord* window, Rect rect, std::string id, float paddingX, float paddingY) {
    LayoutScope scope;
    scope.parent = parent;
    scope.window = window;
    scope.screen = rect;
    scope.width = std::max(1.0f, rect.width);
    scope.height = std::max(1.0f, rect.height);
    scope.cursorX = paddingX;
    scope.cursorY = paddingY;
    scope.lineStartX = paddingX;
    scope.id = std::move(id);
    c.layouts.push_back(std::move(scope));
}

void popLayout(Context& c) {
    if (!c.layouts.empty()) {
        auto& leaving = c.layouts.back();
        if (leaving.child && c.layouts.size() > 1) {
            auto& parent = c.layouts[c.layouts.size() - 2];
            parent.cursorX = parent.lineStartX;
            parent.cursorY += leaving.height + c.style.ItemSpacing.y;
            parent.lineHeight = leaving.height;
        }
        c.layouts.pop_back();
    }
}

bool panelOpenByName(const Context& c, const std::string& name) {
    const auto panelIt = c.panels.find(name);
    if (panelIt != c.panels.end()) {
        return panelIt->second.open;
    }
    if (name == "Hierarchy") return true;
    if (name == "Inspector") return true;
    if (name == "Scene") return true;
    if (name == "Game") return true;
    if (name == "Assets") return true;
    if (name == "Script Editor") return true;
    if (name == "Console") return true;
    if (name == "Prefab Window") return c.windows.find(name) != c.windows.end();
    return true;
}

PanelInfo* panelForName(Context& c, const std::string& name) {
    auto it = c.panels.find(name);
    return it == c.panels.end() ? nullptr : &it->second;
}

void closePopup(Context& c, const std::string& id) {
    auto it = c.popups.find(id);
    if (it != c.popups.end()) {
        it->second.open = false;
        if (it->second.root) {
            it->second.root->setVisible(false);
        }
    }
    if (c.openPopup == id) {
        c.openPopup.clear();
    }
    if (c.currentPopup == id) {
        c.currentPopup.clear();
    }
}

PopupRecord& ensurePopup(Context& c, const std::string& id, bool modal, bool tooltip) {
    auto& record = c.popups[id];
    record.id = id;
    record.modal = modal;
    record.tooltip = tooltip;
    record.open = true;
    record.lastFrame = c.frame;
    if (!record.root) {
        Rect rect{c.io.MousePos.x + 10.0f, c.io.MousePos.y + 12.0f, 260.0f, 240.0f};
        if (modal) {
            rect.x = std::max(10.0f, (c.io.DisplaySize.x - rect.width) * .5f);
            rect.y = std::max(10.0f, (c.io.DisplaySize.y - rect.height) * .5f);
        }
        if (tooltip) {
            rect.width = std::min(320.0f, c.io.DisplaySize.x * .4f);
            rect.height = 46.0f;
            rect.x = std::clamp(c.io.MousePos.x + 14.0f, 4.0f,
                                std::max(4.0f, c.io.DisplaySize.x - rect.width - 4.0f));
            rect.y = std::clamp(c.io.MousePos.y + 18.0f, 4.0f,
                                std::max(4.0f, c.io.DisplaySize.y - rect.height - 4.0f));
        }
        record.rect = rect;
        const auto layer = (modal || tooltip) ? "ToolTip" : "Popup";
        record.root = createRootWidget(c,
                                       "Widget",
                                       tooltip ? "LegacyPopupSkin" : "LegacyPopupSkin",
                                       rect,
                                       layer,
                                       "LE_Popup_" + std::to_string(hashStr(id.c_str())));
        if (record.root) {
            record.client = record.root->createWidgetT("Widget",
                                                       "PanelEmpty",
                                                       MyGUI::IntCoord(4, 4, clampInt(rect.width) - 8, clampInt(rect.height) - 8),
                                                       MyGUI::Align::Stretch,
                                                       "LE_PopupClient");
        }
    }
    if (record.root) {
        record.root->setVisible(true);
        record.root->setCoord(MyGUI::IntCoord(clampInt(record.rect.x),
                                              clampInt(record.rect.y),
                                              std::max(1, clampInt(record.rect.width)),
                                              std::max(1, clampInt(record.rect.height))));
    }
    return record;
}

bool beginPopupInternal(Context& c, const std::string& id, bool modal, bool tooltip) {
    auto it = c.popups.find(id);
    if (it == c.popups.end() || !it->second.open) {
        if (c.openPopup != id) {
            return false;
        }
        auto& created = ensurePopup(c, id, modal, tooltip);
        it = c.popups.find(id);
        if (it == c.popups.end()) {
            return false;
        }
        (void)created;
    }
    auto& record = ensurePopup(c, id, modal, tooltip);
    if (!record.root || !record.client) {
        return false;
    }
    if (modal && !c.popupStack.empty() && !c.popupStack.back()->modal) {
        return false;
    }
    c.currentPopup = id;
    c.popupStack.push_back(&record);
    pushLayout(c,
               record.client,
               c.currentWindow,
               {record.rect.x + 8, record.rect.y + 8, record.rect.width - 16, record.rect.height - 16},
               "popup/" + id,
               0,
               0);
    return true;
}

void drawDockTab(Context& c, PanelGroup& group, const std::string& name, int column, int visibleTabs) {
    if (!c.workspaceRoot || !panelOpenByName(c, name)) {
        return;
    }
    const float each = std::max(74.0f, group.outer.width / std::max(1, visibleTabs));
    const float localX = group.outer.x + column * each;
    const float width = std::min(each,
                                 std::max(1.0f, group.outer.x + group.outer.width - localX));
    const float x = c.workspaceRect.x + localX;
    const float y = c.workspaceRect.y + group.outer.y;
    const std::string id = "__dock_tab/" + name;
    auto* button = ensureRawWidget(c,
                                   id,
                                   "Button",
                                   "LegacyTabSkin",
                                   nullptr,
                                   MyGUI::IntCoord(clampInt(x), clampInt(y), clampInt(width), kDockTabHeight));
    if (auto* record = recordForWidget(c, button)) {
        record->selected = (group.active == name);
        setCaption(button, name);
        configureTextBox(c, button, c.style.Colors[Col_Text], false);
        if (auto* nativeButton = button->castType<MyGUI::Button>(false)) {
            nativeButton->setStateSelected(record->selected);
        }
        record->rect = {x, y, width, static_cast<float>(kDockTabHeight)};
        record->lastFrame = c.frame;
        if (record->clickFrame == c.frame) {
            group.active = name;
        }
    }
}

void makeDockGroup(Context& c,
                   PanelGroup& group,
                   uint32_t id,
                   Rect outer,
                   std::vector<std::string> names,
                   std::string active) {
    group.id = id;
    group.outer = outer;
    group.content = {outer.x, outer.y + kDockTabHeight, outer.width, std::max(1.0f, outer.height - kDockTabHeight)};
    group.names = std::move(names);
    group.active = std::move(active);
    int visible = 0;
    for (const auto& name : group.names) {
        if (panelOpenByName(c, name)) {
            ++visible;
        }
    }
    if (visible > 0) {
        int col = 0;
        for (const auto& name : group.names) {
            if (!panelOpenByName(c, name)) {
                continue;
            }
            drawDockTab(c, group, name, col++, visible);
        }
    }
    if (!panelOpenByName(c, group.active)) {
        for (const auto& name : group.names) {
            if (panelOpenByName(c, name)) {
                group.active = name;
                break;
            }
        }
    }
    for (const auto& name : group.names) {
        auto& panel = c.panels[name];
        panel.name = name;
        panel.rect = group.content;
        panel.dockId = id;
        panel.dockTabs = group.names;
        panel.activeDockTab = group.active;
        panel.visible = (group.active == name) && panelOpenByName(c, name);
        panel.tabId = hashStr(name.c_str());
    }
}

void setPanelOpenFlags(Context& c, bool* openArray, int panelCount) {
    const std::pair<const char*, int> indexes[] = {{"Hierarchy", 0},
                                                   {"Inspector", 1},
                                                   {"Scene", 2},
                                                   {"Script Editor", 3},
                                                   {"Console", 4},
                                                   {"Assets", 5},
                                                   {"Game", 6}};
    for (const auto& entry : indexes) {
        auto& p = c.panels[entry.first];
        p.name = entry.first;
        p.open = entry.second < panelCount && openArray ? openArray[entry.second] : true;
    }
    auto& prefab = c.panels["Prefab Window"];
    prefab.name = "Prefab Window";
    prefab.open = c.windows.find("Prefab Window") != c.windows.end();
}

WindowRecord* currentWindowRecord(Context& c) {
    return c.currentWindow;
}

void addTableCellLayout(Context& c, TableState& table, int column) {
    auto& layout = c.layout();
    if (table.rowHeights.size() <= static_cast<std::size_t>(table.row)) {
        table.rowHeights.resize(static_cast<std::size_t>(table.row) + 1, table.rowHeight);
    }
    updateTableClipRegions(c, table);
    const float cellX = std::accumulate(table.widths.begin(),
                                        table.widths.begin() + column,
                                        0.0f) -
                        (column >= table.freezeColumns ? table.scrollX : 0.0f);
    const float rowOffset = std::accumulate(table.rowHeights.begin(),
                                            table.rowHeights.begin() + table.row,
                                            0.0f);
    const float rowY = rowOffset -
                       (table.row >= table.freezeRows ? table.scrollY : 0.0f);
    const bool frozenColumn = column < table.freezeColumns;
    const bool frozenRow = table.row < table.freezeRows;
    const std::size_t regionIndex = frozenRow ? (frozenColumn ? 0u : 1u)
                                               : (frozenColumn ? 2u : 3u);
    table.currentRegionIndex = regionIndex;
    const auto& region = table.clipBounds[regionIndex];
    const float regionX = region.x;
    const float regionY = region.y;
    layout.parent = table.clipWidgets[regionIndex] ? table.clipWidgets[regionIndex] : table.root;
    layout.screen = {table.bounds.x + region.x,
                     table.bounds.y + region.y,
                     region.width,
                     region.height};
    layout.cursorX = cellX - regionX;
    layout.cursorY = rowY - regionY;
    layout.lineStartX = layout.cursorX;
    layout.width = table.widths[static_cast<std::size_t>(column)];
    layout.height = table.rowHeight;
    layout.lineHeight = table.rowHeight;
    layout.tableCell = true;
    layout.maxContentX = std::max(layout.maxContentX, cellX + layout.width);

    auto* draw = c.currentWindow ? &c.currentWindow->drawList : &c.foreground;
    if (table.cellClipPushed) draw->popClipRect();
    const Rect cellClip = layout.screen;
    draw->pushClipRect({cellClip.x, cellClip.y},
                       {cellClip.x + cellClip.width, cellClip.y + cellClip.height},
                       true);
    table.cellClipPushed = true;

    if ((table.flags & TableFlags_Borders) && column == 0 && table.row > 0) {
        draw->popClipRect();
        draw->addLine({table.bounds.x, table.bounds.y + rowY},
                      {table.bounds.x + table.width, table.bounds.y + rowY},
                      colorU32(Col_Border));
        draw->pushClipRect({cellClip.x, cellClip.y},
                           {cellClip.x + cellClip.width, cellClip.y + cellClip.height},
                           true);
    }
    if ((table.flags & (TableFlags_BordersInnerV | TableFlags_Borders)) && column > 0) {
        const float borderX = table.bounds.x + cellX;
        draw->pushClipRect({table.bounds.x, table.bounds.y},
                           {table.bounds.x + table.width,
                            table.bounds.y + table.hostHeight},
                           true);
        draw->addLine({borderX, table.bounds.y + rowY},
                      {borderX, table.bounds.y + rowY + table.rowHeight},
                      colorU32(Col_Border));
        draw->popClipRect();
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Native retained widget event handlers
// ---------------------------------------------------------------------------

void WidgetRecord::onClickEvent(MyGUI::Widget*) {
    if (auto* c = context()) {
        clickFrame = c->frame + 1;
    }
}

void WidgetRecord::onDoubleClickEvent(MyGUI::Widget*) {
    if (auto* c = context()) {
        doubleClickFrame = c->frame + 1;
    }
}

void WidgetRecord::onPressedEvent(MyGUI::Widget*, int, int, MyGUI::MouseButton button) {
    if (auto* c = context()) {
        if (button == MyGUI::MouseButton::Left || button == MyGUI::MouseButton::Right ||
            button == MyGUI::MouseButton::Middle) {
            pressedFrame = c->frame + 1;
        }
    }
}

void WidgetRecord::onEditChanged(MyGUI::EditBox* sender) {
    if (auto* c = context()) {
        changedFrame = c->frame + 1;
        if (sender) {
            inputValue = sender->getCaption().asUTF8();
        }
    }
}

void WindowRecord::onTitlePress(MyGUI::Widget*, int, int, MyGUI::MouseButton button) {
    if (button != MyGUI::MouseButton::Left || docked) {
        return;
    }
    if (auto* c = context()) {
        dragging = true;
        const auto p = c->io.MousePos;
        dragStartX = p.x;
        dragStartY = p.y;
        startX = rect.x;
        startY = rect.y;
        c->focusedRoot = root;
    }
}

void WindowRecord::onTitleDrag(MyGUI::Widget*, int, int, MyGUI::MouseButton button) {
    if (button != MyGUI::MouseButton::Left || !dragging) {
        return;
    }
    if (auto* c = context()) {
        auto next = rect;
        next.x = std::clamp(startX + c->io.MousePos.x - dragStartX, 0.0f,
                            std::max(0.0f, c->io.DisplaySize.x - next.width));
        next.y = std::clamp(startY + c->io.MousePos.y - dragStartY, 0.0f,
                            std::max(0.0f, c->io.DisplaySize.y - next.height));
        applyWindowRect(*this, next);
    }
}

void WindowRecord::onTitleRelease(MyGUI::Widget*, int, int, MyGUI::MouseButton button) {
    if (button == MyGUI::MouseButton::Left) {
        dragging = false;
    }
}

void WindowRecord::onClose(MyGUI::Widget*) {
    closeRequested = true;
    if (openPtr) {
        *openPtr = false;
    }
}

void WindowRecord::onResizePress(MyGUI::Widget*, int, int, MyGUI::MouseButton button) {
    if (button != MyGUI::MouseButton::Left) {
        return;
    }
    if (auto* c = context()) {
        resizing = true;
        dragStartX = c->io.MousePos.x;
        dragStartY = c->io.MousePos.y;
        startW = rect.width;
        startH = rect.height;
    }
}

void WindowRecord::onResizeDrag(MyGUI::Widget*, int, int, MyGUI::MouseButton button) {
    if (button != MyGUI::MouseButton::Left || !resizing) {
        return;
    }
    if (auto* c = context()) {
        auto next = rect;
        next.width = std::clamp(startW + c->io.MousePos.x - dragStartX, 140.0f,
                                std::max(140.0f, c->io.DisplaySize.x - next.x));
        next.height = std::clamp(startH + c->io.MousePos.y - dragStartY, 90.0f,
                                 std::max(90.0f, c->io.DisplaySize.y - next.y));
        applyWindowRect(*this, next);
    }
}

void WindowRecord::onResizeRelease(MyGUI::Widget*, int, int, MyGUI::MouseButton button) {
    if (button == MyGUI::MouseButton::Left) {
        resizing = false;
    }
}

// ---------------------------------------------------------------------------
// InputText callback helpers
// ---------------------------------------------------------------------------

void InputTextCallbackData::deleteChars(int pos, int count) {
    if (!buf || count <= 0 || pos < 0 || static_cast<std::size_t>(pos) >= bufSize) {
        return;
    }
    const int length = std::max(0, bufTextLen);
    const int first = std::clamp(pos, 0, length);
    const int amount = std::clamp(count, 0, length - first);
    std::memmove(buf + first, buf + first + amount, static_cast<std::size_t>(length - first - amount + 1));
    bufTextLen = length - amount;
    if (cursorPos > first) {
        cursorPos = std::max(first, cursorPos - amount);
    }
}

void InputTextCallbackData::insertChars(int pos, const char* text) {
    if (!buf || !text || bufSize == 0) {
        return;
    }
    const int length = static_cast<int>(std::strlen(text));
    const int at = std::clamp(pos, 0, bufTextLen);
    const int room = static_cast<int>(bufSize - static_cast<std::size_t>(bufTextLen) - 1);
    const int amount = std::min(length, room);
    if (amount <= 0) {
        return;
    }
    std::memmove(buf + at + amount, buf + at, static_cast<std::size_t>(bufTextLen - at + 1));
    std::memcpy(buf + at, text, static_cast<std::size_t>(amount));
    bufTextLen += amount;
    cursorPos = at + amount;
}

// ---------------------------------------------------------------------------
// DrawList -> native OpenGL overlay batch rendered by LegacyMyGuiRenderManager
// ---------------------------------------------------------------------------

void DrawList::reset(float viewW, float viewH, LegacyMergedFont* font, MyGUI::ITexture* whiteTex) {
    viewW_ = std::max(1.0f, viewW);
    viewH_ = std::max(1.0f, viewH);
    font_ = font;
    whiteTex_ = whiteTex;
    clipStack_.clear();
    path_.clear();
    vertices_.clear();
    commands_.clear();
}

MyGUI::Vertex DrawList::makeVertex(float x, float y, float u, float v, uint32_t col) const {
    MyGUI::Vertex vertex{};
    vertex.x = (x / viewW_) * 2.0f - 1.0f;
    vertex.y = 1.0f - (y / viewH_) * 2.0f;
    vertex.z = 0.0f;
    vertex.u = u;
    vertex.v = v;
    vertex.colour = col;
    return vertex;
}

void DrawList::pushTri(const MyGUI::Vertex& a,
                       const MyGUI::Vertex& b,
                       const MyGUI::Vertex& c,
                       MyGUI::ITexture* texture,
                       unsigned extGlTex,
                       bool flipV) {
    if (!commands_.empty()) {
        auto& last = commands_.back();
        const DrawClipRect clip = clipStack_.empty() ? DrawClipRect{} : clipStack_.back();
        const bool hasClip = !clipStack_.empty();
        const bool sameClip = last.hasClip == hasClip &&
                              (!hasClip || (last.clip.minX == clip.minX && last.clip.minY == clip.minY &&
                                            last.clip.maxX == clip.maxX && last.clip.maxY == clip.maxY));
        if (last.texture == texture && last.externalGlTexture == extGlTex && last.flipV == flipV && sameClip &&
            last.vertexOffset + last.vertexCount == vertices_.size()) {
            vertices_.push_back(a);
            vertices_.push_back(b);
            vertices_.push_back(c);
            last.vertexCount += 3;
            return;
        }
    }
    OverlayDrawCmd command;
    command.texture = texture;
    command.externalGlTexture = extGlTex;
    command.flipV = flipV;
    command.vertexOffset = vertices_.size();
    command.vertexCount = 3;
    if (!clipStack_.empty()) {
        command.clip = clipStack_.back();
        command.hasClip = true;
    }
    commands_.push_back(command);
    vertices_.push_back(a);
    vertices_.push_back(b);
    vertices_.push_back(c);
}

void DrawList::pushQuadRaw(Vec2 a,
                           Vec2 b,
                           Vec2 uv0,
                           Vec2 uv1,
                           uint32_t col,
                           MyGUI::ITexture* texture,
                           unsigned extGlTex,
                           bool flipV) {
    const auto topLeft = makeVertex(a.x, a.y, uv0.x, uv0.y, col);
    const auto topRight = makeVertex(b.x, a.y, uv1.x, uv0.y, col);
    const auto bottomLeft = makeVertex(a.x, b.y, uv0.x, uv1.y, col);
    const auto bottomRight = makeVertex(b.x, b.y, uv1.x, uv1.y, col);
    pushTri(topLeft, topRight, bottomLeft, texture, extGlTex, flipV);
    pushTri(bottomLeft, topRight, bottomRight, texture, extGlTex, flipV);
}

void DrawList::pushClipRect(Vec2 minPt, Vec2 maxPt, bool intersect) {
    DrawClipRect clip{minPt.x, minPt.y, maxPt.x, maxPt.y};
    if (intersect && !clipStack_.empty()) {
        const auto& parent = clipStack_.back();
        clip.minX = std::max(clip.minX, parent.minX);
        clip.minY = std::max(clip.minY, parent.minY);
        clip.maxX = std::min(clip.maxX, parent.maxX);
        clip.maxY = std::min(clip.maxY, parent.maxY);
    }
    clipStack_.push_back(clip);
}

void DrawList::popClipRect() {
    if (!clipStack_.empty()) {
        clipStack_.pop_back();
    }
}

Vec2 DrawList::clipRectMin() const {
    if (clipStack_.empty()) return {0.0f, 0.0f};
    return {clipStack_.back().minX, clipStack_.back().minY};
}

Vec2 DrawList::clipRectMax() const {
    if (clipStack_.empty()) return {viewW_, viewH_};
    return {clipStack_.back().maxX, clipStack_.back().maxY};
}

void DrawList::addLine(Vec2 a, Vec2 b, uint32_t col, float thickness) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.001f) {
        return;
    }
    const float half = std::max(0.5f, thickness * 0.5f);
    const float ox = -dy / length * half;
    const float oy = dx / length * half;
    const auto v0 = makeVertex(a.x + ox, a.y + oy, 0.5f, 0.5f, col);
    const auto v1 = makeVertex(b.x + ox, b.y + oy, 0.5f, 0.5f, col);
    const auto v2 = makeVertex(a.x - ox, a.y - oy, 0.5f, 0.5f, col);
    const auto v3 = makeVertex(b.x - ox, b.y - oy, 0.5f, 0.5f, col);
    pushTri(v0, v1, v2, whiteTex_);
    pushTri(v2, v1, v3, whiteTex_);
}

void DrawList::addRect(Vec2 a, Vec2 b, uint32_t col, float rounding, float thickness) {
    const float radius = std::max(0.0f,
                                  std::min({rounding, (b.x - a.x) * 0.5f,
                                            (b.y - a.y) * 0.5f}));
    if (radius <= 0.0f) {
        addLine(a, {b.x, a.y}, col, thickness);
        addLine({b.x, a.y}, b, col, thickness);
        addLine(b, {a.x, b.y}, col, thickness);
        addLine({a.x, b.y}, a, col, thickness);
        return;
    }

    const Vec2 centers[] = {{a.x + radius, a.y + radius},
                            {b.x - radius, a.y + radius},
                            {b.x - radius, b.y - radius},
                            {a.x + radius, b.y - radius}};
    const float starts[] = {kPi, -kPi * 0.5f, 0.0f, kPi * 0.5f};
    constexpr int segmentsPerCorner = 4;
    Vec2 first{};
    Vec2 previous{};
    for (int corner = 0; corner < 4; ++corner) {
        const float start = starts[corner];
        const Vec2 arcStart{centers[corner].x + std::cos(start) * radius,
                            centers[corner].y + std::sin(start) * radius};
        if (corner == 0) {
            first = arcStart;
        } else {
            addLine(previous, arcStart, col, thickness);
        }
        previous = arcStart;
        for (int segment = 1; segment <= segmentsPerCorner; ++segment) {
            const float angle = start + (kPi * 0.5f) * segment / segmentsPerCorner;
            const Vec2 point{centers[corner].x + std::cos(angle) * radius,
                             centers[corner].y + std::sin(angle) * radius};
            addLine(previous, point, col, thickness);
            previous = point;
        }
    }
    addLine(previous, first, col, thickness);
}

void DrawList::addRectFilled(Vec2 a, Vec2 b, uint32_t col, float rounding) {
    if (rounding <= 0.0f || whiteTex_ == nullptr) {
        pushQuadRaw(a, b, {0.5f, 0.5f}, {0.5f, 0.5f}, col, whiteTex_);
        return;
    }
    const float r = std::min({rounding, (b.x - a.x) * .5f, (b.y - a.y) * .5f});
    const Vec2 centers[] = {{a.x + r, a.y + r}, {b.x - r, a.y + r},
                            {b.x - r, b.y - r}, {a.x + r, b.y - r}};
    const float start[] = {kPi, -kPi * .5f, 0.0f, kPi * .5f};
    std::vector<Vec2> perimeter;
    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i <= 4; ++i) {
            const float angle = start[corner] + (kPi * .5f) * static_cast<float>(i) / 4.0f;
            perimeter.push_back({centers[corner].x + std::cos(angle) * r,
                                 centers[corner].y + std::sin(angle) * r});
        }
    }
    const Vec2 center{(a.x + b.x) * .5f, (a.y + b.y) * .5f};
    for (std::size_t i = 0; i < perimeter.size(); ++i) {
        const auto p0 = makeVertex(center.x, center.y, .5f, .5f, col);
        const auto p1 = makeVertex(perimeter[i].x, perimeter[i].y, .5f, .5f, col);
        const auto& next = perimeter[(i + 1) % perimeter.size()];
        const auto p2 = makeVertex(next.x, next.y, .5f, .5f, col);
        pushTri(p0, p1, p2, whiteTex_);
    }
}

void DrawList::addQuad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, uint32_t col, float thickness) {
    addLine(a, b, col, thickness);
    addLine(b, c, col, thickness);
    addLine(c, d, col, thickness);
    addLine(d, a, col, thickness);
}

void DrawList::addQuadFilled(Vec2 a, Vec2 b, Vec2 c, Vec2 d, uint32_t col) {
    auto v0 = makeVertex(a.x, a.y, .5f, .5f, col);
    auto v1 = makeVertex(b.x, b.y, .5f, .5f, col);
    auto v2 = makeVertex(c.x, c.y, .5f, .5f, col);
    auto v3 = makeVertex(d.x, d.y, .5f, .5f, col);
    pushTri(v0, v1, v2, whiteTex_);
    pushTri(v0, v2, v3, whiteTex_);
}

void DrawList::addTriangle(Vec2 a, Vec2 b, Vec2 c, uint32_t col, float thickness) {
    addLine(a, b, col, thickness);
    addLine(b, c, col, thickness);
    addLine(c, a, col, thickness);
}

void DrawList::addTriangleFilled(Vec2 a, Vec2 b, Vec2 c, uint32_t col) {
    pushTri(makeVertex(a.x, a.y, .5f, .5f, col),
            makeVertex(b.x, b.y, .5f, .5f, col),
            makeVertex(c.x, c.y, .5f, .5f, col),
            whiteTex_);
}

void DrawList::addCircle(Vec2 center, float radius, uint32_t col, int segments, float thickness) {
    const int count = std::max(8, segments > 0 ? segments : static_cast<int>(radius * .75f));
    Vec2 previous{center.x + radius, center.y};
    for (int i = 1; i <= count; ++i) {
        const float angle = static_cast<float>(i) * 2.0f * kPi / count;
        Vec2 current{center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
        addLine(previous, current, col, thickness);
        previous = current;
    }
}

void DrawList::addCircleFilled(Vec2 center, float radius, uint32_t col, int segments) {
    const int count = std::max(8, segments > 0 ? segments : static_cast<int>(radius * .75f));
    Vec2 previous{center.x + radius, center.y};
    for (int i = 1; i <= count; ++i) {
        const float angle = static_cast<float>(i) * 2.0f * kPi / count;
        Vec2 current{center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
        addTriangleFilled(center, previous, current, col);
        previous = current;
    }
}

void DrawList::pathArcTo(Vec2 center, float radius, float aMin, float aMax, int segments) {
    const int count = std::max(2, segments);
    for (int i = 0; i <= count; ++i) {
        const float t = static_cast<float>(i) / count;
        const float angle = aMin + (aMax - aMin) * t;
        path_.push_back({center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius});
    }
}

void DrawList::pathStroke(uint32_t col, bool closed, float thickness) {
    if (path_.size() >= 2) {
        for (std::size_t i = 1; i < path_.size(); ++i) {
            addLine(path_[i - 1], path_[i], col, thickness);
        }
        if (closed) {
            addLine(path_.back(), path_.front(), col, thickness);
        }
    }
    path_.clear();
}

void DrawList::addText(Vec2 pos, uint32_t col, const char* textBegin, const char* textEnd) {
    addText(font_ ? static_cast<float>(font_->getDefaultHeight()) : kDefaultFontSize,
            pos,
            col,
            textBegin,
            textEnd);
}

void DrawList::addText(LegacyMergedFont* font,
                       float size,
                       Vec2 pos,
                       uint32_t col,
                       const char* textBegin,
                       const char* textEnd) {
    auto* previous = font_;
    font_ = font ? font : previous;
    addText(size, pos, col, textBegin, textEnd);
    font_ = previous;
}

void DrawList::addText(float size,
                       Vec2 pos,
                       uint32_t col,
                       const char* textBegin,
                       const char* textEnd) {
    if (!font_ || !textBegin) {
        return;
    }
    if (!textEnd) {
        textEnd = textBegin + std::strlen(textBegin);
    }
    const float scale = size / std::max(1.0f, static_cast<float>(font_->getDefaultHeight()));
    const float lineHeight = size;
    float x = pos.x;
    float y = pos.y;
    const char* it = textBegin;
    while (it < textEnd) {
        const uint32_t cp = utf8Codepoint(it, textEnd);
        if (cp == '\n') {
            x = pos.x;
            y += lineHeight;
            continue;
        }
        const auto* glyph = font_->getGlyphInfo(cp);
        if (!glyph) {
            continue;
        }
        if (glyph->width > 0.0f && glyph->height > 0.0f) {
            const Vec2 a{x + glyph->bearingX * scale, y + glyph->bearingY * scale};
            const Vec2 b{a.x + glyph->width * scale, a.y + glyph->height * scale};
            pushQuadRaw(a,
                        b,
                        {glyph->uvRect.left, glyph->uvRect.top},
                        {glyph->uvRect.right, glyph->uvRect.bottom},
                        col,
                        font_->getTextureFont());
        }
        x += glyph->advance * scale;
    }
}

void DrawList::addImage(unsigned glTexture, Vec2 a, Vec2 b, Vec2 uv0, Vec2 uv1, uint32_t col) {
    pushQuadRaw(a, b, uv0, uv1, col, nullptr, glTexture, false);
}

OverlayDrawBatch DrawList::buildBatch(int layerOrder) const {
    OverlayDrawBatch batch;
    batch.layerOrder = layerOrder;
    batch.vertices = vertices_;
    batch.commands = commands_;
    return batch;
}

// ---------------------------------------------------------------------------
// Context & platform lifecycle / SDL3 input
// ---------------------------------------------------------------------------

bool createContext(bool headless) {
    destroyContext();
    try {
        gContext = std::make_unique<Context>(headless);
        return true;
    } catch (const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MyGUI initialization failed: %s", error.what());
    } catch (...) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MyGUI initialization failed with an unknown error");
    }
    gContext.reset();
    return false;
}

bool initGl(SDL_Window* window) {
    if (!gContext || !gContext->platform || !gContext->platform->initGl(window)) {
        return false;
    }
    gContext->headless = false;
    return true;
}

void destroyContext() {
    if (gContext) {
        gContext->inFrame = false;
        gContext->layouts.clear();
        gContext->windowStack.clear();
        gContext->popupStack.clear();
        gContext->widgets.clear();
        gContext->windows.clear();
        gContext->popups.clear();
        gContext.reset();
    }
}

bool hasContext() {
    return static_cast<bool>(gContext);
}

LegacyMyGuiPlatform* platform() {
    return gContext ? gContext->platform.get() : nullptr;
}

MyGUI::Gui* myGui() {
    return gContext && gContext->platform ? &gContext->platform->gui() : nullptr;
}

bool processSdlEvent(const SDL_Event& event) {
    if (!gContext || !gContext->platform) {
        return false;
    }
    auto& c = *gContext;
    auto* input = inputManager();
    if (!input) {
        return false;
    }
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION: {
        const auto x = static_cast<int>(std::lround(event.motion.x));
        const auto y = static_cast<int>(std::lround(event.motion.y));
        c.io.MousePos = {event.motion.x, event.motion.y};
        input->injectMouseMove(x, y, c.wheelAbsolute);
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const int button = event.button.button == SDL_BUTTON_RIGHT ? MouseButton_Right
                           : event.button.button == SDL_BUTTON_MIDDLE ? MouseButton_Middle
                                                                      : MouseButton_Left;
        const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        const float x = event.button.x;
        const float y = event.button.y;
        c.io.MousePos = {x, y};
        if (button >= 0 && button < static_cast<int>(c.mouseDown.size())) {
            if (down) {
                c.pendingPressed[button] = true;
                c.mouseDown[button] = true;
                c.pressPosition[button] = {x, y};
                const double now = c.seconds + c.time;
                const float dx = x - c.lastClickPosition[button].x;
                const float dy = y - c.lastClickPosition[button].y;
                c.pendingDouble[button] = c.lastClickTime[button] >= 0.0 &&
                                           now - c.lastClickTime[button] <= 0.30 &&
                                           dx * dx + dy * dy <= 36.0f;
                c.lastClickTime[button] = now;
                c.lastClickPosition[button] = {x, y};
            } else {
                c.pendingReleased[button] = true;
                c.mouseDown[button] = false;
                if (!c.payload.type.empty() && c.dragSource.size() && button == MouseButton_Left) {
                    c.payloadDropped = true;
                }
            }
        }
        const auto mx = static_cast<int>(std::lround(x));
        const auto my = static_cast<int>(std::lround(y));
        if (down) {
            input->injectMousePress(mx, my, MyGUI::MouseButton(static_cast<MyGUI::MouseButton::Enum>(button)));
        } else {
            input->injectMouseRelease(mx, my, MyGUI::MouseButton(static_cast<MyGUI::MouseButton::Enum>(button)));
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        const float wheelDirection = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        const float wheel = event.wheel.y * wheelDirection;
        const float wheelH = event.wheel.x * wheelDirection;
        c.pendingMouseWheel += wheel;
        c.pendingMouseWheelH += wheelH;
        c.wheelAbsolute += static_cast<int>(std::lround(wheel * 120.0f));
        input->injectMouseMove(clampInt(c.io.MousePos.x), clampInt(c.io.MousePos.y), c.wheelAbsolute);
        return true;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        const Key key = keyFromScancode(event.key.scancode);
        const auto myKey = keyCodeFromScancode(event.key.scancode);
        if (key != Key_None && static_cast<std::size_t>(key) < c.keyDown.size()) {
            if (down) {
                c.pendingKeyPressed[static_cast<std::size_t>(key)] = true;
                c.keyDown[static_cast<std::size_t>(key)] = true;
            } else {
                c.pendingKeyReleased[static_cast<std::size_t>(key)] = true;
                c.keyDown[static_cast<std::size_t>(key)] = false;
            }
        }
        const SDL_Keymod mod = event.key.mod;
        c.io.KeyCtrl = (mod & SDL_KMOD_CTRL) != 0;
        c.io.KeyShift = (mod & SDL_KMOD_SHIFT) != 0;
        c.io.KeyAlt = (mod & SDL_KMOD_ALT) != 0;
        if (down) {
            input->injectKeyPress(myKey);
        } else {
            input->injectKeyRelease(myKey);
        }
        return true;
    }
    case SDL_EVENT_TEXT_INPUT:
        injectTextUtf8(event.text.text);
        return true;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        for (std::size_t i = 0; i < c.mouseDown.size(); ++i) {
            c.mouseDown[i] = false;
            c.pendingReleased[i] = true;
        }
        for (std::size_t i = 0; i < c.keyDown.size(); ++i) {
            c.keyDown[i] = false;
        }
        c.io.KeyCtrl = c.io.KeyShift = c.io.KeyAlt = false;
        return true;
    default:
        return false;
    }
}

void setDisplaySize(float width, float height, float fbScaleX, float fbScaleY) {
    if (!gContext) {
        return;
    }
    auto& c = *gContext;
    c.io.DisplaySize = {std::max(1.0f, width), std::max(1.0f, height)};
    c.io.DisplayFramebufferScale = {std::max(0.25f, fbScaleX), std::max(0.25f, fbScaleY)};
    c.viewport.Size = c.io.DisplaySize;
    c.viewport.WorkPos = {0.0f, 28.0f};
    c.viewport.WorkSize = {c.io.DisplaySize.x, std::max(1.0f, c.io.DisplaySize.y - 28.0f)};
    if (c.platform) {
        c.platform->setViewSize(clampInt(c.io.DisplaySize.x),
                                clampInt(c.io.DisplaySize.y),
                                clampInt(c.io.DisplaySize.x * fbScaleX),
                                clampInt(c.io.DisplaySize.y * fbScaleY));
        if (auto* menu = c.menuBarRoot) {
            menu->setSize(MyGUI::IntSize(clampInt(width), 28));
        }
    }
}

void setDeltaTime(float dt) {
    if (!gContext) {
        return;
    }
    gContext->io.DeltaTime = std::clamp(dt, 1.0f / 1000.0f, 0.25f);
}

void injectMousePos(float x, float y) {
    if (!gContext) return;
    auto& c = *gContext;
    c.io.MousePos = {x, y};
    if (auto* input = inputManager()) {
        input->injectMouseMove(clampInt(x), clampInt(y), c.wheelAbsolute);
    }
}

void injectMouseButton(int button, bool down) {
    if (!gContext || button < 0 || button >= 3) return;
    auto& c = *gContext;
    if (down) {
        c.pendingPressed[button] = true;
        c.mouseDown[button] = true;
        c.pressPosition[button] = c.io.MousePos;
        const double now = c.seconds + c.time;
        const float dx = c.io.MousePos.x - c.lastClickPosition[button].x;
        const float dy = c.io.MousePos.y - c.lastClickPosition[button].y;
        c.pendingDouble[button] = c.lastClickTime[button] >= 0.0 &&
                                  now - c.lastClickTime[button] <= 0.30 &&
                                  dx * dx + dy * dy <= 36.0f;
        c.lastClickTime[button] = now;
        c.lastClickPosition[button] = c.io.MousePos;
    } else {
        c.pendingReleased[button] = true;
        c.mouseDown[button] = false;
        if (!c.payload.type.empty() && !c.dragSource.empty() && button == MouseButton_Left) {
            c.payloadDropped = true;
        }
    }
    if (auto* input = inputManager()) {
        const auto pos = c.io.MousePos;
        if (down) {
            input->injectMousePress(clampInt(pos.x), clampInt(pos.y), MyGUI::MouseButton(static_cast<MyGUI::MouseButton::Enum>(button)));
        } else {
            input->injectMouseRelease(clampInt(pos.x), clampInt(pos.y), MyGUI::MouseButton(static_cast<MyGUI::MouseButton::Enum>(button)));
        }
    }
}

void injectMouseWheel(float wheel, float horizontal) {
    if (!gContext) return;
    auto& c = *gContext;
    c.pendingMouseWheel += wheel;
    c.pendingMouseWheelH += horizontal;
    c.wheelAbsolute += static_cast<int>(std::lround(wheel * 120.0f));
    if (auto* input = inputManager()) {
        input->injectMouseMove(clampInt(c.io.MousePos.x), clampInt(c.io.MousePos.y), c.wheelAbsolute);
    }
}

void injectKey(Key key, bool down) {
    if (!gContext || key <= Key_None || key >= Key_COUNT) return;
    auto& c = *gContext;
    if (down) {
        c.pendingKeyPressed[key] = true;
        c.keyDown[key] = true;
    } else {
        c.pendingKeyReleased[key] = true;
        c.keyDown[key] = false;
    }
    if (auto* input = inputManager()) {
        if (down) input->injectKeyPress(myGuiKey(key));
        else input->injectKeyRelease(myGuiKey(key));
    }
}

void injectKeyMod(uint32_t modFlag, bool down) {
    if (!gContext) return;
    auto& c = *gContext;
    if (modFlag == Mod_Ctrl) c.io.KeyCtrl = down;
    if (modFlag == Mod_Shift) c.io.KeyShift = down;
    if (modFlag == Mod_Alt) c.io.KeyAlt = down;
    if (auto* input = inputManager()) {
        const auto key = modFlag == Mod_Ctrl ? MyGUI::KeyCode::LeftControl
                         : modFlag == Mod_Shift ? MyGUI::KeyCode::LeftShift
                                                : MyGUI::KeyCode::LeftAlt;
        if (down) input->injectKeyPress(key);
        else input->injectKeyRelease(key);
    }
}

void injectTextUtf8(const char* utf8) {
    if (!gContext || !utf8) return;
    auto* input = inputManager();
    if (!input) return;
    const char* p = utf8;
    const char* end = utf8 + std::strlen(utf8);
    while (p < end) {
        const auto cp = utf8Codepoint(p, end);
        input->injectKeyPress(MyGUI::KeyCode::None, cp);
        input->injectKeyRelease(MyGUI::KeyCode::None);
    }
}

void setClipboardCallbacks(std::function<void(const std::string&)> setter,
                          std::function<std::string()> getter) {
    if (!gContext || !gContext->platform) return;
    gContext->clipboardSetter = std::move(setter);
    gContext->clipboardGetter = std::move(getter);
    gContext->platform->setClipboardCallbacks(gContext->clipboardSetter, gContext->clipboardGetter);
}

void setClipboardText(const char* text) {
    if (!MyGUI::ClipboardManager::getInstancePtr()) return;
    MyGUI::ClipboardManager::getInstance().setClipboardData("Text", text ? text : "");
}

std::string getClipboardText() {
    if (!MyGUI::ClipboardManager::getInstancePtr()) return {};
    return MyGUI::ClipboardManager::getInstance().getClipboardData("Text");
}

void newFrame() {
    if (!gContext) return;
    auto& c = *gContext;
    c.inFrame = true;
    ++c.frame;
    c.seconds += c.lastFrameDt;
    c.lastFrameDt = c.io.DeltaTime;
    c.io.Framerate = c.io.DeltaTime > 0 ? 1.0f / c.io.DeltaTime : 0.0f;
    c.io.MouseDelta = {c.io.MousePos.x - c.previousMouse.x, c.io.MousePos.y - c.previousMouse.y};
    c.previousMouse = c.io.MousePos;
    c.io.MouseWheel = c.pendingMouseWheel;
    c.io.MouseWheelH = c.pendingMouseWheelH;
    c.pendingMouseWheel = 0.0f;
    c.pendingMouseWheelH = 0.0f;
    c.mouseWheelConsumed = false;
    c.mousePressed = c.pendingPressed;
    c.mouseReleased = c.pendingReleased;
    c.mouseDouble = c.pendingDouble;
    c.pendingPressed.fill(false);
    c.pendingReleased.fill(false);
    c.pendingDouble.fill(false);
    c.keyPressed = c.pendingKeyPressed;
    c.pendingKeyPressed.fill(false);
    c.pendingKeyReleased.fill(false);
    c.layouts.clear();
    c.windowStack.clear();
    c.popupStack.clear();
    c.menuStack.clear();
    c.tables.clear();
    c.currentWindow = nullptr;
    c.lastItem = nullptr;
    c.lastItemRecord = nullptr;
    c.lastItemRect = {};
    c.disabledDepth = 0;
    c.disabled = false;
    c.menuBarX = 8.0f;
    c.focusNext = false;
    c.focusInputPending = false;
    c.focusInputTarget = -1;
    c.focusInputScope.clear();
    c.focusInputIndices.clear();
    c.focusInputs.clear();
    c.hasNextPos = false;
    c.hasNextSize = false;
    c.hasNextConstraints = false;
    c.nextItemWidth = 0.0f;
    c.wrapLocalPosX = 0.0f;
    c.wrapStack.clear();
    c.openPopup = c.popupPending.empty() ? c.openPopup : c.popupPending;
    c.popupPending.clear();
    if (c.keyPressed[Key_Escape] && !c.openPopup.empty()) {
        const auto popup = c.popups.find(c.openPopup);
        if (popup == c.popups.end() || !popup->second.modal) {
            const std::string id = c.openPopup;
            closePopup(c, id);
            if (c.activeMenu == id) c.activeMenu.clear();
        }
    }
    c.payloadAccepted = false;
    c.background.reset(c.io.DisplaySize.x, c.io.DisplaySize.y,
                       c.platform->defaultFont(), c.platform->renderManager().whiteTexture());
    c.foreground.reset(c.io.DisplaySize.x, c.io.DisplaySize.y,
                       c.platform->defaultFont(), c.platform->renderManager().whiteTexture());
    c.platform->renderManager().clearOverlays();
    for (auto& entry : c.windows) {
        entry.second.drawList.reset(c.io.DisplaySize.x,
                                    c.io.DisplaySize.y,
                                    c.platform->defaultFont(),
                                    c.platform->renderManager().whiteTexture());
    }
    for (auto& entry : c.popups) {
        if (entry.second.open && entry.second.tooltip) {
            entry.second.open = false;
            if (entry.second.root) entry.second.root->setVisible(false);
        }
    }
    // MyGUI input remains authoritative; this state only mirrors the active text editor.
    auto* manager = inputManager();
    MyGUI::Widget* focus = manager ? manager->getKeyFocusWidget() : nullptr;
    const bool textFocused = focus && focus->castType<MyGUI::EditBox>(false);
    c.io.WantTextInput = textFocused;
    c.io.WantCaptureKeyboard = focus != nullptr;
    c.io.WantCaptureMouse = (manager && manager->getMouseFocusWidget() != nullptr) || c.mouseDown[0] ||
                            c.mouseDown[1] || c.mouseDown[2];
    if (manager && manager->getMouseFocusWidget()) {
        auto* hovered = manager->getMouseFocusWidget();
        if (!c.lastItemRecord || !widgetIsDescendant(hovered, c.lastItem)) {
            // Hover identity and delay are updated by item queries, not by draw order.
        }
    }
    if (c.platform) {
        c.platform->renderManager().setFramebufferScale(c.io.DisplayFramebufferScale.x,
                                                        c.io.DisplayFramebufferScale.y);
    }
}

void render() {
    if (!gContext || !gContext->platform) return;
    auto& c = *gContext;
    for (auto& entry : c.widgets) {
        auto& record = *entry.second;
        if (record.widget && record.lastFrame != c.frame) {
            record.widget->setVisible(false);
        }
    }
    for (auto& entry : c.windows) {
        auto& window = entry.second;
        if (window.root && window.lastFrame != c.frame) {
            window.root->setVisible(false);
        }
    }
    for (auto& entry : c.popups) {
        auto& popup = entry.second;
        if (popup.root && (!popup.open || (!popup.tooltip && popup.lastFrame + 2 < c.frame))) {
            popup.root->setVisible(false);
        }
    }
    if (c.background.vertices().size()) {
        c.platform->renderManager().submitOverlay(c.background.buildBatch(0));
    }
    for (auto& entry : c.windows) {
        auto& window = entry.second;
        if (window.root && window.root->getVisible() && !window.drawList.vertices().empty()) {
            c.platform->renderManager().submitOverlay(window.drawList.buildBatch(1));
        }
    }
    for (auto& entry : c.popups) {
        auto& popup = entry.second;
        if (popup.root && popup.root->getVisible()) {
            // Popup children use the same retained widget tree; draw lists remain foreground.
        }
    }
    if (!c.foreground.vertices().empty()) {
        c.platform->renderManager().submitOverlay(c.foreground.buildBatch(2));
    }
    c.platform->renderManager().drawOneFrame(c.io.DeltaTime);
    c.inFrame = false;
    c.lastFrameDt = c.io.DeltaTime;
    if (c.payloadDropped && !c.payloadAccepted) {
        c.payload.type.clear();
        c.payload.bytes.clear();
        c.dragSource.clear();
        c.payloadDropped = false;
    }
}

std::size_t lastFrameVertexCount() {
    return gContext && gContext->platform ? gContext->platform->renderManager().lastFrameVertexCount() : 0;
}

void renderStandaloneFpsOverlay(int fbWidth, int fbHeight, const char* label) {
    if (!gContext || !gContext->platform) return;
    DrawList draw;
    draw.reset(static_cast<float>(std::max(1, fbWidth)),
               static_cast<float>(std::max(1, fbHeight)),
               gContext->platform->defaultFont(),
               gContext->platform->renderManager().whiteTexture());
    const std::string text = label ? label : "";
    const auto size = calcTextSize(text.c_str());
    draw.addRectFilled({4, 4}, {size.x + 16, size.y + 16}, UI_COL32(0, 0, 0, 180));
    draw.addText({8, 8}, UI_COL32_WHITE, text.c_str());
    gContext->platform->renderManager().renderStandaloneOverlay(fbWidth,
                                                                 fbHeight,
                                                                 draw.buildBatch(1));
}

// ---------------------------------------------------------------------------
// Style and theme
// ---------------------------------------------------------------------------

IO& io() {
    static IO fallback;
    return gContext ? gContext->io : fallback;
}

Style& style() {
    return gContext ? gContext->style : gFallbackStyle;
}

void styleColorsDark(Style* target) {
    Style& s = target ? *target : style();
    s.Colors[Col_Text] = {.88f, .91f, .95f, 1};
    s.Colors[Col_TextDisabled] = {.48f, .54f, .63f, 1};
    s.Colors[Col_WindowBg] = {.10f, .12f, .15f, 1};
    s.Colors[Col_ChildBg] = {.09f, .11f, .14f, 1};
    s.Colors[Col_PopupBg] = {.13f, .15f, .19f, 1};
    s.Colors[Col_Border] = {.22f, .26f, .32f, 1};
    s.Colors[Col_FrameBg] = {.16f, .19f, .24f, 1};
    s.Colors[Col_FrameBgHovered] = {.22f, .29f, .38f, 1};
    s.Colors[Col_FrameBgActive] = {.24f, .36f, .51f, 1};
    s.Colors[Col_TitleBg] = {.08f, .10f, .13f, 1};
    s.Colors[Col_TitleBgActive] = {.14f, .19f, .26f, 1};
    s.Colors[Col_TitleBgCollapsed] = s.Colors[Col_TitleBg];
    s.Colors[Col_MenuBarBg] = {.08f, .10f, .13f, 1};
    s.Colors[Col_ScrollbarBg] = {.08f, .10f, .13f, 1};
    s.Colors[Col_ScrollbarGrab] = {.27f, .32f, .40f, 1};
    s.Colors[Col_ScrollbarGrabHovered] = {.36f, .43f, .53f, 1};
    s.Colors[Col_ScrollbarGrabActive] = {.42f, .52f, .65f, 1};
    s.Colors[Col_CheckMark] = {.38f, .70f, 1, 1};
    s.Colors[Col_SliderGrab] = {.30f, .59f, .90f, 1};
    s.Colors[Col_SliderGrabActive] = {.48f, .76f, 1, 1};
    s.Colors[Col_Button] = {.18f, .25f, .34f, 1};
    s.Colors[Col_ButtonHovered] = {.24f, .41f, .61f, 1};
    s.Colors[Col_ButtonActive] = {.20f, .48f, .77f, 1};
    s.Colors[Col_Header] = {.18f, .27f, .38f, 1};
    s.Colors[Col_HeaderHovered] = {.23f, .38f, .56f, 1};
    s.Colors[Col_HeaderActive] = {.24f, .44f, .67f, 1};
    s.Colors[Col_Separator] = s.Colors[Col_Border];
    s.Colors[Col_SeparatorHovered] = s.Colors[Col_ButtonHovered];
    s.Colors[Col_SeparatorActive] = s.Colors[Col_ButtonActive];
    s.Colors[Col_ResizeGrip] = {.30f, .59f, .90f, .25f};
    s.Colors[Col_ResizeGripHovered] = {.30f, .59f, .90f, .65f};
    s.Colors[Col_ResizeGripActive] = s.Colors[Col_SliderGrab];
    s.Colors[Col_Tab] = s.Colors[Col_FrameBg];
    s.Colors[Col_TabHovered] = s.Colors[Col_ButtonHovered];
    s.Colors[Col_TabSelected] = s.Colors[Col_Header];
    s.Colors[Col_TabDimmed] = {.11f, .13f, .16f, 1};
    s.Colors[Col_TabDimmedSelected] = {.16f, .19f, .24f, 1};
    s.Colors[Col_TextSelectedBg] = {.25f, .53f, .85f, .45f};
    s.Colors[Col_NavCursor] = s.Colors[Col_CheckMark];
}

void styleColorsLight(Style* target) {
    Style& s = target ? *target : style();
    styleColorsDark(&s);
    s.Colors[Col_Text] = {.12f, .12f, .12f, 1};
    s.Colors[Col_TextDisabled] = {.43f, .43f, .43f, 1};
    s.Colors[Col_WindowBg] = {.76f, .76f, .76f, 1};
    s.Colors[Col_ChildBg] = {.79f, .79f, .79f, 1};
    s.Colors[Col_PopupBg] = {.88f, .88f, .88f, 1};
    s.Colors[Col_Border] = {.39f, .39f, .39f, 1};
    s.Colors[Col_FrameBg] = {.94f, .94f, .94f, 1};
    s.Colors[Col_FrameBgHovered] = {.98f, .98f, .98f, 1};
    s.Colors[Col_FrameBgActive] = {.82f, .87f, .93f, 1};
    s.Colors[Col_TitleBg] = {.66f, .66f, .66f, 1};
    s.Colors[Col_TitleBgActive] = {.70f, .70f, .70f, 1};
    s.Colors[Col_MenuBarBg] = {.82f, .82f, .82f, 1};
    s.Colors[Col_Button] = {.74f, .74f, .74f, 1};
    s.Colors[Col_ButtonHovered] = {.84f, .84f, .84f, 1};
    s.Colors[Col_ButtonActive] = {.50f, .65f, .81f, 1};
    s.Colors[Col_Header] = {.40f, .57f, .76f, .65f};
    s.Colors[Col_HeaderHovered] = {.43f, .63f, .84f, .8f};
    s.Colors[Col_HeaderActive] = {.30f, .51f, .75f, 1};
    s.Colors[Col_CheckMark] = {.12f, .29f, .48f, 1};
    s.Colors[Col_TextSelectedBg] = {.24f, .49f, .78f, .4f};
    s.Colors[Col_Tab] = {.66f, .66f, .66f, 1};
    s.Colors[Col_TabSelected] = {.84f, .84f, .84f, 1};
    s.Colors[Col_TabHovered] = {.78f, .83f, .89f, 1};
    s.Colors[Col_TabDimmed] = {.64f, .64f, .64f, 1};
    s.Colors[Col_TabDimmedSelected] = {.76f, .76f, .76f, 1};
}

void syncThemeToMyGui(bool glossy) {
    if (!gContext || !gContext->platform) return;
    auto& s = gContext->style;
    SkinAtlasPalette p;
    p.windowBg = s.Colors[Col_WindowBg];
    p.childBg = s.Colors[Col_ChildBg];
    p.popupBg = s.Colors[Col_PopupBg];
    p.border = s.Colors[Col_Border];
    p.titleBg = s.Colors[Col_TitleBg];
    p.titleBgActive = s.Colors[Col_TitleBgActive];
    p.menuBarBg = s.Colors[Col_MenuBarBg];
    p.frameBg = s.Colors[Col_FrameBg];
    p.frameBgHovered = s.Colors[Col_FrameBgHovered];
    p.frameBgActive = s.Colors[Col_FrameBgActive];
    p.button = s.Colors[Col_Button];
    p.buttonHovered = s.Colors[Col_ButtonHovered];
    p.buttonActive = s.Colors[Col_ButtonActive];
    p.header = s.Colors[Col_Header];
    p.headerHovered = s.Colors[Col_HeaderHovered];
    p.headerActive = s.Colors[Col_HeaderActive];
    p.checkMark = s.Colors[Col_CheckMark];
    p.scrollbarBg = s.Colors[Col_ScrollbarBg];
    p.scrollbarGrab = s.Colors[Col_ScrollbarGrab];
    p.scrollbarGrabHovered = s.Colors[Col_ScrollbarGrabHovered];
    p.scrollbarGrabActive = s.Colors[Col_ScrollbarGrabActive];
    p.tab = s.Colors[Col_Tab];
    p.tabHovered = s.Colors[Col_TabHovered];
    p.tabSelected = s.Colors[Col_TabSelected];
    p.text = s.Colors[Col_Text];
    p.textDisabled = s.Colors[Col_TextDisabled];
    p.frameRounding = s.FrameRounding;
    p.frameBorderSize = s.FrameBorderSize;
    p.glossy = glossy;
    gContext->platform->updateSkinAtlas(p);
}

Color styleColor(ColorRole role) {
    const auto index = std::clamp(static_cast<int>(role), 0, Col_COUNT - 1);
    return style().Colors[static_cast<std::size_t>(index)];
}

uint32_t colorU32(ColorRole role) {
    return colorU32(styleColor(role));
}

uint32_t colorU32(Color c) {
    return c.toAbgr();
}

Color colorFromU32(uint32_t abgr) {
    return Color::fromAbgr(abgr);
}

void pushStyleColor(ColorRole role, Color col) {
    const int idx = std::clamp(static_cast<int>(role), 0, Col_COUNT - 1);
    style().Colors[static_cast<std::size_t>(idx)] = col;
    if (gContext) {
        gContext->colorStack.emplace_back(role, col);
    }
}

void popStyleColor(int count) {
    if (!gContext) return;
    while (count-- > 0 && !gContext->colorStack.empty()) {
        auto [role, value] = gContext->colorStack.back();
        gContext->colorStack.pop_back();
        (void)value;
        // Editor theme owns the underlying palette; recover it from the last committed atlas palette.
        // Theme application refreshes the atlas after each frame if needed.
        const int idx = static_cast<int>(role);
        if (idx >= 0 && idx < Col_COUNT) {
            gContext->style.Colors[static_cast<std::size_t>(idx)] = gContext->style.Colors[static_cast<std::size_t>(idx)];
        }
    }
}

void pushStyleVar(StyleVar var, float val) {
    if (!gContext) return;
    auto& s = gContext->style;
    std::variant<float, Vec2> old;
    switch (var) {
    case StyleVar_WindowRounding: old = s.WindowRounding; s.WindowRounding = val; break;
    case StyleVar_WindowBorderSize: old = s.WindowBorderSize; s.WindowBorderSize = val; break;
    case StyleVar_FrameRounding: old = s.FrameRounding; s.FrameRounding = val; break;
    case StyleVar_WindowPadding: old = s.WindowPadding; s.WindowPadding = {val, val}; break;
    case StyleVar_FramePadding: old = s.FramePadding; s.FramePadding = {val, val}; break;
    }
    gContext->styleStack.emplace_back(var, old);
}

void pushStyleVar(StyleVar var, Vec2 val) {
    if (!gContext) return;
    auto& s = gContext->style;
    std::variant<float, Vec2> old;
    switch (var) {
    case StyleVar_WindowPadding: old = s.WindowPadding; s.WindowPadding = val; break;
    case StyleVar_FramePadding: old = s.FramePadding; s.FramePadding = val; break;
    case StyleVar_WindowRounding: old = s.WindowRounding; s.WindowRounding = val.x; break;
    case StyleVar_WindowBorderSize: old = s.WindowBorderSize; s.WindowBorderSize = val.x; break;
    case StyleVar_FrameRounding: old = s.FrameRounding; s.FrameRounding = val.x; break;
    }
    gContext->styleStack.emplace_back(var, old);
}

void popStyleVar(int count) {
    if (!gContext) return;
    auto& s = gContext->style;
    while (count-- > 0 && !gContext->styleStack.empty()) {
        auto pair = gContext->styleStack.back();
        gContext->styleStack.pop_back();
        switch (pair.first) {
        case StyleVar_WindowPadding: s.WindowPadding = std::get<Vec2>(pair.second); break;
        case StyleVar_FramePadding: s.FramePadding = std::get<Vec2>(pair.second); break;
        case StyleVar_WindowRounding: s.WindowRounding = std::get<float>(pair.second); break;
        case StyleVar_WindowBorderSize: s.WindowBorderSize = std::get<float>(pair.second); break;
        case StyleVar_FrameRounding: s.FrameRounding = std::get<float>(pair.second); break;
        }
    }
}

// ---------------------------------------------------------------------------
// Viewport, font, timing, ID stack
// ---------------------------------------------------------------------------

const Viewport* mainViewport() {
    static Viewport fallback;
    if (!gContext) return &fallback;
    gContext->viewport.Size = gContext->io.DisplaySize;
    gContext->viewport.WorkSize = {gContext->io.DisplaySize.x, std::max(1.0f, gContext->io.DisplaySize.y - 28.0f)};
    return &gContext->viewport;
}

float fontSize() {
    return gContext && gContext->platform && gContext->platform->defaultFont()
               ? static_cast<float>(gContext->platform->defaultFont()->getDefaultHeight())
               : kDefaultFontSize;
}

LegacyMergedFont* defaultFont() {
    return gContext && gContext->platform ? gContext->platform->defaultFont() : nullptr;
}

Vec2 calcTextSize(const char* text, const char* textEnd) {
    if (!text) return {};
    if (!textEnd) textEnd = text + std::strlen(text);
    float maxWidth = 0.0f;
    float lineWidth = 0.0f;
    int lines = 1;
    const char* p = text;
    while (p < textEnd) {
        const char* begin = p;
        const auto cp = utf8Codepoint(p, textEnd);
        if (cp == '\n') {
            maxWidth = std::max(maxWidth, lineWidth);
            lineWidth = 0.0f;
            ++lines;
            continue;
        }
        if (gContext && gContext->platform && gContext->platform->defaultFont()) {
            const auto* glyph = gContext->platform->defaultFont()->getGlyphInfo(cp);
            if (glyph) lineWidth += glyph->advance;
        } else {
            lineWidth += static_cast<float>(p - begin) * 8.0f;
        }
    }
    maxWidth = std::max(maxWidth, lineWidth);
    return {maxWidth, static_cast<float>(lines) * fontSize()};
}

float textLineHeight() {
    return std::max(kMinItemHeight, fontSize());
}

float frameHeight() {
    return std::max(kDefaultLineHeight, fontSize() + style().FramePadding.y * 2.0f);
}

float frameHeightWithSpacing() {
    return frameHeight() + style().ItemSpacing.y;
}

double timeSeconds() {
    return gContext ? gContext->seconds + gContext->time : 0.0;
}

int frameCount() {
    return gContext ? static_cast<int>(gContext->frame) : static_cast<int>(gFallbackTime++);
}

float hoveredIdTimer() {
    if (!gContext || !gContext->lastItemRecord) return 0.0f;
    const auto* record = gContext->lastItemRecord;
    return record->hoverSince < 0.0f ? 0.0f : static_cast<float>(gContext->seconds - record->hoverSince);
}

void pushId(const char* strId) {
    if (gContext) gContext->idStack.emplace_back(strId ? strId : "");
}

void pushId(int intId) {
    if (gContext) gContext->idStack.push_back(std::to_string(intId));
}

void pushId(const void* ptrId) {
    if (gContext) gContext->idStack.push_back(std::to_string(reinterpret_cast<std::uintptr_t>(ptrId)));
}

void popId() {
    if (gContext && !gContext->idStack.empty()) gContext->idStack.pop_back();
}

uint64_t getId(const char* strId) {
    return gContext ? stableId(*gContext, strId ? strId : "") : hashStr(strId ? strId : "");
}

uint64_t hashStr(const char* str) {
    uint64_t hash = 1469598103934665603ull;
    if (str) {
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(str); *p; ++p) {
            hash ^= *p;
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

// ---------------------------------------------------------------------------
// Dock workspace: real MyGUI pane roots and tab buttons, deterministic layout
// ---------------------------------------------------------------------------

void dockWorkspaceBegin(Rect area, bool& reset, bool* panelOpenArray, int panelCount) {
    if (!gContext || !gContext->platform) return;
    auto& c = *gContext;
    setPanelOpenFlags(c, panelOpenArray, panelCount);
    area.width = std::max(1.0f, area.width);
    area.height = std::max(1.0f, area.height);
    c.workspaceRect = area;
    if (reset) {
        reset = false;
        if (panelOpenArray) {
            for (int i = 0; i < panelCount; ++i) panelOpenArray[i] = true;
        }
        c.activeTabByGroup[0] = "Hierarchy";
        c.activeTabByGroup[1] = "Inspector";
        c.activeTabByGroup[2] = "Scene";
        c.activeTabByGroup[3] = "Assets";
    }
    if (!c.workspaceRoot) {
        c.workspaceRoot = createRootWidget(c,
                                           "Widget",
                                           "LegacyChildSkin",
                                           area,
                                           "Main",
                                           "LE_DockWorkspace");
    }
    if (c.workspaceRoot) {
        c.workspaceRoot->setVisible(true);
        c.workspaceRoot->setNeedMouseFocus(false);
        c.workspaceRoot->setCoord(MyGUI::IntCoord(clampInt(area.x), clampInt(area.y),
                                                  std::max(1, clampInt(area.width)),
                                                  std::max(1, clampInt(area.height))));
    }
    const float left = area.width * .19f;
    const float right = area.width * .25f;
    const float middle = std::max(1.0f, area.width - left - right);
    const float bottom = area.height * .34f;
    const float topHeight = std::max(1.0f, area.height - bottom);
    const Rect leftRect{0, 0, left, area.height};
    const Rect centerRect{left, 0, middle, area.height};
    const Rect rightRect{left + middle, 0, right, area.height};
    const float leftBottom = area.height * .30f;
    const Rect hierarchyOuter{area.x + leftRect.x,
                              area.y + leftRect.y,
                              left,
                              std::max(kDockTabHeight + 1.0f, area.height - leftBottom)};
    const Rect prefabOuter{area.x + leftRect.x,
                           area.y + area.height - leftBottom,
                           left,
                           leftBottom};
    const Rect inspectorOuter{area.x + rightRect.x, area.y, right, area.height};
    const Rect sceneOuter{area.x + centerRect.x, area.y, middle, topHeight};
    const Rect bottomOuter{area.x + centerRect.x, area.y + topHeight, middle, bottom};
    // Panel rectangles remain local to the workspace; tab widgets are positioned in screen space.
    auto localize = [&](Rect r) {
        r.x -= area.x;
        r.y -= area.y;
        return r;
    };
    PanelGroup hierarchy, inspector, center, lower;
    makeDockGroup(c, hierarchy, 1, localize(hierarchyOuter), {"Hierarchy", "Prefab Window"}, c.activeTabByGroup[0]);
    c.activeTabByGroup[0] = hierarchy.active;
    makeDockGroup(c, inspector, 2, localize(inspectorOuter), {"Inspector"}, c.activeTabByGroup[1]);
    c.activeTabByGroup[1] = inspector.active;
    makeDockGroup(c, center, 3, localize(sceneOuter), {"Scene", "Game"}, c.activeTabByGroup[2]);
    c.activeTabByGroup[2] = center.active;
    makeDockGroup(c, lower, 4, localize(bottomOuter), {"Assets", "Script Editor", "Console"}, c.activeTabByGroup[3]);
    c.activeTabByGroup[3] = lower.active;
    c.groups["Hierarchy"] = std::move(hierarchy);
    c.groups["Inspector"] = std::move(inspector);
    c.groups["Scene"] = std::move(center);
    c.groups["Assets"] = std::move(lower);
    (void)topHeight;
}

void dockWorkspaceEnd() {
    if (!gContext) return;
    auto& c = *gContext;
    // Refresh exposed panel snapshots after tab clicks.
    for (const auto& pair : c.groups) {
        for (const auto& name : pair.second.names) {
            auto it = c.panels.find(name);
            if (it != c.panels.end()) it->second.activeDockTab = pair.second.active;
        }
    }
}

const PanelInfo* findPanel(const char* name) {
    if (!gContext || !name) return nullptr;
    auto it = gContext->panels.find(name);
    return it == gContext->panels.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// Windows, children, popups, modals, menus
// ---------------------------------------------------------------------------

void setNextWindowPos(Vec2 pos, Cond cond, Vec2 pivot) {
    if (!gContext) return;
    gContext->nextPos = pos;
    gContext->nextPosCond = cond;
    gContext->nextPivot = pivot;
    gContext->hasNextPos = true;
}

void setNextWindowSize(Vec2 size, Cond cond) {
    if (!gContext) return;
    gContext->nextSize = size;
    gContext->nextSizeCond = cond;
    gContext->hasNextSize = true;
}

void setNextWindowSizeConstraints(Vec2 minSize, Vec2 maxSize) {
    if (!gContext) return;
    gContext->minSize = minSize;
    gContext->maxSize = maxSize;
    gContext->hasNextConstraints = true;
}

void setNextWindowFocus() {
    if (gContext) gContext->focusNext = true;
}

void setWindowFocus(const char* name) {
    if (!gContext || !name) return;
    auto it = gContext->windows.find(name);
    if (it != gContext->windows.end() && it->second.root) {
        it->second.root->setVisible(true);
        it->second.focused = true;
        gContext->focusedRoot = it->second.root;
        if (auto* manager = inputManager()) manager->setKeyFocusWidget(it->second.root);
    }
}

bool beginWindow(const char* name, bool* open, WindowFlags flags) {
    if (!gContext || !name) return false;
    auto& c = *gContext;
    const std::string windowName(name);
    auto panel = c.panels.find(windowName);
    const bool docked = panel != c.panels.end() && c.workspaceRoot;
    if (docked && !panel->second.visible) {
        auto it = c.windows.find(windowName);
        if (it != c.windows.end() && it->second.root) it->second.root->setVisible(false);
        // Begin/End must remain balanced, even when a dock tab is inactive.
        c.windowStack.push_back(nullptr);
        c.layouts.emplace_back();
        return false;
    }
    const bool noTitle = docked || (flags & WindowFlags_NoTitleBar) != 0;
    WindowRecord& window = ensureWindow(c, windowName, noTitle, docked);
    window.openPtr = open;
    window.lastFrame = c.frame;
    window.root->setVisible(!open || *open);
    if (open && !*open) {
        c.windowStack.push_back(&window);
        c.layouts.emplace_back();
        return false;
    }
    if (docked) {
        Rect rect = panel->second.rect;
        rect.x += c.workspaceRect.x;
        rect.y += c.workspaceRect.y;
        window.docked = true;
        window.titleless = true;
        window.root->changeWidgetSkin("LegacyChildSkin");
        applyWindowRect(window, rect);
        panel->second.widget = window.root;
        panel->second.rect = rect;
        panel->second.visible = true;
    } else {
        if (c.hasNextSize) {
            if (c.nextSizeCond == Cond_Always || c.nextSizeCond == Cond_Appearing ||
                (c.nextSizeCond == Cond_FirstUseEver && window.lastFrame == c.frame) ||
                c.nextSizeCond == Cond_Once) {
                window.rect.width = c.nextSize.x > 0 ? c.nextSize.x : window.rect.width;
                window.rect.height = c.nextSize.y > 0 ? c.nextSize.y : window.rect.height;
            }
        }
        if (c.hasNextConstraints) {
            window.rect.width = std::clamp(window.rect.width, c.minSize.x, c.maxSize.x);
            window.rect.height = std::clamp(window.rect.height, c.minSize.y, c.maxSize.y);
        }
        if (c.hasNextPos && c.nextPosCond != Cond_None) {
            if (c.nextPosCond == Cond_Always || c.nextPosCond == Cond_Appearing ||
                (c.nextPosCond == Cond_FirstUseEver && window.lastFrame == c.frame) ||
                c.nextPosCond == Cond_Once) {
                window.rect.x = c.nextPos.x - c.nextPivot.x * window.rect.width;
                window.rect.y = c.nextPos.y - c.nextPivot.y * window.rect.height;
            }
        }
        if (c.hasNextPos && c.nextPosCond == Cond_None) {
            window.rect.x = c.nextPos.x;
            window.rect.y = c.nextPos.y;
        }
        applyWindowRect(window, window.rect);
    }
    if (!noTitle && window.title) {
        window.title->setCaption(MyGUI::UString(windowName));
    }
    window.root->setVisible(true);
    window.root->setEnabled(!c.disabled);
    c.currentWindow = &window;
    c.windowStack.push_back(&window);
    c.lastItem = nullptr;
    c.lastItemRecord = nullptr;
    const Rect clientRect{window.rect.x + c.style.WindowPadding.x,
                          window.rect.y + (window.titleless ? c.style.WindowPadding.y
                                                           : kTitleHeight + c.style.WindowPadding.y),
                          std::max(1.0f, window.rect.width - c.style.WindowPadding.x * 2.0f),
                          std::max(1.0f, window.rect.height - kTitleHeight - c.style.WindowPadding.y * 2.0f)};
    pushLayout(c,
               window.client,
               &window,
               clientRect,
               windowName,
               c.style.WindowPadding.x,
               c.style.WindowPadding.y);
    restoreScrollState(c, c.layouts.back(), "__window/" + windowName);
    c.layouts.back().horizontalScroll = (flags & WindowFlags_HorizontalScrollbar) != 0;
    if (c.focusNext) {
        c.focusNext = false;
        c.focusedRoot = window.root;
        if (auto* manager = inputManager()) manager->setKeyFocusWidget(window.root);
    }
    c.hasNextPos = c.hasNextSize = c.hasNextConstraints = false;
    c.nextPosCond = c.nextSizeCond = Cond_None;
    return true;
}

void endWindow() {
    if (!gContext || gContext->windowStack.empty()) return;
    auto& c = *gContext;
    auto* window = c.windowStack.back();
    c.windowStack.pop_back();
    if (!c.layouts.empty()) {
        if (c.layouts.back().parent) {
            finishScrollableLayout(c,
                                   c.layouts.back(),
                                   c.layouts.back().horizontalScroll,
                                   true);
        }
        c.layouts.pop_back();
    }
    c.currentWindow = c.windowStack.empty() ? nullptr : c.windowStack.back();
    if (window && window->openPtr && window->closeRequested) {
        *window->openPtr = false;
        window->root->setVisible(false);
        window->closeRequested = false;
    }
}

bool beginChild(const char* id, Vec2 size, ChildFlags childFlags, WindowFlags windowFlags) {
    if (!gContext || !id) return false;
    auto& c = *gContext;
    auto& parent = c.layout();
    const float width = size.x > 0
                            ? size.x
                            : std::max(1.0f,
                                       remainingWidth(parent, c.style.WindowPadding.x, true));
    const float height = size.y > 0 ? size.y : std::max(1.0f, parent.height - parent.cursorY);
    const float x = parent.cursorX + parent.indent;
    const float y = parent.cursorY;
    const auto skin = (childFlags & ChildFlags_Borders) ? "LegacyChildSkin" : "PanelEmpty";
    auto* child = ensureRawWidget(c,
                                  widgetKey(c, id, "child"),
                                  "Widget",
                                  skin,
                                  parent.parent,
                                  widgetCoord(parent, x, y, width, height));
    auto* record = recordForWidget(c, child);
    setLastItem(c, record, screenRectFor(parent, x, y, width, height));
    updateTableRowHeight(c, parent, height);
    parent.maxContentX = std::max(parent.maxContentX, x + width);
    LayoutScope scope;
    scope.parent = child;
    scope.window = parent.window;
    scope.screen = {parent.screen.x + x - parent.scrollX,
                    parent.screen.y + y - parent.scrollY,
                    width,
                    height};
    scope.width = width;
    scope.height = height;
    const float pad = (childFlags & ChildFlags_AlwaysUseWindowPadding) ? c.style.WindowPadding.x : 0.0f;
    scope.cursorX = scope.lineStartX = pad;
    scope.cursorY = pad;
    scope.child = true;
    scope.horizontalScroll = (windowFlags & WindowFlags_HorizontalScrollbar) != 0;
    scope.id = id;
    scope.scrollKey = widgetKey(c, id, "child");
    c.layouts.push_back(std::move(scope));
    restoreScrollState(c, c.layouts.back(), c.layouts.back().scrollKey);
    return true;
}

void endChild() {
    if (!gContext || gContext->layouts.size() < 2) return;
    auto& c = *gContext;
    auto& leaving = c.layouts.back();
    finishScrollableLayout(c, leaving, leaving.horizontalScroll, true);
    const float height = leaving.height;
    c.layouts.pop_back();
    auto& parent = c.layout();
    updateTableRowHeight(c, parent, height);
    parent.cursorX = parent.lineStartX;
    parent.cursorY += height + c.style.ItemSpacing.y;
    parent.lineHeight = height;
}

bool beginMainMenuBar() {
    if (!gContext || !gContext->menuBarRoot) return false;
    auto& c = *gContext;
    c.menuBarRoot->setVisible(true);
    c.menuBarRoot->setCoord(MyGUI::IntCoord(0, 0, clampInt(c.io.DisplaySize.x), 28));
    pushLayout(c, c.menuBarRoot, nullptr, {0, 0, c.io.DisplaySize.x, 28}, "__main_menu", c.menuBarX, 2);
    c.menuStack.push_back({true, false, false, {}, c.layouts.size() - 1, c.menuBarX});
    return true;
}

void endMainMenuBar() {
    if (!gContext || gContext->menuStack.empty()) return;
    auto& c = *gContext;
    const auto frame = c.menuStack.back();
    c.menuStack.pop_back();
    if (!c.layouts.empty()) {
        c.menuBarX = c.layouts.back().cursorX;
        c.layouts.pop_back();
    }
    (void)frame;
}

bool beginMenu(const char* label, bool enabled) {
    if (!gContext || !label) return false;
    auto& c = *gContext;
    const bool top = !c.menuStack.empty() && c.menuStack.back().mainMenu;
    const std::string menuId = widgetKey(c, label, "menu");
    auto* rec = item(c,
                     label,
                     "menuButton",
                     "Button",
                     "LegacyFlatButtonSkin",
                     measuredWidth(c, visibleLabel(label)) + 24.0f,
                     frameHeight(),
                     true);
    if (!rec) return false;
    setWidgetText(rec, std::string(visibleLabel(label)) + (top ? "  ▾" : "  ▸"));
    configureTextBox(c, rec->widget, c.style.Colors[Col_Text], !enabled || c.disabled);
    const bool hovered = hoveredWidget(c, rec->widget);
    if (enabled && rec->clickFrame == c.frame) {
        c.activeMenu = c.activeMenu == menuId ? std::string{} : menuId;
    } else if (enabled && hovered && c.mouseDown[MouseButton_Left] && c.activeMenu.size()) {
        c.activeMenu = menuId;
    }
    if (!enabled || c.activeMenu != menuId) {
        return false;
    }
    const Rect buttonRect = rec->rect;
    const float popupX = top ? buttonRect.x : buttonRect.x + buttonRect.width;
    const float popupY = top ? buttonRect.y + buttonRect.height : buttonRect.y;
    auto& popup = ensurePopup(c, menuId, false, false);
    if (popup.rect.x == 0 && popup.rect.y == 0) {
        popup.rect = {popupX, popupY, top ? 220.0f : 210.0f, 240.0f};
    } else {
        popup.rect.x = popupX;
        popup.rect.y = popupY;
    }
    if (popup.root) {
        popup.root->setCoord(MyGUI::IntCoord(clampInt(popup.rect.x), clampInt(popup.rect.y),
                                             clampInt(popup.rect.width), clampInt(popup.rect.height)));
    }
    c.popupStack.push_back(&popup);
    pushLayout(c, popup.client, c.currentWindow,
               {popup.rect.x + 8, popup.rect.y + 8, popup.rect.width - 16, popup.rect.height - 16},
               menuId, 4, 4);
    c.menuStack.push_back({false, true, true, menuId, c.layouts.size() - 1, 0});
    return true;
}

void endMenu() {
    if (!gContext || gContext->menuStack.empty()) return;
    auto& c = *gContext;
    const auto frame = c.menuStack.back();
    c.menuStack.pop_back();
    if (frame.popup) {
        if (!c.layouts.empty()) c.layouts.pop_back();
        if (!c.popupStack.empty()) c.popupStack.pop_back();
    }
}

bool menuItem(const char* label,
              const char* shortcutText,
              bool selected,
              bool enabled) {
    if (!gContext || !label) return false;
    auto& c = *gContext;
    auto* rec = item(c,
                     label,
                     "menuItem",
                     "Button",
                     "LegacyFlatButtonSkin",
                     std::max(160.0f, c.layout().width),
                     frameHeight(),
                     false);
    if (!rec) return false;
    std::string text(visibleLabel(label));
    if (shortcutText && *shortcutText) {
        text += "\t";
        text += shortcutText;
    }
    if (selected) text = "✓  " + text;
    setWidgetText(rec, text);
    configureTextBox(c, rec->widget, c.style.Colors[Col_Text], !enabled || c.disabled);
    if (rec->clickFrame == c.frame && enabled && !c.disabled) {
        if (!c.currentPopup.empty()) closePopup(c, c.currentPopup);
        if (!c.activeMenu.empty() && c.currentPopup.empty()) c.activeMenu.clear();
        return true;
    }
    return false;
}

bool menuItem(const char* label, const char* shortcutText, bool* selected, bool enabled) {
    const bool result = menuItem(label, shortcutText, selected && *selected, enabled);
    if (result && selected) *selected = !*selected;
    return result;
}

void openPopup(const char* strId) {
    if (gContext && strId) {
        gContext->popupPending = strId;
        gContext->openPopup = strId;
    }
}

bool beginPopup(const char* strId, WindowFlags) {
    if (!gContext || !strId) return false;
    auto& c = *gContext;
    return beginPopupInternal(c, strId, false, false);
}

bool beginPopupModal(const char* name, bool* open, WindowFlags) {
    if (!gContext || !name) return false;
    auto& c = *gContext;
    if (open && !*open) return false;
    const std::string id(name);
    if (c.openPopup != id) return false;
    auto& popup = ensurePopup(c, id, true, false);
    popup.modal = true;
    if (popup.root) {
        popup.rect.width = std::min(500.0f, std::max(280.0f, c.io.DisplaySize.x * .42f));
        popup.rect.height = std::min(360.0f, std::max(180.0f, c.io.DisplaySize.y * .38f));
        popup.rect.x = (c.io.DisplaySize.x - popup.rect.width) * .5f;
        popup.rect.y = (c.io.DisplaySize.y - popup.rect.height) * .5f;
        popup.root->setCoord(MyGUI::IntCoord(clampInt(popup.rect.x), clampInt(popup.rect.y),
                                             clampInt(popup.rect.width), clampInt(popup.rect.height)));
    }
    c.foreground.addRectFilled({0, 0}, c.io.DisplaySize, UI_COL32(0, 0, 0, 110));
    return beginPopupInternal(c, id, true, false);
}

bool beginPopupContextItem(const char* strId, PopupFlags flags) {
    if (!gContext) return false;
    auto& c = *gContext;
    const bool right = (flags & PopupFlags_MouseButtonRight) != 0;
    const std::string id = strId ? strId : (c.lastItemRecord ? c.lastItemRecord->key : "__context_item");
    if (right && c.mousePressed[MouseButton_Right] && itemHovered(c) &&
        (!(flags & PopupFlags_NoOpenOverItems) || c.lastItem != nullptr)) {
        c.openPopup = id;
    }
    return beginPopup(id.c_str());
}

bool beginPopupContextWindow(const char* strId, PopupFlags flags) {
    if (!gContext) return false;
    auto& c = *gContext;
    const std::string id = strId ? strId : (c.currentWindow ? c.currentWindow->name + "/context" : "__context_window");
    if ((flags & PopupFlags_MouseButtonRight) && c.mousePressed[MouseButton_Right] && currentWindowHovered(c)) {
        c.openPopup = id;
    }
    return beginPopup(id.c_str());
}

void endPopup() {
    if (!gContext || gContext->popupStack.empty()) return;
    auto& c = *gContext;
    if (!c.layouts.empty()) c.layouts.pop_back();
    c.popupStack.pop_back();
    if (c.popupStack.empty()) c.currentPopup.clear();
}

void closeCurrentPopup() {
    if (!gContext) return;
    if (!gContext->popupStack.empty()) closePopup(*gContext, gContext->popupStack.back()->id);
    else if (!gContext->openPopup.empty()) closePopup(*gContext, gContext->openPopup);
}

bool isPopupOpen(const char* strId, PopupFlags flags) {
    if (!gContext) return false;
    auto& c = *gContext;
    if (flags & PopupFlags_AnyPopupId) {
        for (const auto& pair : c.popups) if (pair.second.open) return true;
        return !c.openPopup.empty();
    }
    if (!strId || !*strId) return !c.openPopup.empty();
    auto it = c.popups.find(strId);
    return (it != c.popups.end() && it->second.open) || c.openPopup == strId;
}

Rect topmostPopupRect() {
    if (!gContext) return {};
    for (auto it = gContext->popupStack.rbegin(); it != gContext->popupStack.rend(); ++it) {
        if (*it) return (*it)->rect;
    }
    for (const auto& pair : gContext->popups) if (pair.second.open && !pair.second.tooltip) return pair.second.rect;
    return {};
}

bool beginTooltip() {
    if (!gContext || !isItemHovered()) return false;
    auto& c = *gContext;
    if (hoveredIdTimer() < 0.25f) return false;
    const std::string id = "__tooltip/" + (c.lastItemRecord ? c.lastItemRecord->key : "item");
    auto& popup = ensurePopup(c, id, false, true);
    popup.rect.x = std::clamp(c.io.MousePos.x + 12.0f,
                              4.0f,
                              std::max(4.0f, c.io.DisplaySize.x - popup.rect.width - 4.0f));
    popup.rect.y = std::clamp(c.io.MousePos.y + 18.0f,
                              4.0f,
                              std::max(4.0f, c.io.DisplaySize.y - popup.rect.height - 4.0f));
    if (popup.root) {
        popup.root->setCoord(MyGUI::IntCoord(clampInt(popup.rect.x),
                                             clampInt(popup.rect.y),
                                             clampInt(popup.rect.width),
                                             clampInt(popup.rect.height)));
    }
    c.popupStack.push_back(&popup);
    pushLayout(c,
               popup.client,
               c.currentWindow,
               {popup.rect.x + 6, popup.rect.y + 6, popup.rect.width - 12, popup.rect.height - 12},
               id,
               0,
               0);
    return true;
}

void endTooltip() {
    if (!gContext || gContext->popupStack.empty()) return;
    auto& c = *gContext;
    if (c.layouts.empty()) return;
    auto& layout = c.layout();
    auto* popup = c.popupStack.back();
    const float maxWidth = std::max(1.0f, std::min(480.0f, c.io.DisplaySize.x - 8.0f));
    const float minWidth = std::min(120.0f, maxWidth);
    if (popup) {
        popup->rect.width = std::clamp(layout.maxContentX + 16.0f, minWidth, maxWidth);
        popup->rect.height = std::clamp(layout.cursorY + 14.0f,
                                        32.0f,
                                        std::max(32.0f, c.io.DisplaySize.y - 8.0f));
        popup->rect.x = std::clamp(c.io.MousePos.x + 12.0f,
                                  4.0f,
                                  std::max(4.0f, c.io.DisplaySize.x - popup->rect.width - 4.0f));
        popup->rect.y = std::clamp(c.io.MousePos.y + 18.0f,
                                  4.0f,
                                  std::max(4.0f, c.io.DisplaySize.y - popup->rect.height - 4.0f));
        if (popup->root) {
            popup->root->setCoord(MyGUI::IntCoord(clampInt(popup->rect.x),
                                                  clampInt(popup->rect.y),
                                                  clampInt(popup->rect.width),
                                                  clampInt(popup->rect.height)));
        }
        popup->open = true;
    }
    c.layouts.pop_back();
    c.popupStack.pop_back();
}

void setTooltip(const char* fmt, ...) {
    if (!gContext || !fmt || !isItemHovered()) return;
    auto& c = *gContext;
    if (hoveredIdTimer() < 0.25f) return;
    va_list args;
    va_start(args, fmt);
    const std::string text = formatString(fmt, args);
    va_end(args);
    const std::string id = "__tooltip/" + (c.lastItemRecord ? c.lastItemRecord->key : "item");
    auto& popup = ensurePopup(c, id, false, true);
    const auto measured = calcTextSize(text.c_str());
    const float maxWidth = std::max(1.0f, std::min(480.0f, c.io.DisplaySize.x - 8.0f));
    const float minWidth = std::min(120.0f, maxWidth);
    popup.rect.width = std::clamp(measured.x + 18.0f, minWidth, maxWidth);
    popup.rect.height = std::clamp(measured.y + 14.0f,
                                   28.0f,
                                   std::max(28.0f, c.io.DisplaySize.y - 8.0f));
    popup.rect.x = std::clamp(c.io.MousePos.x + 14.0f,
                              4.0f,
                              std::max(4.0f, c.io.DisplaySize.x - popup.rect.width - 4.0f));
    popup.rect.y = std::clamp(c.io.MousePos.y + 18.0f,
                              4.0f,
                              std::max(4.0f, c.io.DisplaySize.y - popup.rect.height - 4.0f));
    if (popup.root) {
        popup.root->setCoord(MyGUI::IntCoord(clampInt(popup.rect.x),
                                             clampInt(popup.rect.y),
                                             clampInt(popup.rect.width),
                                             clampInt(popup.rect.height)));
    }
    if (popup.client) {
        if (!popup.tooltipText) {
            popup.tooltipText = popup.client->createWidget<MyGUI::TextBox>(
                "TextBoxSkin",
                MyGUI::IntCoord(3, 2, clampInt(popup.rect.width) - 18,
                                clampInt(popup.rect.height) - 12),
                MyGUI::Align::Stretch,
                "LE_TooltipText");
        }
        if (popup.tooltipText) {
            popup.tooltipText->setCoord(MyGUI::IntCoord(3,
                                                        2,
                                                        std::max(1, clampInt(popup.rect.width) - 18),
                                                        std::max(1, clampInt(popup.rect.height) - 12)));
            popup.tooltipText->setFontName("Default");
            popup.tooltipText->setFontHeight(static_cast<int>(kDefaultFontSize));
            popup.tooltipText->setTextColour(c.style.Colors[Col_Text].toMyGui());
            popup.tooltipText->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Top);
            popup.tooltipText->setCaption(MyGUI::UString(text));
        }
    }
}

// ---------------------------------------------------------------------------
// Layout & cursor
// ---------------------------------------------------------------------------

void sameLine(float offsetFromStartX, float spacingValue) {
    if (!gContext) return;
    auto& c = *gContext;
    auto& layout = c.layout();
    if (layout.lineHeight == 0.0f && layout.cursorY > 0) {
        layout.cursorY = std::max(0.0f, layout.cursorY - c.style.ItemSpacing.y);
    }
    if (offsetFromStartX > 0.0f) {
        layout.cursorX = layout.lineStartX + offsetFromStartX;
    } else if (c.lastItemRecord) {
        layout.cursorX = (c.lastItemRect.x - layout.screen.x) + layout.scrollX +
                         c.lastItemRect.width +
                         (spacingValue >= 0.0f ? spacingValue : c.style.ItemSpacing.x) -
                         layout.indent;
        layout.cursorY = std::max(0.0f, c.lastItemRect.y - layout.screen.y + layout.scrollY);
    } else {
        layout.cursorX += spacingValue >= 0.0f ? spacingValue : c.style.ItemSpacing.x;
    }
    layout.lineHeight = std::max(layout.lineHeight, frameHeight());
}

void newLine() {
    if (gContext) advanceLine(*gContext);
}

void spacing() {
    if (!gContext) return;
    auto& c = *gContext;
    c.layout().cursorY += std::max(4.0f, c.style.ItemSpacing.y);
}

void dummy(Vec2 size) {
    if (!gContext) return;
    auto& c = *gContext;
    const float height = size.y > 0 ? size.y : frameHeight();
    const float width = size.x > 0 ? size.x : 1.0f;
    const float x = c.layout().cursorX + c.layout().indent;
    const std::string id = widgetKey(c,
                                      "##dummy",
                                      std::to_string(c.frame) + "/" +
                                          std::to_string(c.layout().cursorY) + "/" +
                                          std::to_string(c.layout().cursorX));
    auto* rec = getRecord(c, id, "Widget", "PanelEmpty", c.layout().parent,
                          widgetCoord(c.layout(), x, c.layout().cursorY, width, height));
    setLastItem(c, rec, screenRectFor(c.layout(), x, c.layout().cursorY, width, height));
    updateTableRowHeight(c, c.layout(), height);
    c.layout().maxContentX = std::max(c.layout().maxContentX, x + width);
    c.layout().cursorY += height;
}

void separator() {
    if (!gContext) return;
    auto& c = *gContext;
    auto& layout = c.layout();
    const float y = layout.cursorY + 2.0f;
    auto* draw = c.currentWindow ? &c.currentWindow->drawList : &c.foreground;
    const float rightEdge = layout.screen.x +
                            (layout.tableCell ? layout.lineStartX + layout.width
                                              : layout.width) -
                            layout.scrollX;
    draw->addLine({layout.screen.x + layout.cursorX - layout.scrollX,
                   layout.screen.y + y - layout.scrollY},
                  {rightEdge, layout.screen.y + y - layout.scrollY},
                  colorU32(Col_Border), 1.0f);
    advanceLine(c, 4.0f);
}

void separatorText(const char* label) {
    if (!gContext) return;
    auto& c = *gContext;
    const float labelWidth = measuredWidth(c, visibleLabel(label)) + 12.0f;
    auto& layout = c.layout();
    const float y = layout.screen.y + layout.cursorY + textLineHeight() * .5f - layout.scrollY;
    const float rightEdge = layout.screen.x +
                            (layout.tableCell ? layout.lineStartX + layout.width
                                              : layout.width) -
                            layout.scrollX;
    auto* draw = c.currentWindow ? &c.currentWindow->drawList : &c.foreground;
    draw->addLine({layout.screen.x + layout.cursorX - layout.scrollX, y},
                  {rightEdge - labelWidth, y},
                  colorU32(Col_Border));
    textUnformatted(label);
    sameLine(labelWidth, 0.0f);
    draw->addLine({layout.screen.x + layout.cursorX - layout.scrollX, y},
                  {rightEdge, y},
                  colorU32(Col_Border));
    advanceLine(c, textLineHeight());
}

void indent(float indentW) {
    if (!gContext) return;
    auto& c = *gContext;
    const float value = indentW != 0.0f ? indentW : c.style.IndentSpacing;
    c.indentStack.push_back(c.layout().indent);
    c.layout().indent += value;
}

void unindent(float indentW) {
    if (!gContext) return;
    auto& c = *gContext;
    if (!c.indentStack.empty()) {
        c.layout().indent = c.indentStack.back();
        c.indentStack.pop_back();
    } else {
        c.layout().indent = std::max(0.0f, c.layout().indent - (indentW != 0.0f ? indentW : c.style.IndentSpacing));
    }
}

void alignTextToFramePadding() {
    if (gContext) gContext->layout().cursorY += std::max(0.0f, style().FramePadding.y - 2.0f);
}

void setNextItemWidth(float width) {
    if (gContext) gContext->layout().nextWidth = width;
}

void pushTextWrapPos(float wrapLocalPosX) {
    if (!gContext) return;
    gContext->wrapStack.push_back(gContext->wrapLocalPosX);
    gContext->wrapLocalPosX = wrapLocalPosX;
}

void popTextWrapPos() {
    if (!gContext || gContext->wrapStack.empty()) return;
    gContext->wrapLocalPosX = gContext->wrapStack.back();
    gContext->wrapStack.pop_back();
}

void beginDisabled(bool disabled) {
    if (!gContext) return;
    if (disabled) ++gContext->disabledDepth;
    gContext->disabled = gContext->disabledDepth > 0;
}

void endDisabled() {
    if (!gContext) return;
    gContext->disabledDepth = std::max(0, gContext->disabledDepth - 1);
    gContext->disabled = gContext->disabledDepth > 0;
}

Vec2 cursorPos() {
    if (!gContext) return {};
    const auto& layout = gContext->layout();
    return {layout.cursorX + layout.indent, layout.cursorY};
}

float cursorPosX() {
    if (!gContext) return 0.0f;
    const auto& layout = gContext->layout();
    return layout.cursorX + layout.indent;
}

void setCursorPos(Vec2 localPos) {
    if (!gContext) return;
    auto& layout = gContext->layout();
    layout.cursorX = localPos.x - layout.indent;
    layout.cursorY = localPos.y;
}

void setCursorPosX(float localX) {
    if (gContext) gContext->layout().cursorX = localX - gContext->layout().indent;
}

Vec2 cursorScreenPos() {
    if (!gContext) return {};
    auto& layout = gContext->layout();
    return {layout.screen.x + layout.cursorX + layout.indent - layout.scrollX,
            layout.screen.y + layout.cursorY - layout.scrollY};
}

void setCursorScreenPos(Vec2 screenPos) {
    if (!gContext) return;
    auto& layout = gContext->layout();
    layout.cursorX = screenPos.x - layout.screen.x + layout.scrollX - layout.indent;
    layout.cursorY = screenPos.y - layout.screen.y + layout.scrollY;
}

Vec2 contentRegionAvail() {
    if (!gContext) return {};
    const auto& l = gContext->layout();
    return {remainingWidth(l, style().WindowPadding.x, true),
            std::max(0.0f, l.height - l.cursorY)};
}

Vec2 windowContentRegionMax() {
    if (!gContext) return {};
    const auto& l = gContext->layout();
    return {std::max(0.0f, l.width - style().WindowPadding.x),
            std::max(0.0f, l.height - style().WindowPadding.y)};
}

Rect currentWindowInnerRect() {
    if (!gContext || !gContext->currentWindow) return {};
    return gContext->currentWindow->rect;
}

float scrollY() {
    return gContext ? gContext->layout().scrollY : 0.0f;
}

float scrollMaxY() {
    return gContext ? gContext->layout().scrollMaxY : 0.0f;
}

void setScrollHereY(float centerRatio) {
    if (!gContext) return;
    auto& c = *gContext;
    auto& l = c.layout();
    const float contentMax = std::max(0.0f, l.cursorY + c.style.WindowPadding.y - l.height);
    l.scrollMaxY = std::max(l.scrollMaxY, contentMax);
    l.scrollY = std::clamp(l.scrollMaxY * std::clamp(centerRatio, 0.0f, 1.0f),
                           0.0f,
                           l.scrollMaxY);
    saveScrollState(c, l);
}

// ---------------------------------------------------------------------------
// Widgets: MyGUI Button/TextBox/EditBox/ImageBox and layout helpers
// ---------------------------------------------------------------------------

namespace {

float textWrapWidth(Context& c) {
    const auto& layout = c.layout();
    if (!c.wrapStack.empty() && c.wrapLocalPosX > 0.0f) {
        return c.wrapLocalPosX - layout.cursorX - layout.indent;
    }
    return remainingWidth(layout, c.style.WindowPadding.x, true);
}

std::string wrapTextToWidth(Context& c, std::string_view text, float maxWidth) {
    maxWidth = std::max(1.0f, maxWidth);
    std::string wrapped;
    std::string word;
    std::string whitespace;
    float lineWidth = 0.0f;
    auto appendWord = [&] {
        if (word.empty()) return;
        const float wordWidth = measuredWidth(c, word);
        const float whitespaceWidth = measuredWidth(c, whitespace);
        if (lineWidth > 0.0f && lineWidth + whitespaceWidth + wordWidth > maxWidth) {
            wrapped.push_back('\n');
            lineWidth = 0.0f;
            whitespace.clear();
        } else if (!whitespace.empty()) {
            wrapped += whitespace;
            lineWidth += whitespaceWidth;
            whitespace.clear();
        }

        const char* p = word.data();
        const char* end = p + word.size();
        while (p < end) {
            const char* codepointStart = p;
            utf8Codepoint(p, end);
            const std::string_view codepoint(codepointStart,
                                             static_cast<std::size_t>(p - codepointStart));
            const float codepointWidth = measuredWidth(c, codepoint);
            if (lineWidth > 0.0f && lineWidth + codepointWidth > maxWidth) {
                wrapped.push_back('\n');
                lineWidth = 0.0f;
            }
            wrapped.append(codepoint.data(), codepoint.size());
            lineWidth += codepointWidth;
        }
        word.clear();
    };
    auto appendNewline = [&] {
        appendWord();
        wrapped += whitespace;
        whitespace.clear();
        wrapped.push_back('\n');
        lineWidth = 0.0f;
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '\n') {
            appendNewline();
        } else if (ch == '\r') {
            // Normalize both Unix and Windows line endings to a single newline.
            if (i + 1 < text.size() && text[i + 1] == '\n') continue;
            appendNewline();
        } else if (std::isspace(static_cast<unsigned char>(ch))) {
            appendWord();
            whitespace.push_back(ch);
        } else {
            word.push_back(ch);
        }
    }
    appendWord();
    wrapped += whitespace;
    return wrapped;
}

} // namespace

void textUnformatted(const char* text, const char* textEnd) {
    if (!gContext || !text) return;
    auto& c = *gContext;
    if (!textEnd) textEnd = text + std::strlen(text);
    std::string value(text, textEnd);
    if (!c.wrapStack.empty()) {
        value = wrapTextToWidth(c, value, textWrapWidth(c));
    }
    const auto measured = calcTextSize(value.c_str());
    const float width = std::max(1.0f,
                                 std::min(measured.x + 4.0f,
                                          std::max(1.0f,
                                                   remainingWidth(c.layout(), 0.0f, true))));
    const float height = std::max(textLineHeight(), measured.y);
    const auto key = widgetKey(c,
                               "##text",
                               std::to_string(c.frame) + "/" +
                                   std::to_string(c.layout().cursorY) + "/" +
                                   std::to_string(c.layout().cursorX));
    auto* rec = getRecord(c,
                          key,
                          "TextBox",
                          "TextBoxSkin",
                          c.layout().parent,
                          widgetCoord(c.layout(), c.layout().cursorX + c.layout().indent,
                                      c.layout().cursorY, width, height));
    if (rec) {
        setWidgetText(rec, value);
        configureTextBox(c, rec->widget, c.style.Colors[Col_Text], c.disabled);
    }
    setLastItem(c, rec, screenRectFor(c.layout(), c.layout().cursorX + c.layout().indent,
                                      c.layout().cursorY, width, height));
    finishLine(c, width, height);
}

void text(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const auto value = formatString(fmt, args);
    va_end(args);
    textUnformatted(value.c_str(), value.c_str() + value.size());
}

void textDisabled(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const auto value = formatString(fmt, args);
    va_end(args);
    if (!gContext) return;
    auto color = gContext->style.Colors[Col_Text];
    gContext->style.Colors[Col_Text] = gContext->style.Colors[Col_TextDisabled];
    textUnformatted(value.c_str(), value.c_str() + value.size());
    gContext->style.Colors[Col_Text] = color;
}

void textColored(Color col, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const auto value = formatString(fmt, args);
    va_end(args);
    if (!gContext) return;
    const auto old = gContext->style.Colors[Col_Text];
    gContext->style.Colors[Col_Text] = col;
    textUnformatted(value.c_str(), value.c_str() + value.size());
    gContext->style.Colors[Col_Text] = old;
}

void textWrapped(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const auto value = formatString(fmt, args);
    va_end(args);
    if (!gContext) return;
    auto& c = *gContext;
    const float maxWidth = textWrapWidth(c);
    const std::string wrapped = wrapTextToWidth(c, value, maxWidth);
    textUnformatted(wrapped.c_str(), wrapped.c_str() + wrapped.size());
}

bool button(const char* label, Vec2 size) {
    if (!gContext || !label) return false;
    auto& c = *gContext;
    const float width = size.x != 0.0f ? (size.x < 0 ? remainingWidth(c.layout(), 0.0f, true) : size.x) : 0.0f;
    const float height = size.y > 0 ? size.y : frameHeight();
    auto* rec = item(c, label, "button", "Button", "LegacyButtonSkin", width, height, size.x == 0.0f);
    if (!rec) return false;
    const auto labelText = visibleLabel(label);
    setWidgetText(rec, std::string(labelText));
    configureTextBox(c, rec->widget, c.style.Colors[Col_Text], c.disabled);
    const bool clicked = rec->clickFrame == c.frame && !c.disabled;
    if (rec->widget) {
        rec->widget->setColour({1, 1, 1, 1});
    }
    return clicked;
}

bool smallButton(const char* label) {
    return button(label, {0, std::max(18.0f, frameHeight() - 4.0f)});
}

bool invisibleButton(const char* strId, Vec2 size) {
    if (!gContext || !strId) return false;
    auto& c = *gContext;
    auto* rec = item(c, strId, "invisible", "Button", "PanelEmpty", size.x, size.y, false);
    return rec && rec->clickFrame == c.frame && !c.disabled;
}

bool arrowButton(const char* strId, Dir dir) {
    const char* arrow = dir == Dir_Left ? "‹" : dir == Dir_Right ? "›" : dir == Dir_Up ? "⌃" : "⌄";
    return button(strId ? (std::string(arrow) + (strId[0] == '#' ? "" : strId)).c_str() : arrow,
                  {frameHeight(), frameHeight()});
}

bool checkbox(const char* label, bool* value) {
    if (!gContext || !label || !value) return false;
    auto& c = *gContext;
    auto* rec = item(c, label, "checkbox", "Button", "LegacyCheckBoxSkin", 0.0f, frameHeight(), true);
    if (!rec) return false;
    setWidgetText(rec, std::string(visibleLabel(label)));
    configureTextBox(c, rec->widget, c.style.Colors[Col_Text], c.disabled);
    auto* check = rec->widget->castType<MyGUI::Button>(false);
    if (check) check->setStateSelected(*value);
    if (rec->clickFrame == c.frame && !c.disabled) {
        *value = !*value;
        if (check) check->setStateSelected(*value);
        return true;
    }
    return false;
}

bool radioButton(const char* label, bool active) {
    if (!gContext || !label) return false;
    auto& c = *gContext;
    auto* rec = item(c, label, "radio", "Button", "LegacyRadioSkin", 0.0f, frameHeight(), true);
    if (!rec) return false;
    setWidgetText(rec, std::string(visibleLabel(label)));
    configureTextBox(c, rec->widget, c.style.Colors[Col_Text], c.disabled);
    if (auto* button = rec->widget->castType<MyGUI::Button>(false)) button->setStateSelected(active);
    return rec->clickFrame == c.frame && !c.disabled;
}

bool selectable(const char* label, bool selected, SelectableFlags flags, Vec2 size) {
    if (!gContext || !label) return false;
    auto& c = *gContext;
    const float width = size.x != 0.0f ? (size.x < 0 ? remainingWidth(c.layout(), 0.0f, true) : size.x) : 0.0f;
    const float height = size.y > 0.0f ? size.y : frameHeight();
    // A zero width Selectable follows the editor convention: span the remaining
    // row width, including when the visible label is hidden behind an ID suffix.
    auto* rec = item(c, label, "selectable", "Button", "LegacyHeaderSkin", width, height, false);
    if (!rec) return false;
    setWidgetText(rec, std::string(visibleLabel(label)));
    configureTextBox(c, rec->widget, c.style.Colors[Col_Text], c.disabled);
    rec->selected = selected;
    if (auto* button = rec->widget->castType<MyGUI::Button>(false)) button->setStateSelected(selected);
    const bool clicked = rec->clickFrame == c.frame ||
                         ((flags & SelectableFlags_AllowDoubleClick) && rec->doubleClickFrame == c.frame);
    return clicked && !c.disabled;
}

void setNextItemOpen(bool isOpen, Cond) {
    if (gContext) gContext->nextTreeState[widgetKey(*gContext, "##treeNext")] = isOpen;
}

bool treeNode(const char* label) {
    return treeNodeEx(label, TreeNodeFlags_None, "%s", label ? label : "");
}

bool treeNodeEx(const char* strId, TreeNodeFlags flags, const char* fmt, ...) {
    if (!gContext || !strId) return false;
    auto& c = *gContext;
    std::string label;
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        label = formatString(fmt, args);
        va_end(args);
    } else {
        label = std::string(visibleLabel(strId));
    }
    const std::string key = widgetKey(c, strId, "tree");
    auto found = c.treeState.find(key);
    if (found == c.treeState.end()) {
        c.treeState[key] = (flags & TreeNodeFlags_DefaultOpen) != 0;
        found = c.treeState.find(key);
    }
    auto next = c.nextTreeState.find(widgetKey(c, "##treeNext"));
    if (next != c.nextTreeState.end()) {
        found->second = next->second;
        c.nextTreeState.erase(next);
    }
    const bool open = found->second;
    const float width = std::max(120.0f, remainingWidth(c.layout(), 0.0f, true));
    auto* rec = item(c,
                     strId,
                     "treeNode",
                     "Button",
                     "LegacyHeaderSkin",
                     width,
                     frameHeight(),
                     false);
    if (!rec) return false;
    setWidgetText(rec, std::string(open ? "▾  " : "▸  ") + label);
    configureTextBox(c, rec->widget, c.style.Colors[Col_Text], c.disabled);
    if (auto* button = rec->widget->castType<MyGUI::Button>(false)) button->setStateSelected(open);
    if (rec->clickFrame == c.frame && !c.disabled) found->second = !found->second;
    const bool nowOpen = found->second;
    if (nowOpen) indent();
    return nowOpen;
}

void treePop() {
    unindent();
}

bool collapsingHeader(const char* label, TreeNodeFlags flags) {
    const bool open = treeNodeEx(label, flags, "%s", label ? label : "");
    if (open) {
        // Header does not require a matching treePop in the callers that use this helper.
        unindent();
    }
    return open;
}

bool beginCombo(const char* label, const char* previewValue) {
    if (!gContext || !label) return false;
    auto& c = *gContext;
    const std::string id = "__combo/" + widgetKey(c, label);
    const std::string preview = std::string(previewValue ? previewValue : "") + "  ▾";
    auto* rec = item(c, label, "combo", "Button", "LegacyFrameSkin", 0.0f, frameHeight(), false);
    if (!rec) return false;
    setWidgetText(rec, preview);
    configureTextBox(c, rec->widget, c.style.Colors[Col_Text], c.disabled);
    if (rec->clickFrame == c.frame && !c.disabled) c.openPopup = id;
    if (c.openPopup != id) return false;
    auto& popup = ensurePopup(c, id, false, false);
    popup.rect.x = rec->rect.x;
    popup.rect.y = rec->rect.y + rec->rect.height;
    popup.rect.width = std::max(160.0f, rec->rect.width);
    popup.rect.height = std::min(240.0f, c.io.DisplaySize.y - popup.rect.y - 8.0f);
    if (popup.root) popup.root->setCoord(MyGUI::IntCoord(clampInt(popup.rect.x), clampInt(popup.rect.y),
                                                         clampInt(popup.rect.width), clampInt(popup.rect.height)));
    c.currentPopup = id;
    c.popupStack.push_back(&popup);
    pushLayout(c, popup.client, c.currentWindow, {popup.rect.x + 8, popup.rect.y + 8,
                                                 popup.rect.width - 16, popup.rect.height - 16}, id, 0, 0);
    return true;
}

void endCombo() {
    endPopup();
}

bool combo(const char* label, int* currentItem, const char* itemsSeparatedByZeros) {
    if (!label || !currentItem || !itemsSeparatedByZeros) return false;
    std::vector<std::string> items;
    const char* itemPtr = itemsSeparatedByZeros;
    while (*itemPtr) {
        const std::size_t len = std::strlen(itemPtr);
        items.emplace_back(itemPtr, len);
        itemPtr += len + 1;
    }
    if (items.empty()) return false;
    const int selected = std::clamp(*currentItem, 0, static_cast<int>(items.size()) - 1);
    bool changed = false;
    if (beginCombo(label, items[static_cast<std::size_t>(selected)].c_str())) {
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (selectable(items[i].c_str(), static_cast<int>(i) == selected)) {
                *currentItem = static_cast<int>(i);
                changed = true;
                closeCurrentPopup();
            }
        }
        endCombo();
    }
    return changed;
}

bool combo(const char* label, int* currentItem, const char* const items[], int itemCount) {
    if (!label || !currentItem || !items || itemCount <= 0) return false;
    const int selected = std::clamp(*currentItem, 0, itemCount - 1);
    bool changed = false;
    if (beginCombo(label, items[selected] ? items[selected] : "")) {
        for (int index = 0; index < itemCount; ++index) {
            const char* itemText = items[index] ? items[index] : "";
            if (selectable(itemText, index == selected)) {
                *currentItem = index;
                changed = true;
                closeCurrentPopup();
            }
        }
        endCombo();
    }
    return changed;
}

void setKeyboardFocusHere(int offset) {
    if (gContext) requestKeyboardFocus(*gContext, offset);
}

static std::size_t utf8ByteOffset(std::string_view text, std::size_t codepointOffset) {
    std::size_t codepoints = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0u) == 0x80u) continue;
        if (codepoints == codepointOffset) return i;
        ++codepoints;
    }
    return text.size();
}

static std::size_t utf8CodepointOffset(std::string_view text, std::size_t byteOffset) {
    std::size_t codepoints = 0;
    const auto end = std::min(byteOffset, text.size());
    for (std::size_t i = 0; i < end; ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0u) != 0x80u) ++codepoints;
    }
    return codepoints;
}

static std::size_t invokeInputCallback(WidgetRecord* record,
                                      char* buffer,
                                      std::size_t bufferSize,
                                      std::size_t cursorPosition,
                                      InputTextCallback callback,
                                      void* userData,
                                      InputTextFlags eventFlag) {
    if (!callback || !buffer || bufferSize == 0) return cursorPosition;
    InputTextCallbackData data;
    data.buf = buffer;
    data.bufSize = bufferSize;
    data.bufTextLen = static_cast<int>(std::strlen(buffer));
    data.cursorPos = std::clamp(static_cast<int>(cursorPosition), 0, data.bufTextLen);
    data.eventFlag = eventFlag;
    data.userData = userData;
    callback(&data);
    buffer[bufferSize - 1] = '\0';
    data.cursorPos = std::clamp(data.cursorPos, 0, static_cast<int>(std::strlen(buffer)));
    if (record) record->inputValue = buffer;
    return static_cast<std::size_t>(data.cursorPos);
}

static void invokeEditCallback(WidgetRecord* record,
                               MyGUI::EditBox* edit,
                               std::size_t bufferSize,
                               InputTextCallback callback,
                               void* userData,
                               InputTextFlags eventFlag) {
    if (!edit || bufferSize == 0) return;
    const std::string original = edit->getCaption().asUTF8();
    std::vector<char> buffer(bufferSize, '\0');
    const auto copied = std::min(original.size(), bufferSize - 1);
    std::memcpy(buffer.data(), original.data(), copied);
    const auto originalCursorBytes = utf8ByteOffset(original, edit->getTextCursor());
    const auto cursorBytes = invokeInputCallback(record,
                                                 buffer.data(),
                                                 bufferSize,
                                                 originalCursorBytes,
                                                 callback,
                                                 userData,
                                                 eventFlag);
    const std::string updated(buffer.data());
    if (updated != original) {
        edit->setCaption(MyGUI::UString(updated));
        edit->setTextCursor(utf8CodepointOffset(updated, cursorBytes));
    } else if (cursorBytes != originalCursorBytes) {
        edit->setTextCursor(utf8CodepointOffset(updated, cursorBytes));
    }
}

bool inputText(const char* label,
               char* buf,
               std::size_t bufSize,
               InputTextFlags flags,
               InputTextCallback callback,
               void* userData) {
    if (!gContext || !label || !buf || bufSize == 0) return false;
    auto& c = *gContext;
    const float width = c.layout().nextWidth > 0 ? c.layout().nextWidth : 0.0f;
    auto* rec = item(c, label, "inputText", "EditBox", "LegacyEditBoxSkin", width, frameHeight(), false);
    if (!rec) return false;
    auto* edit = rec->widget->castType<MyGUI::EditBox>(false);
    if (!edit) return false;
    const bool requestFocus = registerFocusableInput(
        c, rec, (flags & InputTextFlags_AutoSelectAll) != 0);
    edit->setFontName("Default");
    edit->setFontHeight(static_cast<int>(kDefaultFontSize));
    edit->setMaxTextLength(bufSize - 1);
    if (edit->getEditMultiLine()) edit->setEditMultiLine(false);
    edit->setEditPassword((flags & InputTextFlags_HiddenText) != 0);
    auto* manager = inputManager();
    bool focused = manager && manager->getKeyFocusWidget() &&
                   widgetIsDescendant(manager->getKeyFocusWidget(), edit);
    const std::string existing(buf);
    const bool bufferUpdated = existing != rec->inputBufferValue;
    if (bufferUpdated || (!focused && edit->getCaption().asUTF8() != existing)) {
        edit->setCaption(MyGUI::UString(existing));
        rec->inputValue = existing;
    }
    if (requestFocus) {
        focused = manager && manager->getKeyFocusWidget() &&
                  widgetIsDescendant(manager->getKeyFocusWidget(), edit);
    }
    if ((flags & InputTextFlags_AutoSelectAll) && requestFocus) {
        edit->setTextSelection(0, edit->getCaption().size());
    }
    if ((flags & InputTextFlags_CallbackCompletion) && focused && c.keyPressed[Key_Tab]) {
        invokeEditCallback(rec,
                           edit,
                           bufSize,
                           callback,
                           userData,
                           InputTextFlags_CallbackCompletion);
    }
    if (flags & InputTextFlags_CallbackAlways) {
        invokeEditCallback(rec,
                           edit,
                           bufSize,
                           callback,
                           userData,
                           InputTextFlags_CallbackAlways);
    }
    const auto& caption = edit->getCaption().asUTF8();
    const auto copied = std::min(caption.size(), bufSize - 1);
    std::memcpy(buf, caption.data(), copied);
    buf[copied] = '\0';
    rec->inputValue = buf;
    rec->inputBufferValue = buf;
    const bool changed = rec->changedFrame == c.frame || (focused && existing != rec->inputValue);
    if (changed) rec->changedFrame = c.frame;
    configureTextBox(c, edit, c.style.Colors[Col_Text], c.disabled);
    return (flags & InputTextFlags_EnterReturnsTrue) && c.keyPressed[Key_Enter] && focused ? true : changed;
}

bool inputTextMultiline(const char* label,
                        char* buf,
                        std::size_t bufSize,
                        Vec2 size,
                        InputTextFlags flags,
                        InputTextCallback callback,
                        void* userData,
                        MultilineEditInfo* outInfo) {
    if (!gContext || !label || !buf || bufSize == 0) return false;
    auto& c = *gContext;
    const auto avail = contentRegionAvail();
    const float width = size.x != 0 ? (size.x < 0 ? avail.x : size.x) : avail.x;
    const float height = size.y != 0 ? (size.y < 0 ? avail.y : size.y) : std::max(120.0f, avail.y);
    auto* rec = item(c, label, "multiline", "EditBox", "LegacyEditBoxSkin", width, height, false);
    if (!rec) return false;
    auto* edit = rec->widget->castType<MyGUI::EditBox>(false);
    if (!edit) return false;
    const bool requestFocus = registerFocusableInput(
        c, rec, (flags & InputTextFlags_AutoSelectAll) != 0);
    edit->setFontName("Default");
    edit->setFontHeight(static_cast<int>(kDefaultFontSize));
    edit->setMaxTextLength(bufSize - 1);
    if (!edit->getEditMultiLine()) edit->setEditMultiLine(true);
    edit->setEditWordWrap(false);
    edit->setVisibleVScroll(true);
    edit->setVisibleHScroll(true);
    const auto textColor = c.style.Colors[Col_Text];
    auto* manager = inputManager();
    bool focused = manager && manager->getKeyFocusWidget() &&
                   widgetIsDescendant(manager->getKeyFocusWidget(), edit);
    const std::string existing(buf);
    const bool bufferUpdated = existing != rec->inputBufferValue;
    if (bufferUpdated || (!focused && edit->getCaption().asUTF8() != existing)) {
        edit->setCaption(MyGUI::UString(existing));
        rec->inputValue = existing;
    }
    if (requestFocus) {
        focused = manager && manager->getKeyFocusWidget() &&
                  widgetIsDescendant(manager->getKeyFocusWidget(), edit);
    }
    if ((flags & InputTextFlags_CallbackCompletion) && focused && c.keyPressed[Key_Tab]) {
        invokeEditCallback(rec,
                           edit,
                           bufSize,
                           callback,
                           userData,
                           InputTextFlags_CallbackCompletion);
    }
    if (flags & InputTextFlags_CallbackAlways) {
        invokeEditCallback(rec,
                           edit,
                           bufSize,
                           callback,
                           userData,
                           InputTextFlags_CallbackAlways);
    }
    const auto& caption = edit->getCaption().asUTF8();
    const auto copied = std::min(caption.size(), bufSize - 1);
    std::memcpy(buf, caption.data(), copied);
    buf[copied] = '\0';
    rec->inputValue = buf;
    rec->inputBufferValue = buf;
    const bool changed = rec->changedFrame == c.frame || (focused && existing != rec->inputValue);
    if (outInfo) {
        outInfo->innerRect = rec->rect;
        outInfo->textOrigin = {rec->rect.x + c.style.FramePadding.x + 5.0f,
                               rec->rect.y + c.style.FramePadding.y + 3.0f};
        // MyGUI exposes signed view offsets through size_t getters. A transient
        // negative IntPoint therefore wraps to a huge size_t; discard values
        // outside the coordinate type's representable range before conversion.
        const auto checkedScrollOffset = [](std::size_t offset) {
            return offset <= static_cast<std::size_t>(std::numeric_limits<int>::max())
                       ? static_cast<float>(offset)
                       : 0.0f;
        };
        outInfo->scrollX = checkedScrollOffset(edit->getHScrollPosition());
        outInfo->scrollY = checkedScrollOffset(edit->getVScrollPosition());
        outInfo->active = focused;
        outInfo->drawList = c.currentWindow ? &c.currentWindow->drawList : &c.foreground;
    }
    configureTextBox(c, edit, textColor, c.disabled);
    return changed || ((flags & InputTextFlags_EnterReturnsTrue) && c.keyPressed[Key_Enter] && focused);
}

namespace {

bool editNumber(Context& c,
                const char* label,
                std::string suffix,
                float* value,
                bool isInt,
                float speed,
                float minimum,
                float maximum,
                bool hasMinimum,
                bool hasMaximum,
                const char* format,
                SliderFlags flags,
                float width,
                float height) {
    if (!value || !label) return false;
    auto* rec = getRecord(c,
                          widgetKey(c, label, suffix),
                          "EditBox",
                          "LegacyEditBoxSkin",
                          c.layout().parent,
                          widgetCoord(c.layout(), c.layout().cursorX + c.layout().indent,
                                      c.layout().cursorY, width, height));
    if (!rec) return false;
    const Rect rect = screenRectFor(c.layout(), c.layout().cursorX + c.layout().indent,
                                    c.layout().cursorY, width, height);
    setLastItem(c, rec, rect);
    auto* edit = rec->widget->castType<MyGUI::EditBox>(false);
    if (!edit) return false;
    edit->setFontName("Default");
    edit->setFontHeight(static_cast<int>(kDefaultFontSize));
    edit->setMaxTextLength(48);
    if (edit->getEditMultiLine()) edit->setEditMultiLine(false);
    const bool focused = inputManager() && inputManager()->getKeyFocusWidget() &&
                         widgetIsDescendant(inputManager()->getKeyFocusWidget(), edit);
    char number[64]{};
    std::snprintf(number, sizeof(number), format ? format : (isInt ? "%d" : "%.3f"),
                  isInt ? static_cast<int>(*value) : static_cast<double>(*value));
    // Variadic printf requires a type matching the conversion: normalize integer formats separately.
    if (isInt) std::snprintf(number, sizeof(number), format ? format : "%d", static_cast<int>(*value));
    else std::snprintf(number, sizeof(number), format ? format : "%.3f", static_cast<double>(*value));
    if (!focused && edit->getCaption().asUTF8() != number) {
        edit->setCaption(MyGUI::UString(number));
        rec->inputValue = number;
    }
    bool changed = rec->changedFrame == c.frame;
    if (focused) {
        const auto& caption = edit->getCaption().asUTF8();
        char* end = nullptr;
        const float parsed = std::strtof(caption.c_str(), &end);
        if (end != caption.c_str() && std::isfinite(parsed) && std::abs(parsed - *value) > 0.000001f) {
            *value = parsed;
            changed = true;
        }
    }
    if (itemHovered(c) && c.mousePressed[MouseButton_Left]) {
        rec->dragStartX = c.io.MousePos.x;
        rec->dragStartValue = *value;
        rec->dragging = false;
    }
    if (c.mouseDown[MouseButton_Left] && itemHovered(c)) {
        const float delta = c.io.MousePos.x - rec->dragStartX;
        if (std::abs(delta) > 3.0f) {
            rec->dragging = true;
            float next = rec->dragStartValue + delta * speed;
            if ((flags & SliderFlags_AlwaysClamp) || hasMinimum || hasMaximum) {
                if (hasMinimum) next = std::max(minimum, next);
                if (hasMaximum) next = std::min(maximum, next);
            }
            if (isInt) next = std::round(next);
            if (std::abs(next - *value) > .000001f) {
                *value = next;
                changed = true;
                char updated[64]{};
                if (isInt) std::snprintf(updated, sizeof(updated), format ? format : "%d", static_cast<int>(*value));
                else std::snprintf(updated, sizeof(updated), format ? format : "%.3f", static_cast<double>(*value));
                edit->setCaption(MyGUI::UString(updated));
            }
        }
    }
    if (c.mouseReleased[MouseButton_Left]) rec->dragging = false;
    configureTextBox(c, edit, c.style.Colors[Col_Text], c.disabled);
    return changed && !c.disabled;
}

bool numericWidget(const char* label,
                   float* values,
                   int components,
                   float speed,
                   const float* mins,
                   const float* maxs,
                   const char* format,
                   SliderFlags flags,
                   bool ints) {
    if (!gContext || !label || !values || components <= 0) return false;
    auto& c = *gContext;
    auto& layout = c.layout();
    const std::string title(visibleLabel(label));
    const float fullWidth = layout.nextWidth > 0
                                ? std::exchange(layout.nextWidth, 0.0f)
                                : std::max(1.0f,
                                           remainingWidth(layout,
                                                          c.style.WindowPadding.x,
                                                          true));
    const float labelWidth = title.empty() ? 0.0f : std::min(92.0f, measuredWidth(c, title) + 10.0f);
    const float spacing = c.style.ItemSpacing.x;
    const float eachWidth = std::max(36.0f,
                                     (fullWidth - labelWidth - spacing * std::max(0, components - 1)) /
                                         std::max(1, components));
    const float height = frameHeight();
    const float rowY = layout.cursorY;
    const float startX = layout.cursorX + layout.indent;
    bool changed = false;
    if (!title.empty()) {
        const auto labelKey = widgetKey(c, label, "numeric_label");
        auto* labelRec = getRecord(c,
                                   labelKey,
                                   "TextBox",
                                   "TextBoxSkin",
                                   layout.parent,
                                   widgetCoord(layout, startX, rowY, labelWidth, height));
        setWidgetText(labelRec, title);
        configureTextBox(c, labelRec ? labelRec->widget : nullptr, c.style.Colors[Col_Text], c.disabled);
        setLastItem(c, labelRec, screenRectFor(layout, startX, rowY, labelWidth, height));
    }
    for (int i = 0; i < components; ++i) {
        if (i) {
            const float x = startX + labelWidth + i * (eachWidth + spacing);
            const auto oldX = layout.cursorX;
            layout.cursorX = x - layout.indent;
            layout.cursorY = rowY;
            changed |= editNumber(c,
                                  label,
                                  "component" + std::to_string(i),
                                  values + i,
                                  ints,
                                  speed,
                                  mins ? mins[i] : 0.0f,
                                  maxs ? maxs[i] : 0.0f,
                                  mins != nullptr,
                                  maxs != nullptr,
                                  format,
                                  flags,
                                  eachWidth,
                                  height);
            layout.cursorX = oldX;
        } else {
            const float x = startX + labelWidth;
            const auto oldX = layout.cursorX;
            layout.cursorX = x - layout.indent;
            layout.cursorY = rowY;
            changed |= editNumber(c,
                                  label,
                                  components == 1 ? "number" : "component0",
                                  values,
                                  ints,
                                  speed,
                                  mins ? mins[0] : 0.0f,
                                  maxs ? maxs[0] : 0.0f,
                                  mins != nullptr,
                                  maxs != nullptr,
                                  format,
                                  flags,
                                  eachWidth,
                                  height);
            layout.cursorX = oldX;
        }
    }
    layout.cursorX = layout.lineStartX;
    layout.cursorY = rowY + height + c.style.ItemSpacing.y;
    layout.lineHeight = height;
    return changed;
}

bool sliderFloatImpl(const char* label,
                     float* value,
                     float minimum,
                     float maximum,
                     const char* format,
                     bool integer) {
    if (!gContext || !label || !value || maximum <= minimum) return false;
    auto& c = *gContext;
    auto& layout = c.layout();
    const float width = layout.nextWidth > 0
                            ? std::exchange(layout.nextWidth, 0.0f)
                            : std::max(1.0f,
                                       remainingWidth(layout,
                                                      c.style.WindowPadding.x,
                                                      true));
    const float height = frameHeight();
    const float x = layout.cursorX + layout.indent;
    const float y = layout.cursorY;
    auto* rec = getRecord(c, widgetKey(c, label, integer ? "slider_int" : "slider_float"),
                          "Button", "LegacyFrameSkin", layout.parent,
                          widgetCoord(layout, x, y, width, height));
    if (!rec) return false;
    setLastItem(c, rec, screenRectFor(layout, x, y, width, height));
    const float labelWidth = std::min(110.0f, measuredWidth(c, visibleLabel(label)) + 8.0f);
    const float trackX = rec->rect.x + labelWidth;
    const float trackWidth = std::max(20.0f, width - labelWidth - 8.0f);
    auto* draw = c.currentWindow ? &c.currentWindow->drawList : &c.foreground;
    const float normalized = std::clamp((*value - minimum) / (maximum - minimum), 0.0f, 1.0f);
    draw->addRectFilled({trackX, rec->rect.y + height * .36f},
                        {trackX + trackWidth, rec->rect.y + height * .64f},
                        colorU32(Col_FrameBg));
    draw->addRectFilled({trackX, rec->rect.y + height * .36f},
                        {trackX + trackWidth * normalized, rec->rect.y + height * .64f},
                        colorU32(Col_SliderGrab));
    draw->addText({rec->rect.x + 4, rec->rect.y + 2}, colorU32(Col_Text), std::string(visibleLabel(label)).c_str());
    char display[64]{};
    if (integer) std::snprintf(display, sizeof(display), format ? format : "%d", static_cast<int>(std::lround(*value)));
    else std::snprintf(display, sizeof(display), format ? format : "%.3f", static_cast<double>(*value));
    draw->addText({trackX + trackWidth + 4, rec->rect.y + 2}, colorU32(Col_Text), display);
    bool changed = false;
    if ((rec->clickFrame == c.frame || (c.mouseDown[MouseButton_Left] && hoveredWidget(c, rec->widget))) && !c.disabled) {
        const float t = std::clamp((c.io.MousePos.x - trackX) / trackWidth, 0.0f, 1.0f);
        float next = minimum + (maximum - minimum) * t;
        if (integer) next = std::round(next);
        if (next != *value) {
            *value = next;
            changed = true;
        }
    }
    layout.cursorX = layout.lineStartX;
    layout.cursorY += height + c.style.ItemSpacing.y;
    return changed;
}

} // namespace

bool dragScalarNFloat(const char* label,
                      float* v,
                      int components,
                      float speed,
                      const float* vMin,
                      const float* vMax,
                      const char* format,
                      SliderFlags flags) {
    return numericWidget(label, v, components, speed, vMin, vMax, format, flags, false);
}

bool dragFloat(const char* label, float* v, float speed, float vMin, float vMax, const char* format, SliderFlags flags) {
    const float* lo = (vMin == 0.0f && vMax == 0.0f) ? nullptr : &vMin;
    const float* hi = (vMin == 0.0f && vMax == 0.0f) ? nullptr : &vMax;
    return numericWidget(label, v, 1, speed, lo, hi, format, flags, false);
}

bool dragFloat2(const char* label, float v[2], float speed, float vMin, float vMax, const char* format, SliderFlags flags) {
    const float mins[2]{vMin, vMin}, maxs[2]{vMax, vMax};
    return numericWidget(label, v, 2, speed, (vMin == 0 && vMax == 0) ? nullptr : mins,
                         (vMin == 0 && vMax == 0) ? nullptr : maxs, format, flags, false);
}

bool dragFloat3(const char* label, float v[3], float speed, float vMin, float vMax, const char* format, SliderFlags flags) {
    const float mins[3]{vMin, vMin, vMin}, maxs[3]{vMax, vMax, vMax};
    return numericWidget(label, v, 3, speed, (vMin == 0 && vMax == 0) ? nullptr : mins,
                         (vMin == 0 && vMax == 0) ? nullptr : maxs, format, flags, false);
}

bool dragFloat4(const char* label, float v[4], float speed, float vMin, float vMax, const char* format, SliderFlags flags) {
    const float mins[4]{vMin, vMin, vMin, vMin}, maxs[4]{vMax, vMax, vMax, vMax};
    return numericWidget(label, v, 4, speed, (vMin == 0 && vMax == 0) ? nullptr : mins,
                         (vMin == 0 && vMax == 0) ? nullptr : maxs, format, flags, false);
}

bool dragInt(const char* label, int* v, float speed, int vMin, int vMax, const char* format, SliderFlags flags) {
    if (!v) return false;
    float value = static_cast<float>(*v);
    const float min = static_cast<float>(vMin), max = static_cast<float>(vMax);
    const bool changed = numericWidget(label, &value, 1, speed,
                                       (vMin == 0 && vMax == 0) ? nullptr : &min,
                                       (vMin == 0 && vMax == 0) ? nullptr : &max,
                                       format ? format : "%d", flags, true);
    if (changed) *v = static_cast<int>(std::lround(value));
    return changed;
}

bool sliderFloat(const char* label, float* v, float vMin, float vMax, const char* format, SliderFlags) {
    return sliderFloatImpl(label, v, vMin, vMax, format, false);
}

bool sliderInt(const char* label, int* v, int vMin, int vMax, const char* format) {
    if (!v) return false;
    float value = static_cast<float>(*v);
    const bool changed = sliderFloatImpl(label, &value, static_cast<float>(vMin), static_cast<float>(vMax), format, true);
    if (changed) *v = static_cast<int>(std::lround(value));
    return changed;
}

bool inputFloat(const char* label, float* v, float step, float stepFast, const char* format) {
    const bool changed = dragFloat(label, v, step > 0 ? step : .01f, 0, 0, format, SliderFlags_None);
    if (!gContext || !v || !isItemActive()) return changed;
    if (gContext->keyPressed[Key_UpArrow]) { *v += gContext->io.KeyShift ? (stepFast > 0 ? stepFast : step) : (step > 0 ? step : .01f); return true; }
    if (gContext->keyPressed[Key_DownArrow]) { *v -= gContext->io.KeyShift ? (stepFast > 0 ? stepFast : step) : (step > 0 ? step : .01f); return true; }
    return changed;
}

bool inputInt(const char* label, int* v, int step, int stepFast) {
    if (!v) return false;
    const bool changed = dragInt(label, v, 0.0f, 0, 0, "%d", SliderFlags_None);
    if (!gContext || !isItemActive()) return changed;
    if (gContext->keyPressed[Key_UpArrow]) { *v += gContext->io.KeyShift ? stepFast : step; return true; }
    if (gContext->keyPressed[Key_DownArrow]) { *v -= gContext->io.KeyShift ? stepFast : step; return true; }
    return changed;
}

bool colorEdit3(const char* label, float color[3]) {
    if (!gContext || !label || !color) return false;
    const Vec2 oldCursor = cursorPos();
    auto* draw = windowDrawList();
    const auto size = frameHeight();
    const auto x = cursorScreenPos().x;
    const auto y = cursorScreenPos().y;
    draw->addRectFilled({x, y}, {x + size, y + size}, floatColor(color[0], color[1], color[2]));
    setCursorPosX(cursorPosX() + size + style().ItemSpacing.x);
    const bool changed = dragFloat3(label, color, .005f, 0, 0, "%.3f");
    setCursorPos({oldCursor.x, std::max(cursorPos().y, oldCursor.y + size + style().ItemSpacing.y)});
    return changed;
}

void image(unsigned glTexture, Vec2 size, Vec2 uv0, Vec2 uv1) {
    if (!gContext || glTexture == 0) return;
    auto& c = *gContext;
    auto& l = c.layout();
    const float width = size.x > 0 ? size.x
                                   : std::max(1.0f, remainingWidth(l, 0.0f, true));
    const float height = size.y > 0 ? size.y : std::max(1.0f, width * .6f);
    auto* rec = getRecord(c,
                          widgetKey(c,
                                    "##image",
                                    std::to_string(c.frame) + "/" + std::to_string(l.cursorY) + "/" +
                                        std::to_string(l.cursorX)),
                          "ImageBox", "ImageBox", l.parent,
                          widgetCoord(l, l.cursorX + l.indent, l.cursorY, width, height));
    if (rec && c.platform) {
        auto* tex = c.platform->renderManager().wrapExternalTexture(glTexture, 1, 1, false);
        if (auto* box = rec->widget->castType<MyGUI::ImageBox>(false)) {
            box->setImageTexture(tex->getName());
            const int tw = std::max(1, tex->getWidth()), th = std::max(1, tex->getHeight());
            box->setImageRect(MyGUI::IntRect(clampInt(uv0.x * tw), clampInt(uv0.y * th),
                                             clampInt(uv1.x * tw), clampInt(uv1.y * th)));
        }
    }
    setLastItem(c, rec, screenRectFor(l, l.cursorX + l.indent, l.cursorY, width, height));
    finishLine(c, width, height);
}

void progressBar(float fraction, Vec2 size, const char* overlay) {
    if (!gContext) return;
    auto& c = *gContext;
    auto& l = c.layout();
    const float width = size.x > 0 ? size.x
                                   : (size.x < 0 ? remainingWidth(l, 0.0f, true) : 160.0f);
    const float height = size.y > 0 ? size.y : frameHeight() * .72f;
    const float x = l.cursorX + l.indent;
    const float y = l.cursorY;
    const auto rect = screenRectFor(l, x, y, width, height);
    auto* draw = c.currentWindow ? &c.currentWindow->drawList : &c.foreground;
    draw->addRectFilled({rect.x, rect.y}, {rect.x + width, rect.y + height}, colorU32(Col_FrameBg), style().FrameRounding);
    draw->addRectFilled({rect.x, rect.y}, {rect.x + width * std::clamp(fraction, 0.0f, 1.0f), rect.y + height}, colorU32(Col_CheckMark), style().FrameRounding);
    if (overlay) draw->addText({rect.x + 4, rect.y + 1}, colorU32(Col_Text), overlay);
    updateTableRowHeight(c, l, height);
    l.maxContentX = std::max(l.maxContentX, l.cursorX + l.indent + width);
    l.cursorX = l.lineStartX;
    l.cursorY += height + c.style.ItemSpacing.y;
}

void plotLines(const char* label,
              const float* values,
              int valuesCount,
              int valuesOffset,
              const char* overlayText,
              float scaleMin,
              float scaleMax,
              Vec2 graphSize) {
    if (!gContext || !values || valuesCount <= 0) return;
    auto& c = *gContext;
    auto& l = c.layout();
    const float width = graphSize.x > 0 ? graphSize.x
                                         : std::max(1.0f, remainingWidth(l, 0.0f, true));
    const float height = graphSize.y > 0 ? graphSize.y : 80.0f;
    const float x = l.screen.x + l.cursorX + l.indent - l.scrollX;
    const float y = l.screen.y + l.cursorY - l.scrollY;
    auto* draw = c.currentWindow ? &c.currentWindow->drawList : &c.foreground;
    draw->addRectFilled({x, y}, {x + width, y + height}, colorU32(Col_FrameBg));
    if (scaleMax <= scaleMin) {
        scaleMin = values[0]; scaleMax = values[0];
        for (int i = 1; i < valuesCount; ++i) { scaleMin = std::min(scaleMin, values[i]); scaleMax = std::max(scaleMax, values[i]); }
        if (scaleMax <= scaleMin) scaleMax = scaleMin + 1.0f;
    }
    const int count = std::min(valuesCount, static_cast<int>(width));
    for (int i = 1; i < count; ++i) {
        const int aIndex = (valuesOffset + (i - 1) * valuesCount / count) % valuesCount;
        const int bIndex = (valuesOffset + i * valuesCount / count) % valuesCount;
        const float ay = y + height - std::clamp((values[aIndex] - scaleMin) / (scaleMax - scaleMin), 0.0f, 1.0f) * height;
        const float by = y + height - std::clamp((values[bIndex] - scaleMin) / (scaleMax - scaleMin), 0.0f, 1.0f) * height;
        draw->addLine({x + (i - 1) * width / count, ay}, {x + i * width / count, by}, colorU32(Col_CheckMark), 1.3f);
    }
    if (overlayText) draw->addText({x + 4, y + 2}, colorU32(Col_Text), overlayText);
    updateTableRowHeight(c, l, height);
    l.maxContentX = std::max(l.maxContentX, l.cursorX + l.indent + width);
    l.cursorX = l.lineStartX;
    l.cursorY += height + c.style.ItemSpacing.y;
    (void)label;
}

void plotHistogram(const char* label,
                   const float* values,
                   int valuesCount,
                   int valuesOffset,
                   const char* overlayText,
                   float scaleMin,
                   float scaleMax,
                   Vec2 graphSize) {
    if (!gContext || !values || valuesCount <= 0) return;
    auto& c = *gContext;
    auto& l = c.layout();
    const float width = graphSize.x > 0 ? graphSize.x
                                         : std::max(1.0f, remainingWidth(l, 0.0f, true));
    const float height = graphSize.y > 0 ? graphSize.y : 80.0f;
    const float x = l.screen.x + l.cursorX + l.indent - l.scrollX;
    const float y = l.screen.y + l.cursorY - l.scrollY;
    auto* draw = c.currentWindow ? &c.currentWindow->drawList : &c.foreground;
    draw->addRectFilled({x, y}, {x + width, y + height}, colorU32(Col_FrameBg));
    if (scaleMax <= scaleMin) {
        scaleMin = 0.0f; scaleMax = 1.0f;
        for (int i = 0; i < valuesCount; ++i) { scaleMin = std::min(scaleMin, values[i]); scaleMax = std::max(scaleMax, values[i]); }
        if (scaleMax <= scaleMin) scaleMax = scaleMin + 1.0f;
    }
    const float bar = width / valuesCount;
    for (int i = 0; i < valuesCount; ++i) {
        const float value = values[(valuesOffset + i) % valuesCount];
        const float amount = std::clamp((value - scaleMin) / (scaleMax - scaleMin), 0.0f, 1.0f);
        draw->addRectFilled({x + i * bar, y + height * (1.0f - amount)},
                            {x + (i + 1) * bar - 1, y + height},
                            colorU32(Col_CheckMark));
    }
    if (overlayText) draw->addText({x + 4, y + 2}, colorU32(Col_Text), overlayText);
    updateTableRowHeight(c, l, height);
    l.maxContentX = std::max(l.maxContentX, l.cursorX + l.indent + width);
    l.cursorX = l.lineStartX;
    l.cursorY += height + c.style.ItemSpacing.y;
    (void)label;
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

bool beginTable(const char* strId, int columns, TableFlags flags, Vec2 outerSize) {
    if (!gContext || !strId || columns <= 0) return false;
    auto& c = *gContext;
    auto& parent = c.layout();
    TableState table;
    table.id = widgetKey(c, strId, "table");
    table.flags = flags;
    table.columns = columns;
    table.saved = parent;
    table.x = parent.cursorX + parent.indent;
    table.y = parent.cursorY;
    table.width = outerSize.x > 0 ? outerSize.x
                                 : std::max(1.0f,
                                            remainingWidth(parent,
                                                           c.style.WindowPadding.x,
                                                           true));
    table.height = outerSize.y > 0 ? outerSize.y
                                   : std::max(1.0f, parent.height - parent.cursorY);
    table.widths.assign(static_cast<std::size_t>(columns), table.width / columns);
    table.defaultWidths = table.widths;
    table.stretchWeights.assign(static_cast<std::size_t>(columns), 1.0f);
    table.fixedWidths.assign(static_cast<std::size_t>(columns), false);
    table.headers.resize(static_cast<std::size_t>(columns));
    if (const auto state = c.scrollStates.find(table.id); state != c.scrollStates.end()) {
        table.scrollX = state->second.x;
        table.scrollY = state->second.y;
        table.scrollMaxX = state->second.maxX;
        table.scrollMaxY = state->second.maxY;
    }
    auto* root = ensureRawWidget(c,
                                 table.id,
                                 "Widget",
                                 "PanelEmpty",
                                 parent.parent,
                                 widgetCoord(parent, table.x, table.y, table.width, table.height));
    table.root = root;
    if (!root) return false;
    table.bounds = screenRectFor(parent, table.x, table.y, table.width, table.height);
    updateTableClipRegions(c, table);
    setLastItem(c, recordForWidget(c, root), table.bounds);
    parent.maxContentX = std::max(parent.maxContentX, table.x + table.width);
    LayoutScope tableScope;
    tableScope.parent = root;
    tableScope.window = parent.window;
    tableScope.screen = table.bounds;
    tableScope.width = table.width;
    tableScope.height = table.height;
    tableScope.cursorX = tableScope.lineStartX = 0;
    tableScope.cursorY = 0;
    tableScope.id = table.id;
    const float parentBottom = std::min(c.io.DisplaySize.y, parent.screen.y + parent.height);
    c.layouts.push_back(std::move(tableScope));
    auto* draw = c.currentWindow ? &c.currentWindow->drawList : &c.foreground;
    const auto& bounds = c.layouts.back().screen;
    const float clipHeight = (flags & TableFlags_ScrollY)
                                 ? table.height
                                 : std::max(1.0f, parentBottom - bounds.y);
    draw->pushClipRect({bounds.x, bounds.y},
                       {bounds.x + bounds.width, bounds.y + clipHeight},
                       true);
    c.background.pushClipRect({bounds.x, bounds.y},
                              {bounds.x + bounds.width, bounds.y + clipHeight},
                              true);
    if (flags & TableFlags_Borders) {
        draw->addRect({bounds.x, bounds.y},
                      {bounds.x + table.width, bounds.y + table.height},
                      colorU32(Col_Border));
    }
    c.tables.push_back(std::move(table));
    return true;
}

void endTable() {
    if (!gContext || gContext->tables.empty() || gContext->layouts.size() < 2) return;
    auto& c = *gContext;
    auto& table = c.tables.back();
    auto* draw = c.currentWindow ? &c.currentWindow->drawList : &c.foreground;
    if (table.cellClipPushed) {
        draw->popClipRect();
        table.cellClipPushed = false;
    }
    const int rowCount = table.rowStarted ? table.row + 1 : 0;
    const float contentWidth = std::accumulate(table.widths.begin(), table.widths.end(), 0.0f);
    const std::size_t measuredRows = std::min(static_cast<std::size_t>(rowCount),
                                              table.rowHeights.size());
    const float contentHeight = std::accumulate(table.rowHeights.begin(),
                                                table.rowHeights.begin() + measuredRows,
                                                0.0f);
    if (table.flags & TableFlags_RowBg) {
        float rowY = 0.0f;
        for (int row = 0; row < rowCount; ++row) {
            const float height = table.rowHeights[static_cast<std::size_t>(row)];
            if (row > 0 && (row & 1)) {
                const float scrollOffset = row >= table.freezeRows ? table.scrollY : 0.0f;
                c.background.addRectFilled(
                    {table.bounds.x, table.bounds.y + rowY - scrollOffset},
                    {table.bounds.x + table.width,
                     table.bounds.y + rowY - scrollOffset + height},
                    colorU32(Col_ChildBg));
            }
            rowY += height;
        }
    }
    const float headerHeight = rowCount > 0 ? table.rowHeights[0] : frameHeight();
    if (table.flags & TableFlags_Resizable) {
        if (c.mousePressed[MouseButton_Left] && c.mouseDown[MouseButton_Left] &&
            c.io.MousePos.y >= table.bounds.y &&
            c.io.MousePos.y <= table.bounds.y + headerHeight) {
            float boundaryOffset = 0.0f;
            for (int column = 0; column + 1 < table.columns; ++column) {
                boundaryOffset += table.widths[static_cast<std::size_t>(column)];
                const float scrollOffset = column + 1 >= table.freezeColumns ? table.scrollX : 0.0f;
                const float boundaryX = table.bounds.x + boundaryOffset - scrollOffset;
                if (std::abs(c.io.MousePos.x - boundaryX) <= 5.0f) {
                    c.resizingTable = table.id;
                    c.resizingTableColumn = column;
                    c.resizeStartX = c.io.MousePos.x;
                    c.resizeStartLeftWidth = table.widths[static_cast<std::size_t>(column)];
                    c.resizeStartRightWidth = table.widths[static_cast<std::size_t>(column + 1)];
                    break;
                }
            }
        }

        const bool activeResize = c.resizingTable == table.id &&
                                  c.resizingTableColumn >= 0 &&
                                  c.resizingTableColumn + 1 < table.columns;
        if (activeResize &&
            (c.mouseDown[MouseButton_Left] || c.mouseReleased[MouseButton_Left])) {
            const auto left = static_cast<std::size_t>(c.resizingTableColumn);
            const auto right = left + 1;
            const float minWidth =
                std::min(20.0f, (c.resizeStartLeftWidth + c.resizeStartRightWidth) * 0.5f);
            const float delta = std::clamp(c.io.MousePos.x - c.resizeStartX,
                                           minWidth - c.resizeStartLeftWidth,
                                           c.resizeStartRightWidth - minWidth);
            table.widths[left] = c.resizeStartLeftWidth + delta;
            table.widths[right] = c.resizeStartRightWidth - delta;
            auto& offsets = c.tableColumnWidthOffsets[table.id];
            if (offsets.size() != table.widths.size()) {
                offsets.assign(table.widths.size(), 0.0f);
            }
            offsets[left] = table.widths[left] - table.defaultWidths[left];
            offsets[right] = table.widths[right] - table.defaultWidths[right];
        }

        float boundaryOffset = 0.0f;
        for (int column = 0; column + 1 < table.columns; ++column) {
            boundaryOffset += table.widths[static_cast<std::size_t>(column)];
            const float scrollOffset = column + 1 >= table.freezeColumns ? table.scrollX : 0.0f;
            const float boundaryX = table.bounds.x + boundaryOffset - scrollOffset;
            const bool hovered = std::abs(c.io.MousePos.x - boundaryX) <= 5.0f &&
                                 c.io.MousePos.y >= table.bounds.y &&
                                 c.io.MousePos.y <= table.bounds.y + headerHeight;
            const bool active = c.resizingTable == table.id &&
                                c.resizingTableColumn == column;
            if (hovered || active) {
                const auto role = active ? Col_SeparatorActive : Col_SeparatorHovered;
                draw->addLine({boundaryX, table.bounds.y},
                              {boundaryX, table.bounds.y + headerHeight},
                              colorU32(role),
                              2.0f);
            }
        }
        if (activeResize && !c.mouseDown[MouseButton_Left]) {
            c.resizingTable.clear();
            c.resizingTableColumn = -1;
        }
    }
    table.scrollMaxX = (table.flags & TableFlags_ScrollX)
                           ? std::max(0.0f, contentWidth - table.width)
                           : 0.0f;
    table.scrollMaxY = (table.flags & TableFlags_ScrollY)
                           ? std::max(0.0f, contentHeight - table.height)
                           : 0.0f;
    table.scrollX = std::clamp(table.scrollX, 0.0f, table.scrollMaxX);
    table.scrollY = std::clamp(table.scrollY, 0.0f, table.scrollMaxY);

    auto* manager = inputManager();
    auto* focus = manager ? manager->getMouseFocusWidget() : nullptr;
    const bool inside = Rect{table.bounds.x, table.bounds.y, table.width, table.height}
                            .contains(c.io.MousePos.x, c.io.MousePos.y) &&
                        widgetIsDescendant(focus, table.root);
    if (!c.mouseWheelConsumed && inside) {
        float horizontal = c.io.MouseWheelH;
        if (c.io.KeyShift && (table.flags & TableFlags_ScrollX)) {
            horizontal += c.io.MouseWheel;
        }
        if ((table.flags & TableFlags_ScrollX) && horizontal != 0.0f) {
            const float old = table.scrollX;
            table.scrollX = std::clamp(table.scrollX - horizontal * 30.0f,
                                       0.0f,
                                       table.scrollMaxX);
            c.mouseWheelConsumed = table.scrollX != old;
        }
        if (!c.mouseWheelConsumed && (table.flags & TableFlags_ScrollY) &&
            c.io.MouseWheel != 0.0f && !(c.io.KeyShift && (table.flags & TableFlags_ScrollX))) {
            const float old = table.scrollY;
            table.scrollY = std::clamp(table.scrollY - c.io.MouseWheel * 30.0f,
                                       0.0f,
                                       table.scrollMaxY);
            c.mouseWheelConsumed = table.scrollY != old;
        }
    }
    auto& saved = c.scrollStates[table.id];
    saved.x = table.scrollX;
    saved.y = table.scrollY;
    saved.maxX = table.scrollMaxX;
    saved.maxY = table.scrollMaxY;

    if (!(table.flags & TableFlags_ScrollY) && table.root) {
        table.root->setSize(MyGUI::IntSize(clampInt(table.width),
                                           std::max(1, clampInt(std::max(table.height,
                                                                        contentHeight)))));
    }
    updateTableClipRegions(c, table);
    draw->popClipRect();
    c.background.popClipRect();
    const TableState completed = std::move(table);
    c.tables.pop_back();
    c.layouts.pop_back();
    auto& parent = c.layout();
    const float hostHeight = (completed.flags & TableFlags_ScrollY)
                                 ? completed.height
                                 : std::max(completed.height, contentHeight);
    parent.cursorX = parent.lineStartX;
    parent.cursorY = completed.y + hostHeight + c.style.ItemSpacing.y;
    parent.lineHeight = hostHeight;
}

void tableSetupColumn(const char* label, TableColumnFlags flags, float initWidth) {
    if (!gContext || gContext->tables.empty()) return;
    auto& c = *gContext;
    auto& table = c.tables.back();
    if (table.setupColumn >= table.columns) return;
    const auto index = static_cast<std::size_t>(table.setupColumn++);
    if (index < table.headers.size()) table.headers[index] = label ? label : "";
    const bool fixed = (flags & TableColumnFlags_WidthFixed) != 0;
    const bool stretch = (flags & TableColumnFlags_WidthStretch) != 0 ||
                         (table.flags &
                          (TableFlags_SizingStretchProp | TableFlags_SizingStretchSame));
    table.fixedWidths[index] = fixed;
    if (fixed) {
        table.widths[index] = std::max(20.0f, initWidth);
    } else if (stretch) {
        table.widths[index] = 0.0f;
        table.stretchWeights[index] = initWidth > 0.0f ? initWidth : 1.0f;
    } else {
        table.widths[index] = table.width / table.columns;
        table.stretchWeights[index] = 0.0f;
    }

    if (table.setupColumn == table.columns) {
        float fixedWidth = 0.0f;
        float totalWeight = 0.0f;
        int flexibleCount = 0;
        for (std::size_t i = 0; i < table.widths.size(); ++i) {
            if (table.fixedWidths[i]) {
                fixedWidth += table.widths[i];
            } else if (table.widths[i] <= 0.0f) {
                totalWeight += table.stretchWeights[i];
                ++flexibleCount;
            }
        }
        const float remaining = std::max(0.0f, table.width - fixedWidth);
        if (totalWeight > 0.0f) {
            const bool stretchSame = (table.flags & TableFlags_SizingStretchSame) != 0;
            const float denominator = stretchSame ? static_cast<float>(flexibleCount) : totalWeight;
            for (std::size_t i = 0; i < table.widths.size(); ++i) {
                if (!table.fixedWidths[i] && table.widths[i] <= 0.0f) {
                    const float weight = stretchSame ? 1.0f : table.stretchWeights[i];
                    table.widths[i] = denominator > 0.0f
                                          ? remaining * weight / denominator
                                          : table.width / table.columns;
                }
            }
        }
        table.defaultWidths = table.widths;
        if (table.flags & TableFlags_Resizable) {
            const auto offsets = c.tableColumnWidthOffsets.find(table.id);
            if (offsets != c.tableColumnWidthOffsets.end() &&
                offsets->second.size() == table.widths.size()) {
                for (std::size_t i = 0; i < table.widths.size(); ++i) {
                    table.widths[i] = std::max(20.0f, table.widths[i] + offsets->second[i]);
                }
            }
        }
    }
}

void tableSetupScrollFreeze(int cols, int rows) {
    if (!gContext || gContext->tables.empty()) return;
    auto& c = *gContext;
    auto& table = c.tables.back();
    table.freezeColumns = std::clamp(cols, 0, table.columns);
    table.freezeRows = std::max(0, rows);
    updateTableClipRegions(c, table);
}

void tableHeadersRow() {
    if (!gContext || gContext->tables.empty()) return;
    auto& c = *gContext;
    auto& table = c.tables.back();
    table.rowHeight = frameHeight();
    table.row = 0;
    table.rowStarted = true;
    if (table.rowHeights.empty()) table.rowHeights.push_back(table.rowHeight);
    else table.rowHeights[0] = std::max(table.rowHeights[0], table.rowHeight);
    table.column = 0;
    for (int col = 0; col < table.columns; ++col) {
        addTableCellLayout(c, table, col);
        auto* draw = c.currentWindow ? &c.currentWindow->drawList : &c.foreground;
        const auto& layout = c.layout();
        draw->addRectFilled({layout.screen.x + layout.cursorX,
                             layout.screen.y + layout.cursorY},
                            {layout.screen.x + layout.cursorX + table.widths[col],
                             layout.screen.y + layout.cursorY + table.rowHeight},
                            colorU32(Col_Header));
        textUnformatted(table.headers[static_cast<std::size_t>(col)].c_str());
    }
    table.column = 0;
}

void tableNextRow(float minRowHeight) {
    if (!gContext || gContext->tables.empty()) return;
    auto& table = gContext->tables.back();
    if (table.rowStarted) ++table.row;
    else {
        table.row = 0;
        table.rowStarted = true;
    }
    table.column = 0;
    table.rowHeight = std::max({frameHeight(), 24.0f, minRowHeight});
    if (table.rowHeights.size() <= static_cast<std::size_t>(table.row)) {
        table.rowHeights.resize(static_cast<std::size_t>(table.row) + 1, table.rowHeight);
    } else {
        table.rowHeights[static_cast<std::size_t>(table.row)] =
            std::max(table.rowHeights[static_cast<std::size_t>(table.row)], table.rowHeight);
    }
}

bool tableNextColumn() {
    if (!gContext || gContext->tables.empty()) return false;
    auto& table = gContext->tables.back();
    if (!table.rowStarted) {
        table.row = 0;
        table.rowStarted = true;
        table.rowHeight = std::max(frameHeight(), 24.0f);
    }
    if (table.column >= table.columns) {
        table.column = 0;
        ++table.row;
        table.rowHeight = std::max(frameHeight(), 24.0f);
    }
    if (table.rowHeights.size() <= static_cast<std::size_t>(table.row)) {
        table.rowHeights.resize(static_cast<std::size_t>(table.row) + 1, table.rowHeight);
    }
    const int column = table.column++;
    addTableCellLayout(*gContext, table, column);
    return true;
}

bool tableSetColumnIndex(int col) {
    if (!gContext || gContext->tables.empty()) return false;
    auto& table = gContext->tables.back();
    if (col < 0 || col >= table.columns) return false;
    if (!table.rowStarted) {
        table.row = 0;
        table.rowStarted = true;
        table.rowHeight = std::max(frameHeight(), 24.0f);
        table.rowHeights.resize(1, table.rowHeight);
    }
    if (table.rowHeights.size() <= static_cast<std::size_t>(table.row)) {
        table.rowHeights.resize(static_cast<std::size_t>(table.row) + 1, table.rowHeight);
    }
    table.column = col;
    addTableCellLayout(*gContext, table, col);
    return true;
}

// ---------------------------------------------------------------------------
// Drag and drop
// ---------------------------------------------------------------------------

bool beginDragDropSource() {
    if (!gContext || !gContext->lastItemRecord) return false;
    auto& c = *gContext;
    auto* item = c.lastItemRecord;
    if (!c.mouseDown[MouseButton_Left] || !itemHovered(c)) return false;
    const float dx = c.io.MousePos.x - c.pressPosition[MouseButton_Left].x;
    const float dy = c.io.MousePos.y - c.pressPosition[MouseButton_Left].y;
    if (dx * dx + dy * dy < 25.0f) return false;
    c.dragSource = item->key;
    return true;
}

bool setDragDropPayload(const char* type, const void* data, std::size_t size) {
    if (!gContext || !type) return false;
    auto& c = *gContext;
    c.payload.type = type;
    c.payload.bytes.resize(size);
    if (data && size) std::memcpy(c.payload.bytes.data(), data, size);
    c.payloadFrame = c.frame;
    c.payloadPending = true;
    return true;
}

void endDragDropSource() {}

bool beginDragDropTarget() {
    if (!gContext || !gContext->lastItem) return false;
    auto& c = *gContext;
    if (!itemHovered(c)) return false;
    c.dragTarget = c.lastItemRecord ? c.lastItemRecord->key : "target";
    return true;
}

bool beginDragDropTargetCustom(Rect rect, uint64_t id) {
    if (!gContext) return false;
    auto& c = *gContext;
    if (!rect.contains(c.io.MousePos.x, c.io.MousePos.y)) return false;
    c.dragTarget = std::to_string(id);
    return true;
}

const Payload* acceptDragDropPayload(const char* type) {
    if (!gContext || !type) return nullptr;
    auto& c = *gContext;
    if (!c.payloadDropped || c.payload.type != type || c.dragTarget.empty()) return nullptr;
    c.payloadAccepted = true;
    c.payloadDropped = false;
    c.payloadPending = false;
    return &c.payload;
}

void endDragDropTarget() {
    if (gContext) gContext->dragTarget.clear();
}

// ---------------------------------------------------------------------------
// Item, window, mouse, keyboard queries and draw-list access
// ---------------------------------------------------------------------------

bool isItemHovered(HoveredFlags flags) {
    if (!gContext) return false;
    auto& c = *gContext;
    const bool hovered = itemHovered(c);
    if (hovered && c.lastItemRecord) {
        if (c.lastItemRecord->hoverSince < 0.0f) c.lastItemRecord->hoverSince = static_cast<float>(c.seconds);
    } else if (c.lastItemRecord) {
        c.lastItemRecord->hoverSince = -1.0f;
    }
    if (!hovered && (flags & HoveredFlags_AllowWhenDisabled) && c.lastItemRecord && c.lastItemRecord->disabled) {
        return c.lastItemRect.contains(c.io.MousePos.x, c.io.MousePos.y);
    }
    return hovered;
}

bool isItemActive() {
    if (!gContext || !gContext->lastItem) return false;
    auto* manager = inputManager();
    auto* focus = manager ? manager->getKeyFocusWidget() : nullptr;
    return widgetIsDescendant(focus, gContext->lastItem) ||
           (itemHovered(*gContext) && (gContext->mouseDown[0] || gContext->mouseDown[1] || gContext->mouseDown[2]));
}

bool isItemClicked(int button) {
    return gContext && itemClicked(*gContext, button);
}

bool isItemVisible() {
    if (!gContext) return false;
    auto& c = *gContext;
    const auto& rect = c.lastItemRect;
    const auto intersects = [&](const Rect& clip) {
        return rect.x < clip.x + clip.width && rect.y < clip.y + clip.height &&
               rect.x + rect.width > clip.x && rect.y + rect.height > clip.y;
    };
    if (rect.width <= 0 || rect.height <= 0 ||
        !intersects({0, 0, c.io.DisplaySize.x, c.io.DisplaySize.y})) {
        return false;
    }
    for (const auto& layout : c.layouts) {
        if (layout.parent && layout.screen.width > 0 && layout.screen.height > 0 &&
            !intersects(layout.screen)) {
            return false;
        }
    }
    return true;
}

Vec2 itemRectMin() {
    return gContext ? Vec2{gContext->lastItemRect.x, gContext->lastItemRect.y} : Vec2{};
}

Vec2 itemRectMax() {
    return gContext ? Vec2{gContext->lastItemRect.x + gContext->lastItemRect.width,
                           gContext->lastItemRect.y + gContext->lastItemRect.height} : Vec2{};
}

bool isAnyItemActive() {
    if (!gContext) return false;
    auto* manager = inputManager();
    auto* focus = manager ? manager->getKeyFocusWidget() : nullptr;
    // MyGUI also uses key focus for top-level windows. That is not an active
    // editor item; only a focused EditBox should suppress editor shortcuts.
    return (focus && focus->castType<MyGUI::EditBox>(false)) ||
           gContext->mouseDown[MouseButton_Left] ||
           gContext->mouseDown[MouseButton_Right] ||
           gContext->mouseDown[MouseButton_Middle];
}

bool isAnyItemHovered() {
    if (!gContext) return false;
    auto* manager = inputManager();
    return manager && manager->getMouseFocusWidget();
}

bool isWindowHovered(HoveredFlags) {
    return gContext && currentWindowHovered(*gContext);
}

bool isWindowFocused(FocusedFlags) {
    if (!gContext || !gContext->currentWindow) return false;
    auto* manager = inputManager();
    auto* focus = manager ? manager->getKeyFocusWidget() : nullptr;
    return widgetIsDescendant(focus, gContext->currentWindow->root) ||
           gContext->focusedRoot == gContext->currentWindow->root;
}

bool isMouseHoveringRect(Vec2 rMin, Vec2 rMax) {
    return gContext && Rect{rMin.x, rMin.y, rMax.x - rMin.x, rMax.y - rMin.y}.contains(
                           gContext->io.MousePos.x, gContext->io.MousePos.y);
}

bool isMouseDown(int button) {
    return gContext && button >= 0 && button < 3 && gContext->mouseDown[button];
}

bool isMouseClicked(int button) {
    return gContext && button >= 0 && button < 3 && gContext->mousePressed[button];
}

bool isMouseReleased(int button) {
    return gContext && button >= 0 && button < 3 && gContext->mouseReleased[button];
}

bool isMouseDoubleClicked(int button) {
    return gContext && button >= 0 && button < 3 && gContext->mouseDouble[button];
}

bool isMouseDragging(int button, float threshold) {
    if (!gContext || button < 0 || button >= 3 || !gContext->mouseDown[button]) return false;
    if (threshold < 0) threshold = 3.0f;
    const float dx = gContext->io.MousePos.x - gContext->pressPosition[button].x;
    const float dy = gContext->io.MousePos.y - gContext->pressPosition[button].y;
    return dx * dx + dy * dy >= threshold * threshold;
}

bool isMouseDragPastThreshold(int button, float threshold) {
    return isMouseDragging(button, threshold < 0 ? 6.0f : threshold);
}

Vec2 mousePos() {
    return gContext ? gContext->io.MousePos : Vec2{};
}

bool isKeyPressed(Key key, bool) {
    return gContext && key > Key_None && key < Key_COUNT && gContext->keyPressed[key];
}

bool shortcut(KeyChord keyChord, InputFlags flags) {
    if (!gContext) return false;
    auto& c = *gContext;
    const uint32_t key = keyChord & 0xFFFFu;
    const bool ctrl = (keyChord & Mod_Ctrl) != 0;
    const bool shift = (keyChord & Mod_Shift) != 0;
    const bool alt = (keyChord & Mod_Alt) != 0;
    if (ctrl != c.io.KeyCtrl || shift != c.io.KeyShift || alt != c.io.KeyAlt) return false;
    if ((flags & InputFlags_RouteFocused) && !(flags & InputFlags_RouteGlobal) && c.io.WantTextInput) return false;
    if (key == 0 || key >= Key_COUNT) return false;
    return c.keyPressed[key];
}

DrawList* windowDrawList() {
    if (!gContext) return nullptr;
    return gContext->currentWindow ? &gContext->currentWindow->drawList : &gContext->foreground;
}

DrawList* backgroundDrawList() {
    return gContext ? &gContext->background : nullptr;
}

DrawList* foregroundDrawList() {
    return gContext ? &gContext->foreground : nullptr;
}

// ---------------------------------------------------------------------------
// Text filter
// ---------------------------------------------------------------------------

bool TextFilter::draw(const char* label, float width) {
    if (width > 0) setNextItemWidth(width);
    return inputText(label ? label : "Filter", inputBuf, sizeof(inputBuf));
}

bool TextFilter::passFilter(const char* text, const char* textEnd) const {
    if (!text) return inputBuf[0] == '\0';
    if (!textEnd) textEnd = text + std::strlen(text);
    std::string haystack(text, textEnd);
    std::string needle(inputBuf);
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return needle.empty() || haystack.find(needle) != std::string::npos;
}

} // namespace ui
