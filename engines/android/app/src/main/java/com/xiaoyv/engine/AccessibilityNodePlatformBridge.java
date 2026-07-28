/**
 * 文件用途：实现懒人精灵形状的无障碍选择器、节点属性与节点动作。
 */
package com.xiaoyv.engine;

import android.accessibilityservice.AccessibilityService;
import android.content.Context;
import android.content.Intent;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.provider.Settings;
import android.view.accessibility.AccessibilityNodeInfo;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;
import java.util.regex.Pattern;
import java.util.regex.PatternSyntaxException;

final class AccessibilityNodePlatformBridge {
    private static final int MAX_HANDLES = 512;
    private static final int MAX_TRAVERSED_NODES = 10000;
    private static final AtomicLong NEXT_HANDLE = new AtomicLong(1);
    private static final Object HANDLE_LOCK = new Object();
    private static final LinkedHashMap<Long, NodeHandle> HANDLES =
            new LinkedHashMap<>(64, 0.75f, true);
    private static AccessibilityNodeInfo lockedRoot;

    private AccessibilityNodePlatformBridge() {
    }

    static Object callThroughProvider(Context context, String operation, JSONObject arguments)
            throws Exception {
        Uri uri = Uri.parse("content://" + context.getPackageName() + ".accessibility");
        Bundle bundle = context.getContentResolver().call(
                uri,
                operation,
                arguments == null ? "{}" : arguments.toString(),
                null
        );
        String response = bundle == null
                ? null
                : bundle.getString(AccessibilityNodeProvider.RESPONSE_KEY);
        if (response == null || response.isEmpty()) {
            throw new IllegalArgumentException("无障碍节点服务无响应");
        }
        JSONObject envelope = new JSONObject(response);
        if (!envelope.optBoolean("ok", false)) {
            throw new IllegalArgumentException(
                    envelope.optString("error", "无障碍节点操作失败")
            );
        }
        return envelope.opt("value");
    }

    static Object callLocal(Context context, String operation, JSONObject arguments)
            throws Exception {
        switch (operation) {
            case "node.query":
                return query(arguments);
            case "node.selectorAction":
                return selectorAction(arguments);
            case "node.relation":
                return relation(arguments);
            case "node.action":
                return nodeAction(arguments);
            case "node.toJson":
                return nodeJson(arguments.getLong("handle"));
            case "node.xml":
                return nodeXml();
            case "node.save":
                return saveNode(arguments.getString("path"));
            case "node.lock":
                return lockNode();
            case "node.unlock":
                unlockNode();
                return true;
            case "node.release":
                releaseLocalState();
                return true;
            case "node.openAccessibility":
                return openAccessibility(context);
            case "node.closeAccessibility":
                return closeAccessibility(context);
            default:
                throw new IllegalArgumentException("不支持的节点能力：" + operation);
        }
    }

    static void onServiceDisconnected() {
        releaseLocalState();
    }

    /**
     * 由 :engine 进程在单次脚本结束后通知主进程清理节点快照。
     *
     * 清理失败不应覆盖脚本原始结束状态；服务断开时还会再次执行本地兜底清理。
     */
    static void releaseScriptState(Context context) {
        try {
            callThroughProvider(context, "node.release", new JSONObject());
        } catch (Exception ignored) {
            // ContentProvider 或无障碍服务正在销毁时无需重复报告清理错误。
        }
    }

    private static void releaseLocalState() {
        synchronized (HANDLE_LOCK) {
            for (NodeHandle handle : HANDLES.values()) {
                recycle(handle.node);
            }
            HANDLES.clear();
            recycle(lockedRoot);
            lockedRoot = null;
        }
    }

    private static JSONArray query(JSONObject arguments) throws Exception {
        JSONArray filters = arguments.optJSONArray("filters");
        int timeoutMs = Math.max(0, arguments.optInt("timeout", 0));
        int limit = Math.max(1, Math.min(arguments.optInt("limit", MAX_HANDLES), MAX_HANDLES));
        long deadline = SystemClock.uptimeMillis() + timeoutMs;

        do {
            JSONArray result = queryOnce(filters, limit);
            if (result.length() > 0 || SystemClock.uptimeMillis() >= deadline) {
                return result;
            }
            SystemClock.sleep(Math.min(50, Math.max(1, deadline - SystemClock.uptimeMillis())));
        } while (true);
    }

