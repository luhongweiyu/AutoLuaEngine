/**
 * 文件用途：Root helper 进程内的输入注入实现，负责触摸、按键和文本输入。
 */
package com.autolua.engine;

import android.os.SystemClock;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;

import java.lang.reflect.Method;
import java.util.HashMap;
import java.util.Map;

/**
 * Root 输入注入器。
 *
 * 该类只在 `su -c app_process ... RootDaemonMain` 启动的 RootDaemon 特权进程中使用。
 * 所有事件直接通过系统 InputManager 注入，不走无障碍，也不每次拉起外部 input 命令。
 */
final class RootInputInjector {
    private static final int MAX_POINTER_ID = 4;
    private static final int INJECT_INPUT_EVENT_MODE_WAIT_FOR_FINISH = 2;

    private final boolean[] pointerActive = new boolean[MAX_POINTER_ID + 1];
    private final float[] pointerX = new float[MAX_POINTER_ID + 1];
    private final float[] pointerY = new float[MAX_POINTER_ID + 1];
    private final Map<Integer, Long> keyDownTimes = new HashMap<>();

    private Object inputManager;
    private Method injectInputEventMethod;
    private long gestureDownTime;

    boolean touchDown(int id, int x, int y) {
        if (!isValidPointerId(id) || pointerActive[id]) {
            return false;
        }

        int activeCountBefore = activePointerCount();
        long now = SystemClock.uptimeMillis();
        if (activeCountBefore == 0) {
            gestureDownTime = now;
        }

        pointerActive[id] = true;
        pointerX[id] = x;
        pointerY[id] = y;

        int actionIndex = pointerIndexOf(id);
        int action = activeCountBefore == 0
                ? MotionEvent.ACTION_DOWN
                : MotionEvent.ACTION_POINTER_DOWN
                | (actionIndex << MotionEvent.ACTION_POINTER_INDEX_SHIFT);
        return injectMotion(action, now);
    }

    boolean touchMove(int id, int x, int y) {
        if (!isValidPointerId(id) || !pointerActive[id]) {
            return false;
        }

        pointerX[id] = x;
        pointerY[id] = y;
        return injectMotion(MotionEvent.ACTION_MOVE, SystemClock.uptimeMillis());
    }

    boolean touchUp(int id) {
        if (!isValidPointerId(id) || !pointerActive[id]) {
            return false;
        }

        int activeCount = activePointerCount();
        int actionIndex = pointerIndexOf(id);
        int action = activeCount == 1
                ? MotionEvent.ACTION_UP
                : MotionEvent.ACTION_POINTER_UP
                | (actionIndex << MotionEvent.ACTION_POINTER_INDEX_SHIFT);
        boolean ok = injectMotion(action, SystemClock.uptimeMillis());
        if (ok) {
            pointerActive[id] = false;
            if (activeCount == 1) {
                gestureDownTime = 0L;
            }
        }
        return ok;
    }

    boolean keyDown(int keyCode) {
        if (keyCode <= 0) {
            return false;
        }

        long now = SystemClock.uptimeMillis();
        if (!keyDownTimes.containsKey(keyCode)) {
            keyDownTimes.put(keyCode, now);
        }
        return injectKey(KeyEvent.ACTION_DOWN, keyCode, keyDownTimes.get(keyCode), now);
    }

    boolean keyUp(int keyCode) {
        if (keyCode <= 0) {
            return false;
        }

        long now = SystemClock.uptimeMillis();
        Long downTime = keyDownTimes.remove(keyCode);
        return injectKey(KeyEvent.ACTION_UP, keyCode, downTime == null ? now : downTime, now);
    }

    boolean keyPress(int keyCode) {
        if (keyCode <= 0) {
            return false;
        }

        long downTime = SystemClock.uptimeMillis();
        boolean downOk = injectKey(KeyEvent.ACTION_DOWN, keyCode, downTime, downTime);
        long upTime = SystemClock.uptimeMillis();
        boolean upOk = injectKey(KeyEvent.ACTION_UP, keyCode, downTime, upTime);
        return downOk && upOk;
    }

