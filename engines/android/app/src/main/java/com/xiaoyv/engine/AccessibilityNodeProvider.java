/**
 * 文件用途：把主进程无障碍节点能力安全转发给 :engine 脚本进程。
 */
package com.xiaoyv.engine;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Binder;

import org.json.JSONObject;

public final class AccessibilityNodeProvider extends ContentProvider {
    static final String RESPONSE_KEY = "response";

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Bundle call(String method, String arg, Bundle extras) {
        Bundle result = new Bundle();
        int callerUid = Binder.getCallingUid();
        int appUid = getContext() == null ? -1 : getContext().getApplicationInfo().uid;
        if (callerUid != 0 && callerUid != appUid) {
            result.putString(RESPONSE_KEY, "{\"ok\":false,\"error\":\"无障碍节点调用方无权限\"}");
            return result;
        }
        try {
            JSONObject arguments = arg == null || arg.trim().isEmpty()
                    ? new JSONObject()
                    : new JSONObject(arg);
            Object value = AccessibilityNodePlatformBridge.callLocal(
                    getContext(),
                    method == null ? "" : method,
                    arguments
            );
            JSONObject envelope = new JSONObject();
            envelope.put("ok", true);
            envelope.put("value", value == null ? JSONObject.NULL : value);
            result.putString(RESPONSE_KEY, envelope.toString());
        } catch (Exception exception) {
            JSONObject envelope = new JSONObject();
            try {
                envelope.put("ok", false);
                envelope.put(
                        "error",
                        exception.getMessage() == null
                                ? "无障碍节点操作失败"
                                : exception.getMessage()
                );
                result.putString(RESPONSE_KEY, envelope.toString());
            } catch (Exception ignored) {
                result.putString(
                        RESPONSE_KEY,
                        "{\"ok\":false,\"error\":\"无障碍节点操作失败\"}"
                );
            }
        }
        return result;
    }

    @Override
    public String getType(Uri uri) {
        return null;
    }

    @Override
    public Cursor query(
            Uri uri,
            String[] projection,
            String selection,
            String[] selectionArgs,
            String sortOrder
    ) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(
            Uri uri,
            ContentValues values,
            String selection,
            String[] selectionArgs
    ) {
        return 0;
    }
}