    private static JSONArray queryOnce(JSONArray filters, int limit) throws Exception {
        AccessibilityNodeInfo root = obtainQueryRoot();
        JSONArray result = new JSONArray();
        if (root == null) {
            return result;
        }

        ArrayDeque<NodeRecord> pending = new ArrayDeque<>();
        pending.add(new NodeRecord(root, 0, 0));
        int traversed = 0;
        try {
            while (!pending.isEmpty() && result.length() < limit
                    && traversed++ < MAX_TRAVERSED_NODES) {
                NodeRecord record = pending.removeFirst();
                AccessibilityNodeInfo node = record.node;
                try {
                    if (matches(node, record, filters)) {
                        result.put(register(node, record.depth, record.index));
                    }
                    int childCount = node.getChildCount();
                    for (int index = 0; index < childCount; index++) {
                        AccessibilityNodeInfo child = node.getChild(index);
                        if (child != null) {
                            pending.addLast(new NodeRecord(child, record.depth + 1, index));
                        }
                    }
                } finally {
                    recycle(node);
                }
            }
        } finally {
            while (!pending.isEmpty()) {
                recycle(pending.removeFirst().node);
            }
        }
        return result;
    }

    private static boolean selectorAction(JSONObject arguments) throws Exception {
        JSONArray matches = query(arguments);
        String action = arguments.optString("action", "click");
        boolean any = false;
        for (int index = 0; index < matches.length(); index++) {
            JSONObject item = matches.getJSONObject(index);
            NodeHandle handle = copyHandle(item.getLong("handle"));
            try {
                any = performSimpleAction(handle.node, action) || any;
            } finally {
                recycle(handle.node);
            }
        }
        return any;
    }

    private static Object relation(JSONObject arguments) throws Exception {
        NodeHandle handle = copyHandle(arguments.getLong("handle"));
        try {
            String relation = arguments.optString("relation");
            if ("parent".equals(relation)) {
                AccessibilityNodeInfo parent = handle.node.getParent();
                if (parent == null) {
                    return JSONObject.NULL;
                }
                try {
                    return register(parent, Math.max(0, handle.depth - 1), -1);
                } finally {
                    recycle(parent);
                }
            }
            if ("children".equals(relation)) {
                JSONArray children = new JSONArray();
                for (int index = 0; index < handle.node.getChildCount(); index++) {
                    AccessibilityNodeInfo child = handle.node.getChild(index);
                    if (child == null) {
                        continue;
                    }
                    try {
                        children.put(register(child, handle.depth + 1, index));
                    } finally {
                        recycle(child);
                    }
                }
                return children;
            }
            throw new IllegalArgumentException("未知节点关系：" + relation);
        } finally {
            recycle(handle.node);
        }
    }

