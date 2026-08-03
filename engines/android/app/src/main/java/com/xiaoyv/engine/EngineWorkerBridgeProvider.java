/**
 * 文件用途：让 Root app_process 用一次性令牌把 Worker Binder 交给 :engine 控制进程。
 */
package com.xiaoyv.engine;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;

/** Provider 只负责首次 Binder 握手；脚本命令和大数据随后都走直接 Binder/管道。 */
public final class EngineWorkerBridgeProvider extends ContentProvider {
    static final String METHOD_REGISTER = "registerWorker";
    static final String EXTRA_TOKEN = "token";
    static final String EXTRA_BINDER = "workerBinder";
    static final String RESULT_ACCEPTED = "accepted";
    static final String METHOD_IME_COMMIT = "imeCommitText";
    static final String METHOD_IME_LOCK = "imeLock";
    static final String METHOD_IME_UNLOCK = "imeUnlock";
    static final String METHOD_IME_DELETE = "imeDeleteChar";
    static final String METHOD_IME_FINISH = "imeFinishInput";
    static final String METHOD_IME_KEY = "imeKeyEvent";
    static final String EXTRA_TEXT = "text";
    static final String EXTRA_ACTION = "action";
    static final String EXTRA_KEY_CODE = "keyCode";
    static final String METHOD_SET_ROOT_MODE = "setRootMode";
    static final String METHOD_READ_PASTEBOARD = "readPasteboard";
    static final String METHOD_WRITE_PASTEBOARD = "writePasteboard";
    static final String METHOD_UI_HOST = "uiHost";
    static final String EXTRA_ENABLED = "enabled";
    static final String RESULT_TEXT = "resultText";
    static final String EXTRA_UI_ACTION = "uiAction";
    static final String EXTRA_SESSION_ID = "sessionId";
    static final String EXTRA_PAYLOAD = "payload";
    static final String EXTRA_FLAG = "flag";

    @Override
    public boolean onCreate() {
        AndroidHostBridge.init(getContext());
        EngineWorkerCoordinator.initialize(getContext());
        return true;
    }

    @Override
    public Bundle call(String method, String arg, Bundle extras) {
        Bundle result = new Bundle();
        int callerUid = Binder.getCallingUid();
        if (callerUid != 0 && callerUid != android.os.Process.myUid()) {
            result.putBoolean(RESULT_ACCEPTED, false);
            return result;
        }

        if (METHOD_SET_ROOT_MODE.equals(method)) {
            boolean enabled = extras != null && extras.getBoolean(EXTRA_ENABLED, false);
            EngineSettings.setRootModeEnabled(getContext(), enabled);
            result.putBoolean(RESULT_ACCEPTED, true);
            return result;
        }
        if (METHOD_READ_PASTEBOARD.equals(method)) {
            result.putString(RESULT_TEXT, DevicePlatformBridge.readPasteboard(getContext()));
            result.putBoolean(RESULT_ACCEPTED, true);
            return result;
        }
        if (METHOD_WRITE_PASTEBOARD.equals(method)) {
            DevicePlatformBridge.writePasteboard(
                    getContext(),
                    extras == null ? "" : extras.getString(EXTRA_TEXT, "")
            );
            result.putBoolean(RESULT_ACCEPTED, true);
            return result;
        }
        if (METHOD_UI_HOST.equals(method)) {
            result.putBoolean(
                    RESULT_ACCEPTED,
                    EngineUiHost.call(
                            getContext(),
                            extras == null ? "" : extras.getString(EXTRA_UI_ACTION, ""),
                            extras == null ? 0L : extras.getLong(EXTRA_SESSION_ID, 0L),
                            extras == null ? "{}" : extras.getString(EXTRA_PAYLOAD, "{}"),
                            extras != null && extras.getBoolean(EXTRA_FLAG, false)
                    )
            );
            return result;
        }
        if (METHOD_IME_LOCK.equals(method)) {
            result.putBoolean(RESULT_ACCEPTED, EngineImeBridge.lockLocal());
            return result;
        }
        if (METHOD_IME_UNLOCK.equals(method)) {
            result.putBoolean(RESULT_ACCEPTED, EngineImeBridge.unlockLocal());
            return result;
        }
        if (METHOD_IME_COMMIT.equals(method)) {
            result.putBoolean(
                    RESULT_ACCEPTED,
                    EngineImeBridge.setTextLocal(
                            extras == null ? "" : extras.getString(EXTRA_TEXT, "")
                    )
            );
            return result;
        }
        if (METHOD_IME_DELETE.equals(method)) {
            result.putBoolean(RESULT_ACCEPTED, EngineImeBridge.deleteCharLocal());
            return result;
        }
        if (METHOD_IME_FINISH.equals(method)) {
            result.putBoolean(RESULT_ACCEPTED, EngineImeBridge.finishInputLocal());
            return result;
        }
        if (METHOD_IME_KEY.equals(method)) {
            result.putBoolean(
                    RESULT_ACCEPTED,
                    EngineImeBridge.keyEventLocal(
                            extras == null ? 0 : extras.getInt(EXTRA_ACTION),
                            extras == null ? 0 : extras.getInt(EXTRA_KEY_CODE)
                    )
            );
            return result;
        }

        if (!METHOD_REGISTER.equals(method) || extras == null) {
            result.putBoolean(RESULT_ACCEPTED, false);
            return result;
        }

        IBinder binder = extras.getBinder(EXTRA_BINDER);
        IEngineWorker worker = IEngineWorker.Stub.asInterface(binder);
        boolean accepted = EngineWorkerCoordinator.registerRootWorker(
                arg,
                extras.getString(EXTRA_TOKEN),
                worker
        );
        result.putBoolean(RESULT_ACCEPTED, accepted);
        return result;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
                        String[] selectionArgs, String sortOrder) {
        return null;
    }

    @Override
    public String getType(Uri uri) {
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
    public int update(Uri uri, ContentValues values, String selection,
                      String[] selectionArgs) {
        return 0;
    }
}