    boolean inputText(String text) {
        if (text == null || text.isEmpty()) {
            return true;
        }

        KeyCharacterMap keyCharacterMap = KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD);
        KeyEvent[] events = keyCharacterMap.getEvents(text.toCharArray());
        if (events == null || events.length == 0) {
            return false;
        }

        boolean ok = true;
        for (KeyEvent event : events) {
            ok = injectEvent(event) && ok;
        }
        return ok;
    }

    private boolean injectMotion(int action, long eventTime) {
        int pointerCount = activePointerCount();
        if (pointerCount <= 0) {
            return false;
        }

        MotionEvent.PointerProperties[] properties =
                new MotionEvent.PointerProperties[pointerCount];
        MotionEvent.PointerCoords[] coords = new MotionEvent.PointerCoords[pointerCount];

        int index = 0;
        for (int id = 0; id <= MAX_POINTER_ID; id++) {
            if (!pointerActive[id]) {
                continue;
            }

            MotionEvent.PointerProperties pointerProperties = new MotionEvent.PointerProperties();
            pointerProperties.id = id;
            pointerProperties.toolType = MotionEvent.TOOL_TYPE_FINGER;
            properties[index] = pointerProperties;

            MotionEvent.PointerCoords pointerCoords = new MotionEvent.PointerCoords();
            pointerCoords.x = pointerX[id];
            pointerCoords.y = pointerY[id];
            pointerCoords.pressure = 1.0f;
            pointerCoords.size = 1.0f;
            coords[index] = pointerCoords;
            index++;
        }

        long downTime = gestureDownTime == 0L ? eventTime : gestureDownTime;
        MotionEvent event = MotionEvent.obtain(
                downTime,
                eventTime,
                action,
                pointerCount,
                properties,
                coords,
                0,
                0,
                1.0f,
                1.0f,
                0,
                0,
                InputDevice.SOURCE_TOUCHSCREEN,
                0
        );
        try {
            return injectEvent(event);
        } finally {
            event.recycle();
        }
    }

    private boolean injectKey(int action, int keyCode, long downTime, long eventTime) {
        KeyEvent event = new KeyEvent(
                downTime,
                eventTime,
                action,
                keyCode,
                0,
                0,
                KeyCharacterMap.VIRTUAL_KEYBOARD,
                0,
                0,
                InputDevice.SOURCE_KEYBOARD
        );
        return injectEvent(event);
    }

    private boolean injectEvent(InputEvent event) {
        try {
            ensureInputManager();
            Boolean result = (Boolean) injectInputEventMethod.invoke(
                    inputManager,
                    event,
                    INJECT_INPUT_EVENT_MODE_WAIT_FOR_FINISH
            );
            return result != null && result;
        } catch (ReflectiveOperationException | RuntimeException exception) {
            return false;
        }
    }

    private void ensureInputManager() throws ReflectiveOperationException {
        if (inputManager != null && injectInputEventMethod != null) {
            return;
        }

        Class<?> inputManagerClass = Class.forName("android.hardware.input.InputManager");
        Method getInstanceMethod = inputManagerClass.getDeclaredMethod("getInstance");
        getInstanceMethod.setAccessible(true);
        inputManager = getInstanceMethod.invoke(null);

        injectInputEventMethod = inputManagerClass.getDeclaredMethod(
                "injectInputEvent",
                InputEvent.class,
                int.class
        );
        injectInputEventMethod.setAccessible(true);
    }

    private int activePointerCount() {
        int count = 0;
        for (boolean active : pointerActive) {
            if (active) {
                count++;
            }
        }
        return count;
    }

    private int pointerIndexOf(int pointerId) {
        int index = 0;
        for (int id = 0; id <= MAX_POINTER_ID; id++) {
            if (!pointerActive[id]) {
                continue;
            }
            if (id == pointerId) {
                return index;
            }
            index++;
        }
        return -1;
    }

    private static boolean isValidPointerId(int id) {
        return id >= 0 && id <= MAX_POINTER_ID;
    }
}