    private static boolean nodeAction(JSONObject arguments) throws Exception {
        NodeHandle handle = copyHandle(arguments.getLong("handle"));
        try {
            AccessibilityNodeInfo node = handle.node;
            String action = arguments.optString("action");
            Bundle bundle = new Bundle();
            switch (action) {
                case "setText":
                    bundle.putCharSequence(
                            AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE,
                            arguments.optString("text", "")
                    );
                    return node.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, bundle);
                case "scrollTo":
                    bundle.putInt(
                            AccessibilityNodeInfo.ACTION_ARGUMENT_ROW_INT,
                            arguments.optInt("row", 0)
                    );
                    bundle.putInt(
                            AccessibilityNodeInfo.ACTION_ARGUMENT_COLUMN_INT,
                            arguments.optInt("column", 0)
                    );
                    return node.performAction(
                            AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_TO_POSITION.getId(),
                            bundle
                    );
                case "setSelection":
                    bundle.putInt(
                            AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_START_INT,
                            arguments.optInt("start", 0)
                    );
                    bundle.putInt(
                            AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_END_INT,
                            arguments.optInt("end", 0)
                    );
                    return node.performAction(AccessibilityNodeInfo.ACTION_SET_SELECTION, bundle);
                case "setProgress":
                    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
                        return false;
                    }
                    bundle.putFloat(
                            AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE,
                            (float) arguments.optDouble("position", 0)
                    );
                    return node.performAction(
                            AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS.getId(),
                            bundle
                    );
                default:
                    return performSimpleAction(node, action);
            }
        } finally {
            recycle(handle.node);
        }
    }

    private static boolean performSimpleAction(AccessibilityNodeInfo node, String action) {
        switch (action) {
            case "click":
                return node.performAction(AccessibilityNodeInfo.ACTION_CLICK);
            case "longClick":
                return node.performAction(AccessibilityNodeInfo.ACTION_LONG_CLICK);
            case "focus":
                return node.performAction(AccessibilityNodeInfo.ACTION_FOCUS);
            case "clearFocus":
                return node.performAction(AccessibilityNodeInfo.ACTION_CLEAR_FOCUS);
            case "copy":
                return node.performAction(AccessibilityNodeInfo.ACTION_COPY);
            case "paste":
                return node.performAction(AccessibilityNodeInfo.ACTION_PASTE);
            case "cut":
                return node.performAction(AccessibilityNodeInfo.ACTION_CUT);
            case "select":
                return node.performAction(AccessibilityNodeInfo.ACTION_SELECT);
            case "scrollUp":
                return node.performAction(
                        AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_UP.getId()
                );
            case "scrollDown":
                return node.performAction(
                        AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_DOWN.getId()
                );
            case "scrollLeft":
                return node.performAction(
                        AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_LEFT.getId()
                );
            case "scrollRight":
                return node.performAction(
                        AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_RIGHT.getId()
                );
            case "scrollForward":
                return node.performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD);
            case "scrollBackward":
                return node.performAction(AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD);
            case "collapse":
                return node.performAction(
                        AccessibilityNodeInfo.AccessibilityAction.ACTION_COLLAPSE.getId()
                );
            case "expand":
                return node.performAction(
                        AccessibilityNodeInfo.AccessibilityAction.ACTION_EXPAND.getId()
                );
            case "contextClick":
                return node.performAction(
                        AccessibilityNodeInfo.AccessibilityAction.ACTION_CONTEXT_CLICK.getId()
                );
            default:
                throw new IllegalArgumentException("未知节点动作：" + action);
        }
    }

    private static boolean matches(
            AccessibilityNodeInfo node,
            NodeRecord record,
            JSONArray filters
    ) throws JSONException {
        if (filters == null) {
            return true;
        }
        for (int index = 0; index < filters.length(); index++) {
            JSONObject filter = filters.getJSONObject(index);
            String field = filter.optString("field");
            String matcher = filter.optString("matcher", "exact");
            if ("bounds".equals(field) || "boundsInside".equals(field)) {
                Rect bounds = new Rect();
                node.getBoundsInScreen(bounds);
                Rect target = rect(filter.optJSONArray("value"));
                if ("bounds".equals(field) ? !bounds.equals(target) : !target.contains(bounds)) {
                    return false;
                }
                continue;
            }
            if ("drawingOrder".equals(field)) {
                if (drawingOrder(node) != filter.optInt("value", Integer.MIN_VALUE)) {
                    return false;
                }
                continue;
            }
            if ("depth".equals(field)) {
                if (record.depth != filter.optInt("value", Integer.MIN_VALUE)) {
                    return false;
                }
                continue;
            }
            if ("index".equals(field)) {
                if (record.index != filter.optInt("value", Integer.MIN_VALUE)) {
                    return false;
                }
                continue;
            }
            Boolean booleanValue = booleanProperty(node, field);
            if (booleanValue != null) {
                if (booleanValue != filter.optBoolean("value")) {
                    return false;
                }
                continue;
            }
            if (!matchText(stringProperty(node, field), filter.optString("value", ""), matcher)) {
                return false;
            }
        }
        return true;
    }

    private static String stringProperty(AccessibilityNodeInfo node, String field) {
        CharSequence value;
        switch (field) {
            case "id":
                return empty(node.getViewIdResourceName());
            case "text":
                value = node.getText();
                return value == null ? "" : value.toString();
            case "desc":
                value = node.getContentDescription();
                return value == null ? "" : value.toString();
            case "className":
                value = node.getClassName();
                return value == null ? "" : value.toString();
            case "packageName":
                value = node.getPackageName();
                return value == null ? "" : value.toString();
            default:
                throw new IllegalArgumentException("未知节点筛选字段：" + field);
        }
    }

    private static Boolean booleanProperty(AccessibilityNodeInfo node, String field) {
        switch (field) {
            case "visibleToUser":
                return node.isVisibleToUser();
            case "selected":
                return node.isSelected();
            case "clickable":
                return node.isClickable();
            case "longClickable":
                return node.isLongClickable();
            case "enabled":
                return node.isEnabled();
            case "password":
                return node.isPassword();
            case "scrollable":
                return node.isScrollable();
            case "checked":
                return node.isChecked();
            case "checkable":
                return node.isCheckable();
            case "focusable":
                return node.isFocusable();
            case "focused":
                return node.isFocused();
            default:
                return null;
        }
    }

    private static boolean matchText(String actual, String expected, String matcher) {
        switch (matcher) {
            case "exact":
                return actual.equals(expected);
            case "contains":
                return actual.contains(expected);
            case "startsWith":
                return actual.startsWith(expected);
            case "endsWith":
                return actual.endsWith(expected);
            case "matches":
                try {
                    return Pattern.compile(expected).matcher(actual).find();
                } catch (PatternSyntaxException exception) {
                    throw new IllegalArgumentException("节点正则表达式无效：" + exception.getMessage());
                }
            default:
                throw new IllegalArgumentException("未知节点匹配方式：" + matcher);
        }
    }

    private static JSONObject register(AccessibilityNodeInfo node, int depth, int index)
            throws JSONException {
        long handleId = NEXT_HANDLE.getAndIncrement();
        AccessibilityNodeInfo copy = AccessibilityNodeInfo.obtain(node);
        synchronized (HANDLE_LOCK) {
            NodeHandle handle = new NodeHandle(copy, depth, index);
            HANDLES.put(handleId, handle);
            while (HANDLES.size() > MAX_HANDLES) {
                Iterator<Map.Entry<Long, NodeHandle>> iterator = HANDLES.entrySet().iterator();
                if (!iterator.hasNext()) {
                    break;
                }
                Map.Entry<Long, NodeHandle> eldest = iterator.next();
                iterator.remove();
                recycle(eldest.getValue().node);
            }
            return snapshot(handle, handleId);
        }
    }

    private static String nodeJson(long handleId) throws JSONException {
        NodeHandle handle = copyHandle(handleId);
        try {
            return snapshot(handle, handleId).toString();
        } finally {
            recycle(handle.node);
        }
    }

    private static JSONObject snapshot(NodeHandle handle, long handleId) throws JSONException {
        AccessibilityNodeInfo node = handle.node;
        Rect bounds = new Rect();
        Rect parentBounds = new Rect();
        node.getBoundsInScreen(bounds);
        node.getBoundsInParent(parentBounds);
        JSONObject result = new JSONObject();
        result.put("handle", handleId);
        result.put("id", empty(node.getViewIdResourceName()));
        result.put("text", text(node.getText()));
        result.put("desc", text(node.getContentDescription()));
        result.put("className", text(node.getClassName()));
        result.put("packageName", text(node.getPackageName()));
        result.put("bounds", rectJson(bounds));
        result.put("boundsInParent", rectJson(parentBounds));
        result.put("childCount", node.getChildCount());
        result.put("drawingOrder", drawingOrder(node));
        result.put("depth", handle.depth);
        result.put("index", handle.index);
        result.put("visibleToUser", node.isVisibleToUser());
        result.put("selected", node.isSelected());
        result.put("clickable", node.isClickable());
        result.put("longClickable", node.isLongClickable());
        result.put("enabled", node.isEnabled());
        result.put("password", node.isPassword());
        result.put("scrollable", node.isScrollable());
        result.put("checked", node.isChecked());
        result.put("checkable", node.isCheckable());
        result.put("focusable", node.isFocusable());
        result.put("focused", node.isFocused());
        return result;
    }

    private static NodeHandle copyHandle(long handle) {
        synchronized (HANDLE_LOCK) {
            NodeHandle value = HANDLES.get(handle);
            if (value == null) {
                throw new IllegalArgumentException("节点句柄已失效：" + handle);
            }
            return new NodeHandle(
                    AccessibilityNodeInfo.obtain(value.node),
                    value.depth,
                    value.index
            );
        }
    }

    private static AccessibilityNodeInfo obtainQueryRoot() {
        synchronized (HANDLE_LOCK) {
            if (lockedRoot != null) {
                return AccessibilityNodeInfo.obtain(lockedRoot);
            }
        }
        AutomationAccessibilityService service = AutomationAccessibilityService.current();
        AccessibilityNodeInfo root = service == null ? null : service.getRootInActiveWindow();
        if (root == null) {
            return null;
        }
        try {
            return AccessibilityNodeInfo.obtain(root);
        } finally {
            recycle(root);
        }
    }

    private static String nodeXml() throws Exception {
        AccessibilityNodeInfo root = obtainQueryRoot();
        if (root == null) {
            return null;
        }
        StringBuilder xml = new StringBuilder("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        try {
            appendXml(root, xml, 0, 0);
        } finally {
            recycle(root);
        }
        return xml.toString();
    }

    private static void appendXml(
            AccessibilityNodeInfo node,
            StringBuilder output,
            int depth,
            int index
    ) {
        Rect bounds = new Rect();
        node.getBoundsInScreen(bounds);
        indent(output, depth);
        output.append("<node")
                .append(" index=\"").append(index).append('"')
                .append(" text=\"").append(xml(text(node.getText()))).append('"')
                .append(" resource-id=\"").append(xml(empty(node.getViewIdResourceName()))).append('"')
                .append(" class=\"").append(xml(text(node.getClassName()))).append('"')
                .append(" package=\"").append(xml(text(node.getPackageName()))).append('"')
                .append(" content-desc=\"").append(xml(text(node.getContentDescription()))).append('"')
                .append(" clickable=\"").append(node.isClickable()).append('"')
                .append(" enabled=\"").append(node.isEnabled()).append('"')
                .append(" scrollable=\"").append(node.isScrollable()).append('"')
                .append(" bounds=\"[").append(bounds.left).append(',').append(bounds.top)
                .append("][").append(bounds.right).append(',').append(bounds.bottom).append("]\"");
        int childCount = node.getChildCount();
        if (childCount == 0) {
            output.append("/>\n");
            return;
        }
        output.append(">\n");
        for (int childIndex = 0; childIndex < childCount; childIndex++) {
            AccessibilityNodeInfo child = node.getChild(childIndex);
            if (child != null) {
                try {
                    appendXml(child, output, depth + 1, childIndex);
                } finally {
                    recycle(child);
                }
            }
        }
        indent(output, depth);
        output.append("</node>\n");
    }

    private static boolean saveNode(String path) throws Exception {
        String xml = nodeXml();
        if (xml == null) {
            return false;
        }
        File file = new File(path);
        File parent = file.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IllegalArgumentException("无法创建节点文件目录：" + parent);
        }
        try (FileOutputStream output = new FileOutputStream(file, false)) {
            output.write(xml.getBytes(StandardCharsets.UTF_8));
        }
        return true;
    }

    private static boolean lockNode() {
        unlockNode();
        AutomationAccessibilityService service = AutomationAccessibilityService.current();
        AccessibilityNodeInfo root = service == null ? null : service.getRootInActiveWindow();
        if (root == null) {
            return false;
        }
        try {
            synchronized (HANDLE_LOCK) {
                lockedRoot = AccessibilityNodeInfo.obtain(root);
            }
            return true;
        } finally {
            recycle(root);
        }
    }

    private static void unlockNode() {
        synchronized (HANDLE_LOCK) {
            recycle(lockedRoot);
            lockedRoot = null;
        }
    }

    private static boolean openAccessibility(Context context) {
        if (AutomationAccessibilityService.isEnabled()) {
            return true;
        }
        if (setAccessibilityServiceEnabled(context, true)) {
            return true;
        }
        Intent intent = new Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        context.startActivity(intent);
        return false;
    }

    private static boolean closeAccessibility(Context context) {
        AutomationAccessibilityService service = AutomationAccessibilityService.current();
        if (service != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            service.disableSelf();
            return true;
        }
        return setAccessibilityServiceEnabled(context, false);
    }

    /**
     * Root 环境下精确增删本应用服务，不覆盖用户已经启用的其他无障碍组件。
     */
    private static boolean setAccessibilityServiceEnabled(Context context, boolean enabled) {
        String component = context.getPackageName() + "/"
                + AutomationAccessibilityService.class.getName();
        RootHelperBridge.ShellResult read = RootHelperBridge.executeShell(
                "settings get secure enabled_accessibility_services"
        );
        if (!read.success) {
            return false;
        }

        java.util.LinkedHashSet<String> components = new java.util.LinkedHashSet<>();
        for (String item : read.output.trim().split(":")) {
            String value = item.trim();
            if (value.matches("[A-Za-z0-9_.$]+/[A-Za-z0-9_.$]+")) {
                components.add(value);
            }
        }
        if (enabled) {
            components.add(component);
        } else {
            components.remove(component);
        }

        String value = android.text.TextUtils.join(":", components);
        String command = value.isEmpty()
                ? "settings delete secure enabled_accessibility_services"
                : "settings put secure enabled_accessibility_services '" + value + "'";
        if (enabled) {
            command += " && settings put secure accessibility_enabled 1";
        } else if (components.isEmpty()) {
            command += " && settings put secure accessibility_enabled 0";
        }
        RootHelperBridge.ShellResult write = RootHelperBridge.executeShell(command);
        if (!write.success) {
            return false;
        }

        RootHelperBridge.ShellResult verify = RootHelperBridge.executeShell(
                "settings get secure enabled_accessibility_services"
        );
        boolean present = verify.success
                && java.util.Arrays.asList(verify.output.trim().split(":")).contains(component);
        return enabled == present;
    }

    private static int drawingOrder(AccessibilityNodeInfo node) {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.N ? node.getDrawingOrder() : 0;
    }

    private static Rect rect(JSONArray value) throws JSONException {
        if (value == null || value.length() != 4) {
            throw new IllegalArgumentException("节点范围必须包含 left、top、right、bottom");
        }
        return new Rect(value.getInt(0), value.getInt(1), value.getInt(2), value.getInt(3));
    }

    private static JSONObject rectJson(Rect value) throws JSONException {
        JSONObject result = new JSONObject();
        result.put("left", value.left);
        result.put("top", value.top);
        result.put("right", value.right);
        result.put("bottom", value.bottom);
        return result;
    }

    private static String text(CharSequence value) {
        return value == null ? "" : value.toString();
    }

    private static String empty(String value) {
        return value == null ? "" : value;
    }

    private static String xml(String value) {
        return value.replace("&", "&amp;")
                .replace("\"", "&quot;")
                .replace("<", "&lt;")
                .replace(">", "&gt;");
    }

    private static void indent(StringBuilder output, int depth) {
        for (int index = 0; index < depth; index++) {
            output.append("  ");
        }
    }

    @SuppressWarnings("deprecation")
    private static void recycle(AccessibilityNodeInfo node) {
        if (node != null) {
            node.recycle();
        }
    }

    private static final class NodeRecord {
        private final AccessibilityNodeInfo node;
        private final int depth;
        private final int index;

        private NodeRecord(AccessibilityNodeInfo node, int depth, int index) {
            this.node = node;
            this.depth = depth;
            this.index = index;
        }
    }

    private static final class NodeHandle {
        private final AccessibilityNodeInfo node;
        private final int depth;
        private final int index;

        private NodeHandle(AccessibilityNodeInfo node, int depth, int index) {
            this.node = node;
            this.depth = depth;
            this.index = index;
        }
    }
}
