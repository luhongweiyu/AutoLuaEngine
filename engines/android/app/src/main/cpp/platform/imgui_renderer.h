/**
 * 文件用途：声明 Dear ImGui 的 Android Surface、EGL/OpenGL ES 渲染与输入队列入口。
 */
#pragma once

#include <android/native_window.h>

/**
 * 把 Java Surface 对应的 ANativeWindow 交给独立渲染线程。
 *
 * 函数内部会增加 native window 引用；调用方仍需释放 ANativeWindow_fromSurface 返回的
 * 原始引用。重复附着会先完整停止旧渲染线程再切换新 Surface。
 */
bool attachImGuiSurface(ANativeWindow* window);

/** 停止渲染线程并释放 EGL、OpenGL、纹理、Dear ImGui context 和 native window。 */
void detachImGuiSurface();

/** Java MotionEvent 转为渲染线程输入；action 使用 Android ACTION_* 数值。 */
void enqueueImGuiTouch(int action, int pointerId, float x, float y);

/** Java 输入法提交 UTF-8 文本，渲染线程在下一帧写入 Dear ImGui IO。 */
void enqueueImGuiText(const char* utf8Text);

/** Java KeyEvent 转为渲染线程按键输入。 */
void enqueueImGuiKey(int action, int keyCode, int unicodeCodePoint, int metaState);

/** Java 滚轮或触控板滚动输入。 */
void enqueueImGuiScroll(float horizontal, float vertical);
