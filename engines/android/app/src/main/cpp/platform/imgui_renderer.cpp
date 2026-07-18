/**
 * 文件用途：实现 Dear ImGui v1.92.8 的 EGL/OpenGL ES 渲染线程、纹理和 Android 输入。
 */
#include "imgui_renderer.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "android_bridge.h"
#include "../core/api/imgui_api.h"
#include "imgui.h"
#include "backends/imgui_impl_android.h"
#include "backends/imgui_impl_opengl3.h"

namespace {

constexpr const char* kLogTag = "小鱼精灵ImGui";
constexpr int kTargetFrameMs = 16;

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x0040
#endif

enum class InputKind {
    Touch,
    Text,
    Key,
    Scroll
};

/** Java UI 线程只写该轻量事件，所有 Dear ImGui IO 操作都留在渲染线程。 */
struct PendingInput {
    InputKind kind = InputKind::Touch;
    int action = 0;
    int pointerId = 0;
    int keyCode = 0;
    int unicodeCodePoint = 0;
    int metaState = 0;
    float x = 0.0F;
    float y = 0.0F;
    std::string text;
};

struct TextureEntry {
    GLuint id = 0;
    std::uint64_t revision = 0;
};

ImU32 argbToImU32(std::uint32_t color) {
    return IM_COL32(
            (color >> 16U) & 0xFFU,
            (color >> 8U) & 0xFFU,
            color & 0xFFU,
            (color >> 24U) & 0xFFU
    );
}

ImVec4 argbToVec4(std::uint32_t color) {
    constexpr float divisor = 255.0F;
    return ImVec4(
            static_cast<float>((color >> 16U) & 0xFFU) / divisor,
            static_cast<float>((color >> 8U) & 0xFFU) / divisor,
            static_cast<float>(color & 0xFFU) / divisor,
            static_cast<float>((color >> 24U) & 0xFFU) / divisor
    );
}

std::uint32_t vec4ToArgb(const float color[4]) {
    auto channel = [](float value) -> std::uint32_t {
        return static_cast<std::uint32_t>(std::round(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    return (channel(color[3]) << 24U)
            | (channel(color[0]) << 16U)
            | (channel(color[1]) << 8U)
            | channel(color[2]);
}

std::string itemId(const char* prefix, xiaoyv::api::ImGuiHandle handle) {
    return std::string(prefix) + "##xiaoyv_" + std::to_string(handle);
}

float resolvedWidth(float width) {
    if (width == -1.0F) {
        return std::max(1.0F, ImGui::GetContentRegionAvail().x);
    }
    return width;
}

float resolvedHeight(float height) {
    if (height == -1.0F) {
        return std::max(1.0F, ImGui::GetContentRegionAvail().y);
    }
    return height;
}

bool isVectorStyleVar(int style) {
    switch (style) {
        case ImGuiStyleVar_WindowPadding:
        case ImGuiStyleVar_WindowMinSize:
        case ImGuiStyleVar_WindowTitleAlign:
        case ImGuiStyleVar_FramePadding:
        case ImGuiStyleVar_ItemSpacing:
        case ImGuiStyleVar_ItemInnerSpacing:
        case ImGuiStyleVar_CellPadding:
        case ImGuiStyleVar_TableAngledHeadersTextAlign:
        case ImGuiStyleVar_ButtonTextAlign:
        case ImGuiStyleVar_SelectableTextAlign:
        case ImGuiStyleVar_SeparatorTextAlign:
        case ImGuiStyleVar_SeparatorTextPadding:
            return true;
        default:
            return false;
    }
}

/**
 * 单个 Surface 对应一个 Renderer；全局包装层只负责线程切换和输入投递。
 */
class Renderer {
public:
    explicit Renderer(ANativeWindow* window) : window_(window) {
        ANativeWindow_acquire(window_);
    }

    ~Renderer() {
        stop();
        ANativeWindow_release(window_);
    }

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool start() {
        try {
            worker_ = std::thread(&Renderer::run, this);
            return true;
        } catch (...) {
            return false;
        }
    }

    void stop() {
        stopRequested_.store(true);
        if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
            worker_.join();
        }
    }

    void enqueue(PendingInput input) {
        std::lock_guard<std::mutex> lock(inputMutex_);
        if (inputs_.size() >= 512) {
            inputs_.erase(inputs_.begin());
        }
        inputs_.push_back(std::move(input));
    }

private:
    ANativeWindow* window_ = nullptr;
    std::atomic<bool> stopRequested_{false};
    std::thread worker_;
    std::mutex inputMutex_;
    std::vector<PendingInput> inputs_;

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    std::unordered_map<xiaoyv::api::ImGuiHandle, TextureEntry> textures_;
    std::vector<xiaoyv::api::ImGuiInteraction> interactions_;
    bool keyboardVisible_ = false;
    bool surfaceCollapsed_ = false;
    int expandedSurfaceHeight_ = 0;
    std::uint64_t appliedSurfaceGeneration_ = 0;
    int appliedTheme_ = std::numeric_limits<int>::min();
    std::string appliedFontPath_;
    float appliedFontSize_ = 0.0F;

    bool initializeEgl() {
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY || eglInitialize(display_, nullptr, nullptr) != EGL_TRUE) {
            return false;
        }

        const EGLint configAttributes[] = {
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_DEPTH_SIZE, 0,
                EGL_STENCIL_SIZE, 8,
                EGL_NONE
        };
        EGLConfig config = nullptr;
        EGLint configCount = 0;
        if (eglChooseConfig(display_, configAttributes, &config, 1, &configCount) != EGL_TRUE
                || configCount < 1) {
            return false;
        }

        EGLint nativeFormat = 0;
        eglGetConfigAttrib(display_, config, EGL_NATIVE_VISUAL_ID, &nativeFormat);
        ANativeWindow_setBuffersGeometry(window_, 0, 0, nativeFormat);
        const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttributes);
        surface_ = eglCreateWindowSurface(display_, config, window_, nullptr);
        if (context_ == EGL_NO_CONTEXT || surface_ == EGL_NO_SURFACE
                || eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
            return false;
        }
        eglSwapInterval(display_, 1);
        return true;
    }

    void destroyEgl() {
        if (display_ == EGL_NO_DISPLAY) {
            return;
        }
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
            surface_ = EGL_NO_SURFACE;
        }
        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT;
        }
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
    }

    void run() {
        if (!initializeEgl()) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", "EGL 初始化失败");
            destroyEgl();
            xiaoyv::api::imguiNotifyRendererFailure("ImGui EGL 初始化失败");
            return;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigInputTextCursorBlink = true;
        ImGui_ImplAndroid_Init(window_);
        if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
            ImGui_ImplAndroid_Shutdown();
            ImGui::DestroyContext();
            destroyEgl();
            xiaoyv::api::imguiNotifyRendererFailure("ImGui OpenGL ES 3 后端初始化失败");
            return;
        }

        while (!stopRequested_.load()) {
            const auto frameStart = std::chrono::steady_clock::now();
            std::shared_ptr<const xiaoyv::api::ImGuiRenderSnapshot> snapshot =
                    xiaoyv::api::imguiAcquireRenderSnapshot();
            if (snapshot == nullptr || !snapshot->displayed) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kTargetFrameMs));
                continue;
            }

            if (snapshot->surfaceGeneration != appliedSurfaceGeneration_) {
                // 脚本再次 show/showWindow 表示一份新 Surface 配置，不能继承旧窗口的收起状态。
                appliedSurfaceGeneration_ = snapshot->surfaceGeneration;
                surfaceCollapsed_ = false;
                expandedSurfaceHeight_ = snapshot->surface.height;
            }

            applyTheme(snapshot->colorTheme);
            applyFont(snapshot->surface);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplAndroid_NewFrame();
            consumeInput();
            ImGui::NewFrame();

            interactions_.clear();
            std::unordered_set<xiaoyv::api::ImGuiHandle> activeTextures;
            renderRootWidgets(*snapshot, &activeTextures);
            for (xiaoyv::api::ImGuiHandle handle : snapshot->windows) {
                renderWindow(*snapshot, handle, &activeTextures);
            }
            renderShapes(*snapshot, &activeTextures);
            // 独立窗口标题栏最后处理，使标题、关闭和缩放柄始终位于脚本图形之上。
            renderSurfaceChrome(*snapshot);

            ImGui::Render();
            const int width = ANativeWindow_getWidth(window_);
            const int height = ANativeWindow_getHeight(window_);
            glViewport(0, 0, width, height);
            glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (eglSwapBuffers(display_, surface_) != EGL_TRUE) {
                if (!stopRequested_.load()) {
                    xiaoyv::api::imguiNotifyRendererFailure("ImGui Surface 交换缓冲区失败");
                }
                break;
            }

            purgeTextures(activeTextures);
            updateKeyboard(ImGui::GetIO().WantTextInput);
            xiaoyv::api::imguiApplyInteractions(interactions_);

            const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - frameStart
            );
            if (spent.count() < kTargetFrameMs) {
                std::this_thread::sleep_for(
                        std::chrono::milliseconds(kTargetFrameMs - spent.count())
                );
            }
        }

        updateKeyboard(false);
        destroyTextures();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplAndroid_Shutdown();
        ImGui::DestroyContext();
        destroyEgl();
    }

    void applyTheme(int theme) {
        if (theme == appliedTheme_) {
            return;
        }
        if (theme == 1) {
            ImGui::StyleColorsLight();
        } else if (theme == 2) {
            ImGui::StyleColorsClassic();
        } else {
            ImGui::StyleColorsDark();
        }
        appliedTheme_ = theme;
    }

    void applyFont(const xiaoyv::api::ImGuiSurfaceConfig& config) {
        if (appliedFontPath_ == config.fontPath
                && std::abs(appliedFontSize_ - config.fontSize) < 0.01F) {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();
        const char* candidates[] = {
                config.fontPath.c_str(),
                "/system/fonts/NotoSansCJK-Regular.ttc",
                "/system/fonts/NotoSansSC-Regular.otf",
                "/system/fonts/NotoSans-Regular.ttf"
        };
        ImFont* font = nullptr;
        for (const char* candidate : candidates) {
            if (candidate == nullptr || candidate[0] == '\0') continue;
            font = io.Fonts->AddFontFromFileTTF(candidate, config.fontSize);
            if (font != nullptr) break;
        }
        if (font == nullptr) {
            io.Fonts->AddFontDefault();
        }
        appliedFontPath_ = config.fontPath;
        appliedFontSize_ = config.fontSize;
    }

    void consumeInput() {
        std::vector<PendingInput> inputs;
        {
            std::lock_guard<std::mutex> lock(inputMutex_);
            inputs.swap(inputs_);
        }
        ImGuiIO& io = ImGui::GetIO();
        for (const PendingInput& input : inputs) {
            if (input.kind == InputKind::Touch) {
                io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMousePosEvent(input.x, input.y);
                if (input.action == 0) {
                    io.AddMouseButtonEvent(0, true);
                } else if (input.action == 1 || input.action == 3) {
                    io.AddMouseButtonEvent(0, false);
                }
            } else if (input.kind == InputKind::Text) {
                io.AddInputCharactersUTF8(input.text.c_str());
            } else if (input.kind == InputKind::Scroll) {
                io.AddMouseWheelEvent(input.x, input.y);
            } else {
                applyKey(input);
            }
        }
    }

    static ImGuiKey keyCodeToImGui(int keyCode) {
        switch (keyCode) {
            case 61: return ImGuiKey_Tab;
            case 19: return ImGuiKey_UpArrow;
            case 20: return ImGuiKey_DownArrow;
            case 21: return ImGuiKey_LeftArrow;
            case 22: return ImGuiKey_RightArrow;
            case 92: return ImGuiKey_PageUp;
            case 93: return ImGuiKey_PageDown;
            case 122: return ImGuiKey_Home;
            case 123: return ImGuiKey_End;
            case 112: return ImGuiKey_Delete;
            case 67: return ImGuiKey_Backspace;
            case 66: return ImGuiKey_Enter;
            case 111:
            case 4: return ImGuiKey_Escape;
            case 62: return ImGuiKey_Space;
            case 29: return ImGuiKey_A;
            case 30: return ImGuiKey_B;
            case 31: return ImGuiKey_C;
            case 32: return ImGuiKey_D;
            case 33: return ImGuiKey_E;
            case 34: return ImGuiKey_F;
            case 35: return ImGuiKey_G;
            case 36: return ImGuiKey_H;
            case 37: return ImGuiKey_I;
            case 38: return ImGuiKey_J;
            case 39: return ImGuiKey_K;
            case 40: return ImGuiKey_L;
            case 41: return ImGuiKey_M;
            case 42: return ImGuiKey_N;
            case 43: return ImGuiKey_O;
            case 44: return ImGuiKey_P;
            case 45: return ImGuiKey_Q;
            case 46: return ImGuiKey_R;
            case 47: return ImGuiKey_S;
            case 48: return ImGuiKey_T;
            case 49: return ImGuiKey_U;
            case 50: return ImGuiKey_V;
            case 51: return ImGuiKey_W;
            case 52: return ImGuiKey_X;
            case 53: return ImGuiKey_Y;
            case 54: return ImGuiKey_Z;
            case 7: return ImGuiKey_0;
            case 8: return ImGuiKey_1;
            case 9: return ImGuiKey_2;
            case 10: return ImGuiKey_3;
            case 11: return ImGuiKey_4;
            case 12: return ImGuiKey_5;
            case 13: return ImGuiKey_6;
            case 14: return ImGuiKey_7;
            case 15: return ImGuiKey_8;
            case 16: return ImGuiKey_9;
            default: return ImGuiKey_None;
        }
    }

    static void applyKey(const PendingInput& input) {
        ImGuiIO& io = ImGui::GetIO();
        const bool down = input.action == 0;
        io.AddKeyEvent(ImGuiMod_Shift, (input.metaState & 0x1) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (input.metaState & 0x2) != 0);
        io.AddKeyEvent(ImGuiMod_Ctrl, (input.metaState & 0x1000) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (input.metaState & 0x10000) != 0);
        ImGuiKey key = keyCodeToImGui(input.keyCode);
        if (key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
        if (down && input.unicodeCodePoint >= 0x20 && input.unicodeCodePoint != 0x7F) {
            io.AddInputCharacter(static_cast<unsigned int>(input.unicodeCodePoint));
        }
    }

    void updateKeyboard(bool visible) {
        if (keyboardVisible_ == visible) {
            return;
        }
        keyboardVisible_ = visible;
        AndroidBridge::setScriptImGuiKeyboardVisible(visible);
    }

    void applyWidgetStyle(const xiaoyv::api::ImGuiWidgetSnapshot& widget,
                          int* styleCount,
                          int* colorCount) {
        *styleCount = 0;
        *colorCount = 0;
        for (const auto& item : widget.styleValues) {
            if (item.first < 0 || item.first >= ImGuiStyleVar_COUNT) continue;
            if (isVectorStyleVar(item.first)) {
                ImGui::PushStyleVar(
                        static_cast<ImGuiStyleVar>(item.first),
                        ImVec2(item.second.first, item.second.second)
                );
            } else {
                ImGui::PushStyleVar(
                        static_cast<ImGuiStyleVar>(item.first),
                        item.second.first
                );
            }
            ++*styleCount;
        }
        for (const auto& item : widget.styleColors) {
            if (item.first < 0 || item.first >= ImGuiCol_COUNT) continue;
            ImGui::PushStyleColor(
                    static_cast<ImGuiCol>(item.first),
                    argbToVec4(item.second)
            );
            ++*colorCount;
        }
    }

    static void popWidgetStyle(int styleCount, int colorCount) {
        if (colorCount > 0) ImGui::PopStyleColor(colorCount);
        if (styleCount > 0) ImGui::PopStyleVar(styleCount);
    }

    ImTextureRef textureFor(
            xiaoyv::api::ImGuiHandle handle,
            const std::shared_ptr<const xiaoyv::api::ImGuiImagePixels>& image,
            std::unordered_set<xiaoyv::api::ImGuiHandle>* activeTextures
    ) {
        if (image == nullptr || image->rgba == nullptr || image->rgba->empty()) {
            return ImTextureRef();
        }
        activeTextures->insert(handle);
        TextureEntry& entry = textures_[handle];
        if (entry.id != 0 && entry.revision == image->revision) {
            return ImTextureRef(static_cast<ImTextureID>(entry.id));
        }
        if (entry.id != 0) {
            glDeleteTextures(1, &entry.id);
            entry.id = 0;
        }

        glGenTextures(1, &entry.id);
        glBindTexture(GL_TEXTURE_2D, entry.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA,
                image->width,
                image->height,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                image->rgba->data()
        );
        glBindTexture(GL_TEXTURE_2D, 0);
        entry.revision = image->revision;
        return ImTextureRef(static_cast<ImTextureID>(entry.id));
    }

    void purgeTextures(const std::unordered_set<xiaoyv::api::ImGuiHandle>& active) {
        for (auto iterator = textures_.begin(); iterator != textures_.end();) {
            if (active.count(iterator->first) == 0) {
                if (iterator->second.id != 0) {
                    glDeleteTextures(1, &iterator->second.id);
                }
                iterator = textures_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    void destroyTextures() {
        for (auto& item : textures_) {
            if (item.second.id != 0) {
                glDeleteTextures(1, &item.second.id);
            }
        }
        textures_.clear();
    }

    void renderSurfaceChrome(const xiaoyv::api::ImGuiRenderSnapshot& snapshot) {
        if (!snapshot.surface.windowed) {
            return;
        }

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float titleHeight = std::clamp(
                snapshot.surface.titleFontSize + 20.0F,
                36.0F,
                std::max(36.0F, display.y)
        );
        const float buttonSize = std::max(24.0F, titleHeight - 12.0F);
        ImDrawList* foreground = ImGui::GetForegroundDrawList();

        if (!snapshot.surface.hasTitle) {
            // 没有标题栏就没有可恢复的收起入口，强制保持展开状态。
            surfaceCollapsed_ = false;
            expandedSurfaceHeight_ = 0;
        } else {
            const float visibleTitleHeight = std::min(titleHeight, display.y);
            ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F), ImGuiCond_Always);
            ImGui::SetNextWindowSize(
                    ImVec2(std::max(1.0F, display.x), std::max(1.0F, visibleTitleHeight)),
                    ImGuiCond_Always
            );
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
            ImGui::Begin(
                    "##xiaoyv_surface_title",
                    nullptr,
                    ImGuiWindowFlags_NoDecoration
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoScrollbar
                            | ImGuiWindowFlags_NoScrollWithMouse
            );

            foreground->AddRectFilled(
                    ImVec2(0.0F, 0.0F),
                    ImVec2(display.x, visibleTitleHeight),
                    argbToImU32(snapshot.surface.titleBackgroundColor)
            );
            float titleTextRight = display.x - 6.0F;
            if (snapshot.surface.hasClose) titleTextRight -= buttonSize + 4.0F;
            if (snapshot.surface.hasToggle) titleTextRight -= buttonSize + 4.0F;
            foreground->PushClipRect(
                    ImVec2(8.0F, 0.0F),
                    ImVec2(std::max(8.0F, titleTextRight), visibleTitleHeight),
                    true
            );
            foreground->AddText(
                    ImGui::GetFont(),
                    snapshot.surface.titleFontSize,
                    ImVec2(
                            12.0F,
                            std::max(
                                    0.0F,
                                    (visibleTitleHeight - snapshot.surface.titleFontSize) * 0.5F
                            )
                    ),
                    argbToImU32(snapshot.surface.titleColor),
                    snapshot.surface.title.c_str()
            );
            foreground->PopClipRect();

            float buttonRight = display.x - 6.0F;
            if (snapshot.surface.hasClose) {
                const float left = buttonRight - buttonSize;
                ImGui::SetCursorScreenPos(ImVec2(left, 6.0F));
                ImGui::InvisibleButton(
                        "##xiaoyv_surface_close",
                        ImVec2(buttonSize, std::max(1.0F, visibleTitleHeight - 12.0F))
                );
                if (ImGui::IsItemHovered()) {
                    foreground->AddRectFilled(
                            ImVec2(left, 6.0F),
                            ImVec2(buttonRight, visibleTitleHeight - 6.0F),
                            IM_COL32(255, 255, 255, 36)
                    );
                }
                const float inset = buttonSize * 0.3F;
                foreground->AddLine(
                        ImVec2(left + inset, 6.0F + inset),
                        ImVec2(buttonRight - inset, visibleTitleHeight - 6.0F - inset),
                        argbToImU32(snapshot.surface.closeColor),
                        2.0F
                );
                foreground->AddLine(
                        ImVec2(buttonRight - inset, 6.0F + inset),
                        ImVec2(left + inset, visibleTitleHeight - 6.0F - inset),
                        argbToImU32(snapshot.surface.closeColor),
                        2.0F
                );
                if (ImGui::IsItemClicked()) {
                    xiaoyv::api::ImGuiInteraction interaction;
                    interaction.type = xiaoyv::api::ImGuiInteractionType::SurfaceCloseRequested;
                    interactions_.push_back(std::move(interaction));
                }
                buttonRight = left - 4.0F;
            }

            if (snapshot.surface.hasToggle) {
                const float left = buttonRight - buttonSize;
                ImGui::SetCursorScreenPos(ImVec2(left, 6.0F));
                ImGui::InvisibleButton(
                        "##xiaoyv_surface_toggle",
                        ImVec2(buttonSize, std::max(1.0F, visibleTitleHeight - 12.0F))
                );
                if (ImGui::IsItemHovered()) {
                    foreground->AddRectFilled(
                            ImVec2(left, 6.0F),
                            ImVec2(buttonRight, visibleTitleHeight - 6.0F),
                            IM_COL32(255, 255, 255, 28)
                    );
                }
                const ImU32 toggleColor = argbToImU32(snapshot.surface.toggleColor);
                const float centerX = (left + buttonRight) * 0.5F;
                const float centerY = visibleTitleHeight * 0.5F;
                const float half = buttonSize * 0.22F;
                foreground->AddLine(
                        ImVec2(centerX - half, centerY),
                        ImVec2(centerX + half, centerY),
                        toggleColor,
                        2.0F
                );
                if (surfaceCollapsed_) {
                    foreground->AddLine(
                            ImVec2(centerX, centerY - half),
                            ImVec2(centerX, centerY + half),
                            toggleColor,
                            2.0F
                    );
                }
                if (ImGui::IsItemClicked()) {
                    xiaoyv::api::ImGuiInteraction interaction;
                    interaction.type = xiaoyv::api::ImGuiInteractionType::SurfaceGeometryChanged;
                    interaction.x = static_cast<float>(snapshot.surface.x);
                    interaction.y = static_cast<float>(snapshot.surface.y);
                    interaction.width = static_cast<float>(snapshot.surface.width);
                    if (!surfaceCollapsed_) {
                        expandedSurfaceHeight_ = std::max(
                                snapshot.surface.height,
                                static_cast<int>(std::round(display.y))
                        );
                        surfaceCollapsed_ = true;
                        interaction.height = visibleTitleHeight;
                    } else {
                        surfaceCollapsed_ = false;
                        interaction.height = static_cast<float>(std::max(
                                static_cast<int>(std::ceil(titleHeight)),
                                expandedSurfaceHeight_
                        ));
                    }
                    interactions_.push_back(std::move(interaction));
                }
                buttonRight = left - 4.0F;
            }

            // 标题剩余区域负责移动窗口，按钮区域不会触发拖动。
            ImGui::SetCursorScreenPos(ImVec2(0.0F, 0.0F));
            ImGui::InvisibleButton(
                    "##xiaoyv_surface_move",
                    ImVec2(std::max(1.0F, buttonRight), std::max(1.0F, visibleTitleHeight))
            );
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                xiaoyv::api::ImGuiInteraction interaction;
                interaction.type = xiaoyv::api::ImGuiInteractionType::SurfaceGeometryChanged;
                interaction.x = static_cast<float>(snapshot.surface.x) + delta.x;
                interaction.y = static_cast<float>(snapshot.surface.y) + delta.y;
                interaction.width = static_cast<float>(snapshot.surface.width);
                interaction.height = static_cast<float>(snapshot.surface.height);
                interactions_.push_back(std::move(interaction));
            }

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }

        if (snapshot.surface.hasResize && !surfaceCollapsed_ && display.x > 0.0F && display.y > 0.0F) {
            const float grip = std::min(
                    std::max(28.0F, buttonSize),
                    std::min(display.x, display.y)
            );
            const ImVec2 gripPosition(display.x - grip, display.y - grip);
            ImGui::SetNextWindowPos(gripPosition, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(grip, grip), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
            ImGui::Begin(
                    "##xiaoyv_surface_resize_window",
                    nullptr,
                    ImGuiWindowFlags_NoDecoration
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoScrollbar
                            | ImGuiWindowFlags_NoScrollWithMouse
            );
            ImGui::SetCursorScreenPos(gripPosition);
            ImGui::InvisibleButton("##xiaoyv_surface_resize", ImVec2(grip, grip));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                xiaoyv::api::ImGuiInteraction interaction;
                interaction.type = xiaoyv::api::ImGuiInteractionType::SurfaceGeometryChanged;
                interaction.x = static_cast<float>(snapshot.surface.x);
                interaction.y = static_cast<float>(snapshot.surface.y);
                interaction.width = std::max(
                        120.0F,
                        static_cast<float>(snapshot.surface.width) + delta.x
                );
                interaction.height = std::max(
                        titleHeight,
                        static_cast<float>(snapshot.surface.height) + delta.y
                );
                expandedSurfaceHeight_ = static_cast<int>(std::round(interaction.height));
                interactions_.push_back(std::move(interaction));
            }
            foreground->AddTriangleFilled(
                    ImVec2(display.x, display.y - grip),
                    ImVec2(display.x, display.y),
                    ImVec2(display.x - grip, display.y),
                    argbToImU32(snapshot.surface.resizeColor)
            );
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }
    }

    void renderRootWidgets(
            const xiaoyv::api::ImGuiRenderSnapshot& snapshot,
            std::unordered_set<xiaoyv::api::ImGuiHandle>* activeTextures
    ) {
        if (snapshot.rootWidgets.empty() || surfaceCollapsed_) {
            return;
        }
        ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
        ImGui::Begin(
                "##xiaoyv_root_widgets",
                nullptr,
                ImGuiWindowFlags_NoDecoration
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoBringToFrontOnFocus
        );
        for (xiaoyv::api::ImGuiHandle handle : snapshot.rootWidgets) {
            auto iterator = snapshot.widgets.find(handle);
            if (iterator == snapshot.widgets.end()) continue;
            ImGui::SetCursorScreenPos(ImVec2(iterator->second.x, iterator->second.y));
            renderWidget(snapshot, iterator->second, activeTextures, false);
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    void renderWindow(
            const xiaoyv::api::ImGuiRenderSnapshot& snapshot,
            xiaoyv::api::ImGuiHandle handle,
            std::unordered_set<xiaoyv::api::ImGuiHandle>* activeTextures
    ) {
        if (surfaceCollapsed_) return;
        auto iterator = snapshot.widgets.find(handle);
        if (iterator == snapshot.widgets.end()) return;
        const xiaoyv::api::ImGuiWidgetSnapshot& window = iterator->second;
        if (!window.visible || window.kind != xiaoyv::api::ImGuiWidgetKind::Window) return;

        ImGui::SetNextWindowPos(ImVec2(window.x, window.y), ImGuiCond_Always);
        if ((window.windowFlags & ImGuiWindowFlags_AlwaysAutoResize) == 0) {
            // AlwaysAutoResize 必须由 Dear ImGui 根据内容决定尺寸；每帧强制 SetNextWindowSize
            // 会让公开窗口标志看似设置成功却完全没有效果。
            ImGui::SetNextWindowSize(ImVec2(window.width, window.height), ImGuiCond_Always);
        }
        bool open = true;
        bool* openPointer = window.showClose && !window.closePending ? &open : nullptr;
        int styleCount = 0;
        int colorCount = 0;
        applyWidgetStyle(window, &styleCount, &colorCount);
        const std::string id = window.title + "##xiaoyv_window_" + std::to_string(handle);
        ImGuiWindowFlags flags = static_cast<ImGuiWindowFlags>(window.windowFlags)
                | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin(id.c_str(), openPointer, flags)) {
            for (xiaoyv::api::ImGuiHandle child : window.children) {
                auto childIterator = snapshot.widgets.find(child);
                if (childIterator != snapshot.widgets.end()) {
                    renderWidget(snapshot, childIterator->second, activeTextures, false);
                }
            }
        }
        const ImVec2 actualPos = ImGui::GetWindowPos();
        const ImVec2 actualSize = ImGui::GetWindowSize();
        ImGui::End();
        popWidgetStyle(styleCount, colorCount);

        if (openPointer != nullptr && !open) {
            xiaoyv::api::ImGuiInteraction interaction;
            interaction.type = xiaoyv::api::ImGuiInteractionType::WindowCloseRequested;
            interaction.handle = handle;
            interactions_.push_back(std::move(interaction));
        }
        if (std::abs(actualPos.x - window.x) > 0.5F
                || std::abs(actualPos.y - window.y) > 0.5F
                || std::abs(actualSize.x - window.width) > 0.5F
                || std::abs(actualSize.y - window.height) > 0.5F) {
            xiaoyv::api::ImGuiInteraction interaction;
            interaction.type = xiaoyv::api::ImGuiInteractionType::WindowGeometryChanged;
            interaction.handle = handle;
            interaction.x = actualPos.x;
            interaction.y = actualPos.y;
            interaction.width = actualSize.x;
            interaction.height = actualSize.y;
            interactions_.push_back(std::move(interaction));
        }
    }

    void renderChildren(
            const xiaoyv::api::ImGuiRenderSnapshot& snapshot,
            const xiaoyv::api::ImGuiWidgetSnapshot& parent,
            std::unordered_set<xiaoyv::api::ImGuiHandle>* activeTextures,
            bool horizontal
    ) {
        bool first = true;
        for (xiaoyv::api::ImGuiHandle child : parent.children) {
            auto iterator = snapshot.widgets.find(child);
            if (iterator == snapshot.widgets.end() || !iterator->second.visible) continue;
            if (horizontal && !first) {
                ImGui::SameLine();
            }
            renderWidget(snapshot, iterator->second, activeTextures, horizontal);
            first = false;
        }
    }

    void renderWidget(
            const xiaoyv::api::ImGuiRenderSnapshot& snapshot,
            const xiaoyv::api::ImGuiWidgetSnapshot& widget,
            std::unordered_set<xiaoyv::api::ImGuiHandle>* activeTextures,
            bool parentHorizontal
    ) {
        if (!widget.visible) return;
        if (widget.sameLine && !parentHorizontal) {
            ImGui::SameLine(0.0F, widget.sameLineSpacing);
        }

        int styleCount = 0;
        int colorCount = 0;
        applyWidgetStyle(widget, &styleCount, &colorCount);
        ImGui::PushID(static_cast<int>(widget.handle & 0x7FFFFFFF));

        const float width = resolvedWidth(widget.width);
        const float height = resolvedHeight(widget.height);
        switch (widget.kind) {
            case xiaoyv::api::ImGuiWidgetKind::VerticalLayout:
            case xiaoyv::api::ImGuiWidgetKind::HorizontalLayout: {
                ImGuiChildFlags childFlags = widget.borderVisible
                        ? ImGuiChildFlags_Borders
                        : ImGuiChildFlags_None;
                if (widget.width == 0.0F) childFlags |= ImGuiChildFlags_AutoResizeX;
                if (widget.height == 0.0F) childFlags |= ImGuiChildFlags_AutoResizeY;
                const std::string id = itemId("layout", widget.handle);
                if (ImGui::BeginChild(id.c_str(), ImVec2(width, height), childFlags)) {
                    renderChildren(
                            snapshot,
                            widget,
                            activeTextures,
                            widget.kind == xiaoyv::api::ImGuiWidgetKind::HorizontalLayout
                    );
                }
                ImGui::EndChild();
                break;
            }
            case xiaoyv::api::ImGuiWidgetKind::TreeLayout: {
                ImGuiChildFlags childFlags = ImGuiChildFlags_AutoResizeY;
                if (widget.width == 0.0F) childFlags |= ImGuiChildFlags_AutoResizeX;
                if (widget.borderVisible) childFlags |= ImGuiChildFlags_Borders;
                const std::string childId = itemId("tree_layout", widget.handle);
                if (ImGui::BeginChild(childId.c_str(), ImVec2(width, 0.0F), childFlags)) {
                    const std::string treeId = widget.title
                            + "##tree_" + std::to_string(widget.handle);
                    if (ImGui::TreeNodeEx(
                            treeId.c_str(),
                            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth
                    )) {
                        renderChildren(snapshot, widget, activeTextures, false);
                        ImGui::TreePop();
                    }
                }
                ImGui::EndChild();
                break;
            }
            case xiaoyv::api::ImGuiWidgetKind::TabBar: {
                const std::string id = widget.title + "##tabbar_" + std::to_string(widget.handle);
                if (ImGui::BeginTabBar(id.c_str())) {
                    for (xiaoyv::api::ImGuiHandle child : widget.children) {
                        auto iterator = snapshot.widgets.find(child);
                        if (iterator == snapshot.widgets.end()
                                || iterator->second.kind
                                        != xiaoyv::api::ImGuiWidgetKind::TabItem) continue;
                        const auto& tab = iterator->second;
                        if (!tab.visible) continue;
                        int tabStyleCount = 0;
                        int tabColorCount = 0;
                        applyWidgetStyle(tab, &tabStyleCount, &tabColorCount);
                        const std::string tabId = tab.title + "##tab_" + std::to_string(tab.handle);
                        if (ImGui::BeginTabItem(tabId.c_str())) {
                            renderChildren(snapshot, tab, activeTextures, false);
                            ImGui::EndTabItem();
                        }
                        popWidgetStyle(tabStyleCount, tabColorCount);
                    }
                    ImGui::EndTabBar();
                }
                break;
            }
            case xiaoyv::api::ImGuiWidgetKind::TabItem:
            case xiaoyv::api::ImGuiWidgetKind::Window:
                break;
            case xiaoyv::api::ImGuiWidgetKind::Button: {
                if (ImGui::Button(
                        itemId(widget.text.c_str(), widget.handle).c_str(),
                        ImVec2(width, height)
                )) {
                    xiaoyv::api::ImGuiInteraction interaction;
                    interaction.type = xiaoyv::api::ImGuiInteractionType::ButtonClick;
                    interaction.handle = widget.handle;
                    interactions_.push_back(std::move(interaction));
                }
                break;
            }
            case xiaoyv::api::ImGuiWidgetKind::Label:
                if (widget.singleLine) {
                    ImGui::TextUnformatted(widget.text.c_str());
                } else {
                    ImGui::PushTextWrapPos(width > 0.0F ? ImGui::GetCursorPosX() + width : 0.0F);
                    ImGui::TextUnformatted(widget.text.c_str());
                    ImGui::PopTextWrapPos();
                }
                break;
            case xiaoyv::api::ImGuiWidgetKind::CheckBox: {
                bool checked = widget.checked;
                if (ImGui::Checkbox(
                        itemId(widget.text.c_str(), widget.handle).c_str(),
                        &checked
                )) {
                    xiaoyv::api::ImGuiInteraction interaction;
                    interaction.type = xiaoyv::api::ImGuiInteractionType::CheckChanged;
                    interaction.handle = widget.handle;
                    interaction.boolValue = checked;
                    interactions_.push_back(std::move(interaction));
                }
                break;
            }
            case xiaoyv::api::ImGuiWidgetKind::Switch:
                renderSwitch(widget);
                break;
            case xiaoyv::api::ImGuiWidgetKind::InputText:
                renderInput(widget, width, height);
                break;
            case xiaoyv::api::ImGuiWidgetKind::ProgressBar:
                ImGui::ProgressBar(widget.progress, ImVec2(width, height));
                break;
            case xiaoyv::api::ImGuiWidgetKind::Slider: {
                if (width != 0.0F) ImGui::SetNextItemWidth(width);
                int value = widget.integerValue;
                if (ImGui::SliderInt(
                        itemId(widget.text.c_str(), widget.handle).c_str(),
                        &value,
                        widget.minimum,
                        widget.maximum
                )) {
                    xiaoyv::api::ImGuiInteraction interaction;
                    interaction.type = xiaoyv::api::ImGuiInteractionType::SliderChanged;
                    interaction.handle = widget.handle;
                    interaction.integerValue = value;
                    interactions_.push_back(std::move(interaction));
                }
                break;
            }
            case xiaoyv::api::ImGuiWidgetKind::ColorPicker:
                renderColorPicker(widget, width, height);
                break;
            case xiaoyv::api::ImGuiWidgetKind::ComboBox:
                renderCombo(widget, width);
                break;
            case xiaoyv::api::ImGuiWidgetKind::RadioGroup:
                renderRadioGroup(widget);
                break;
            case xiaoyv::api::ImGuiWidgetKind::Table:
                renderTable(widget, width, height);
                break;
            case xiaoyv::api::ImGuiWidgetKind::Image: {
                ImTextureRef texture = textureFor(widget.handle, widget.image, activeTextures);
                if (texture.GetTexID() != ImTextureID_Invalid) {
                    float imageWidth = width;
                    float imageHeight = height;
                    if (widget.image != nullptr) {
                        if (imageWidth <= 0.0F) imageWidth = static_cast<float>(widget.image->width);
                        if (imageHeight <= 0.0F) imageHeight = static_cast<float>(widget.image->height);
                    }
                    ImGui::Image(texture, ImVec2(imageWidth, imageHeight));
                } else {
                    ImGui::Dummy(ImVec2(std::max(1.0F, width), std::max(1.0F, height)));
                }
                break;
            }
        }

        ImGui::PopID();
        popWidgetStyle(styleCount, colorCount);
    }

    void renderSwitch(const xiaoyv::api::ImGuiWidgetSnapshot& widget) {
        const float height = widget.height > 0.0F
                ? widget.height
                : ImGui::GetFrameHeight();
        const float trackWidth = height * 1.8F;
        ImGui::InvisibleButton("##switch", ImVec2(trackWidth, height));
        bool checked = widget.checked;
        if (ImGui::IsItemClicked()) {
            checked = !checked;
            xiaoyv::api::ImGuiInteraction interaction;
            interaction.type = xiaoyv::api::ImGuiInteractionType::CheckChanged;
            interaction.handle = widget.handle;
            interaction.boolValue = checked;
            interactions_.push_back(std::move(interaction));
        }
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 background = ImGui::GetColorU32(
                checked ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg
        );
        drawList->AddRectFilled(minimum, maximum, background, height * 0.5F);
        const float radius = height * 0.4F;
        const float centerX = checked ? maximum.x - height * 0.5F : minimum.x + height * 0.5F;
        drawList->AddCircleFilled(
                ImVec2(centerX, minimum.y + height * 0.5F),
                radius,
                IM_COL32(255, 255, 255, 255)
        );
        if (!widget.text.empty()) {
            ImGui::SameLine();
            ImGui::TextUnformatted(widget.text.c_str());
        }
    }

    void renderInput(
            const xiaoyv::api::ImGuiWidgetSnapshot& widget,
            float width,
            float height
    ) {
        if (width != 0.0F) ImGui::SetNextItemWidth(width);
        const std::size_t capacity = std::max<std::size_t>(4096, widget.text.size() + 1024);
        std::vector<char> buffer(capacity, '\0');
        std::memcpy(buffer.data(), widget.text.data(), std::min(widget.text.size(), capacity - 1));
        ImGuiInputTextFlags flags = widget.inputType == 1
                ? ImGuiInputTextFlags_Password
                : ImGuiInputTextFlags_None;
        bool changed;
        const std::string id = itemId(widget.title.c_str(), widget.handle);
        if (widget.inputType == 2) {
            changed = ImGui::InputTextMultiline(
                    id.c_str(),
                    buffer.data(),
                    buffer.size(),
                    ImVec2(width, height > 0.0F ? height : ImGui::GetTextLineHeight() * 4.0F),
                    flags
            );
        } else {
            changed = ImGui::InputText(id.c_str(), buffer.data(), buffer.size(), flags);
        }
        if (changed) {
            xiaoyv::api::ImGuiInteraction interaction;
            interaction.type = xiaoyv::api::ImGuiInteractionType::InputChanged;
            interaction.handle = widget.handle;
            interaction.text = buffer.data();
            interactions_.push_back(std::move(interaction));
        }
    }

    void renderColorPicker(
            const xiaoyv::api::ImGuiWidgetSnapshot& widget,
            float width,
            float height
    ) {
        ImVec4 source = argbToVec4(widget.color);
        float color[4] = {source.x, source.y, source.z, source.w};
        if (width > 0.0F) ImGui::SetNextItemWidth(width);
        const std::string id = itemId(widget.title.c_str(), widget.handle);
        if (ImGui::ColorPicker4(id.c_str(), color, ImGuiColorEditFlags_AlphaBar)) {
            xiaoyv::api::ImGuiInteraction interaction;
            interaction.type = xiaoyv::api::ImGuiInteractionType::ColorChanged;
            interaction.handle = widget.handle;
            interaction.color = vec4ToArgb(color);
            interactions_.push_back(std::move(interaction));
        }
        if (height > 0.0F) {
            ImGui::Dummy(ImVec2(0.0F, std::max(0.0F, height - ImGui::GetItemRectSize().y)));
        }
    }

    void renderCombo(const xiaoyv::api::ImGuiWidgetSnapshot& widget, float width) {
        if (width != 0.0F) ImGui::SetNextItemWidth(width);
        const char* preview = widget.selectedIndex >= 0
                && widget.selectedIndex < static_cast<int>(widget.items.size())
                ? widget.items[static_cast<std::size_t>(widget.selectedIndex)].c_str()
                : "";
        const std::string id = itemId("combo", widget.handle);
        if (ImGui::BeginCombo(id.c_str(), preview)) {
            for (int index = 0; index < static_cast<int>(widget.items.size()); ++index) {
                const bool selected = index == widget.selectedIndex;
                const std::string optionId = widget.items[static_cast<std::size_t>(index)]
                        + "##option_" + std::to_string(index);
                if (ImGui::Selectable(optionId.c_str(), selected)) {
                    xiaoyv::api::ImGuiInteraction interaction;
                    interaction.type = xiaoyv::api::ImGuiInteractionType::SelectionChanged;
                    interaction.handle = widget.handle;
                    interaction.index = index;
                    interactions_.push_back(std::move(interaction));
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    void renderRadioGroup(const xiaoyv::api::ImGuiWidgetSnapshot& widget) {
        if (!widget.title.empty()) {
            ImGui::TextUnformatted(widget.title.c_str());
        }
        for (int index = 0; index < static_cast<int>(widget.items.size()); ++index) {
            // wrapline 属于前一个单选项：false 让下一项同行，true 让下一项另起一行。
            if (index > 0) {
                const std::size_t previous = static_cast<std::size_t>(index - 1);
                const bool wrapAfterPrevious = previous < widget.radioWrapAfter.size()
                        && widget.radioWrapAfter[previous] != 0;
                if (!wrapAfterPrevious) ImGui::SameLine();
            }
            const std::string id = widget.items[static_cast<std::size_t>(index)]
                    + "##radio_" + std::to_string(index);
            if (ImGui::RadioButton(id.c_str(), widget.selectedIndex == index)) {
                xiaoyv::api::ImGuiInteraction interaction;
                interaction.type = xiaoyv::api::ImGuiInteractionType::SelectionChanged;
                interaction.handle = widget.handle;
                interaction.index = index;
                interactions_.push_back(std::move(interaction));
            }
        }
    }

    void renderTable(
            const xiaoyv::api::ImGuiWidgetSnapshot& widget,
            float width,
            float height
    ) {
        const std::string id = itemId(widget.title.c_str(), widget.handle);
        ImGuiTableFlags flags = ImGuiTableFlags_Borders
                | ImGuiTableFlags_RowBg
                | ImGuiTableFlags_ScrollY
                | ImGuiTableFlags_Resizable;
        if (!ImGui::BeginTable(id.c_str(), widget.columns, flags, ImVec2(width, height))) {
            return;
        }
        for (int column = 0; column < widget.columns; ++column) {
            const std::string& title = widget.headers[static_cast<std::size_t>(column)];
            ImGui::TableSetupColumn(title.empty() ? " " : title.c_str());
        }
        if (widget.showHeader) ImGui::TableHeadersRow();

        for (int row = 0; row < static_cast<int>(widget.rows.size()); ++row) {
            ImGui::TableNextRow();
            for (int column = 0; column < widget.columns; ++column) {
                ImGui::TableSetColumnIndex(column);
                const std::string& text = widget.rows[static_cast<std::size_t>(row)]
                        [static_cast<std::size_t>(column)];
                const std::string cellId = (text.empty() ? " " : text)
                        + "##cell_" + std::to_string(row) + "_" + std::to_string(column);
                const bool selected = widget.selectedRow == row && widget.selectedColumn == column;
                if (ImGui::Selectable(cellId.c_str(), selected)) {
                    xiaoyv::api::ImGuiInteraction interaction;
                    interaction.type = xiaoyv::api::ImGuiInteractionType::TableCellSelected;
                    interaction.handle = widget.handle;
                    interaction.row = row;
                    interaction.column = column;
                    interactions_.push_back(std::move(interaction));
                }
            }
        }
        ImGui::EndTable();
    }

    void renderShapes(
            const xiaoyv::api::ImGuiRenderSnapshot& snapshot,
            std::unordered_set<xiaoyv::api::ImGuiHandle>* activeTextures
    ) {
        if (surfaceCollapsed_) return;
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        for (const xiaoyv::api::ImGuiShapeSnapshot& shape : snapshot.shapes) {
            if (!shape.visible) continue;
            const ImU32 color = argbToImU32(shape.color);
            switch (shape.kind) {
                case xiaoyv::api::ImGuiShapeKind::Rectangle:
                    if (shape.filled) {
                        drawList->AddRectFilled(
                                ImVec2(shape.x, shape.y),
                                ImVec2(shape.x2, shape.y2),
                                color,
                                shape.rounding
                        );
                    } else {
                        drawList->AddRect(
                                ImVec2(shape.x, shape.y),
                                ImVec2(shape.x2, shape.y2),
                                color,
                                shape.rounding,
                                0,
                                shape.thickness
                        );
                    }
                    break;
                case xiaoyv::api::ImGuiShapeKind::Circle:
                    if (shape.filled) {
                        drawList->AddCircleFilled(
                                ImVec2(shape.x, shape.y),
                                shape.radius,
                                color,
                                shape.segments
                        );
                    } else {
                        drawList->AddCircle(
                                ImVec2(shape.x, shape.y),
                                shape.radius,
                                color,
                                shape.segments,
                                shape.thickness
                        );
                    }
                    break;
                case xiaoyv::api::ImGuiShapeKind::Polygon: {
                    std::vector<ImVec2> points;
                    points.reserve(shape.points.size());
                    for (const auto& point : shape.points) {
                        points.emplace_back(point.first, point.second);
                    }
                    if (shape.filled) {
                        drawList->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), color);
                    } else {
                        drawList->AddPolyline(
                                points.data(),
                                static_cast<int>(points.size()),
                                color,
                                shape.closed ? ImDrawFlags_Closed : ImDrawFlags_None,
                                shape.thickness
                        );
                    }
                    break;
                }
                case xiaoyv::api::ImGuiShapeKind::Line:
                    drawList->AddLine(
                            ImVec2(shape.x, shape.y),
                            ImVec2(shape.x2, shape.y2),
                            color,
                            shape.thickness
                    );
                    break;
                case xiaoyv::api::ImGuiShapeKind::Bitmap: {
                    ImTextureRef texture = textureFor(shape.handle, shape.image, activeTextures);
                    if (texture.GetTexID() != ImTextureID_Invalid) {
                        drawList->AddImage(
                                texture,
                                ImVec2(shape.x, shape.y),
                                ImVec2(shape.x + shape.width, shape.y + shape.height)
                        );
                    }
                    break;
                }
                case xiaoyv::api::ImGuiShapeKind::Text: {
                    if (shape.hasBackground) {
                        drawList->AddRectFilled(
                                ImVec2(shape.x, shape.y),
                                ImVec2(shape.x + shape.width, shape.y + shape.height),
                                argbToImU32(shape.backgroundColor)
                        );
                    }
                    const float fontSize = ImGui::GetFontSize() * shape.fontScale;
                    const ImVec4 clip(
                            shape.x,
                            shape.y,
                            shape.x + shape.width,
                            shape.y + shape.height
                    );
                    drawList->AddText(
                            ImGui::GetFont(),
                            fontSize,
                            ImVec2(shape.x, shape.y),
                            argbToImU32(shape.textColor),
                            shape.text.c_str(),
                            nullptr,
                            shape.width,
                            &clip
                    );
                    break;
                }
            }
        }
    }
};

std::mutex gRendererMutex;
std::unique_ptr<Renderer> gRenderer;

void enqueue(PendingInput input) {
    std::lock_guard<std::mutex> lock(gRendererMutex);
    if (gRenderer != nullptr) {
        gRenderer->enqueue(std::move(input));
    }
}

} // namespace

bool attachImGuiSurface(ANativeWindow* window) {
    if (window == nullptr) {
        return false;
    }
    std::unique_ptr<Renderer> previous;
    {
        std::lock_guard<std::mutex> lock(gRendererMutex);
        previous = std::move(gRenderer);
    }
    previous.reset();

    auto renderer = std::make_unique<Renderer>(window);
    if (!renderer->start()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(gRendererMutex);
        gRenderer = std::move(renderer);
    }
    return true;
}

void detachImGuiSurface() {
    std::unique_ptr<Renderer> renderer;
    {
        std::lock_guard<std::mutex> lock(gRendererMutex);
        renderer = std::move(gRenderer);
    }
    renderer.reset();
}

void enqueueImGuiTouch(int action, int pointerId, float x, float y) {
    PendingInput input;
    input.kind = InputKind::Touch;
    input.action = action;
    input.pointerId = pointerId;
    input.x = x;
    input.y = y;
    enqueue(std::move(input));
}

void enqueueImGuiText(const char* utf8Text) {
    if (utf8Text == nullptr || utf8Text[0] == '\0') {
        return;
    }
    PendingInput input;
    input.kind = InputKind::Text;
    input.text = utf8Text;
    enqueue(std::move(input));
}

void enqueueImGuiKey(int action, int keyCode, int unicodeCodePoint, int metaState) {
    PendingInput input;
    input.kind = InputKind::Key;
    input.action = action;
    input.keyCode = keyCode;
    input.unicodeCodePoint = unicodeCodePoint;
    input.metaState = metaState;
    enqueue(std::move(input));
}

void enqueueImGuiScroll(float horizontal, float vertical) {
    PendingInput input;
    input.kind = InputKind::Scroll;
    input.x = horizontal;
    input.y = vertical;
    enqueue(std::move(input));
}
