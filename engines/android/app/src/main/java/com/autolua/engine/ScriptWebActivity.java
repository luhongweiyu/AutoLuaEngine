/**
 * 文件用途：在 App 主进程承载脚本 HTML/JavaScript 界面，并桥接网页与引擎事件。
 */
package com.autolua.engine;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.view.Window;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import androidx.webkit.WebViewCompat;
import androidx.webkit.WebViewFeature;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.Locale;

/**
 * 脚本 WebView Activity。
 *
 * 这是受信任脚本环境：HTML 可加载本地文件、任意 URL、访问网络和文件，并可直接通过
 * AutoLua 桥调用引擎 JSON-RPC。这里不对页面来源、跳转、跨域和桥接能力作安全限制。
 */
public final class ScriptWebActivity extends Activity {
    /**
     * 在文档开始阶段建立页面侧对象。
     *
     * 页面自己的 inline script 通常会在 onPageFinished 之前执行。仅在页面加载完成后
     * evaluateJavascript 会使 AutoLua 在首段脚本中不可用，因此优先使用 AndroidX WebKit
     * 的 document-start 注入；旧 WebView 则退回到同名 Java bridge，保证 emit/call/close
     * 仍可立即调用，页面加载完成后再升级为完整 JavaScript 对象。
     */
    private static final String PAGE_BRIDGE_SCRIPT = "(function(){"
            + "var bridge=window.AutoLuaNative||window.AutoLua;"
            + "if(!bridge){return;}"
            + "var previous=window.AutoLua||{};"
            + "window.AutoLua={"
            + "emit:function(type,data){bridge.emit(String(type),JSON.stringify(data===undefined?null:data));},"
            + "close:function(){bridge.close();},"
            + "call:function(method,params,callbackId){bridge.call(String(method),JSON.stringify(params===undefined?{}:params),String(callbackId===undefined?'':callbackId));},"
            + "onMessage:previous.onMessage,"
            + "onCallResult:previous.onCallResult"
            + "};"
            + "})();";
    private static final String PAGE_BRIDGE_TAG = "<script>" + PAGE_BRIDGE_SCRIPT + "</script>";
    private static final int PAGE_CONNECT_TIMEOUT_MS = 8000;
    private static final int PAGE_READ_TIMEOUT_MS = 15000;

    private long sessionId;
    private JSONObject spec;
    private WebView webView;
    private boolean documentStartBridgeAvailable;
    private boolean eventSent;
    private boolean closedByEngine;
    private final BroadcastReceiver commandReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            boolean closeAll = ScriptUiProtocol.ACTION_CLOSE_ALL.equals(action);
            boolean closeCurrent = ScriptUiProtocol.ACTION_CLOSE.equals(action)
                    && intent.getLongExtra(ScriptUiProtocol.EXTRA_SESSION_ID, 0) == sessionId;
            if (closeAll || closeCurrent) {
                closedByEngine = true;
                eventSent = true;
                finish();
                return;
            }

            if (ScriptUiProtocol.ACTION_WEB_MESSAGE.equals(action)
                    && intent.getLongExtra(ScriptUiProtocol.EXTRA_SESSION_ID, 0) == sessionId) {
                postMessageToPage(intent.getStringExtra(ScriptUiProtocol.EXTRA_MESSAGE_JSON));
            }
        }
    };

    @Override
    @SuppressLint("SetJavaScriptEnabled")
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        sessionId = getIntent().getLongExtra(ScriptUiProtocol.EXTRA_SESSION_ID, 0);
        if (sessionId <= 0) {
            finish();
            return;
        }

        try {
            spec = new JSONObject(getIntent().getStringExtra(ScriptUiProtocol.EXTRA_SPEC_JSON));
        } catch (Exception exception) {
            ScriptUiEventDispatcher.dispatch(this, sessionId, "error", makeData(
                    "message",
                    "HTML 页面配置 JSON 无效"
            ));
            finish();
            return;
        }

        setTitle(spec.optString("title", "AutoLuaEngine"));
        registerCommandReceiver();
        webView = new WebView(this);
        configureWebView(webView);
        setContentView(webView);
        configureWindow();
        loadPage();
    }

    /**
     * 根据脚本配置设置 HTML 窗口。
     *
     * width、height、x、y 全部使用屏幕物理像素，与截图和触摸坐标保持一致。没有传入
     * width/height 时保持原来的全屏页面；只要传入任意尺寸，就切换为无背景变暗的定位
     * 窗口。指定尺寸会限制在当前屏幕像素范围内，避免窗口完全超出可操作区域。
     */
    private void configureWindow() {
        boolean sizedWindow = spec.has("width") || spec.has("height");
        Window window = getWindow();
        if (!sizedWindow) {
            window.setLayout(
                    WindowManager.LayoutParams.MATCH_PARENT,
                    WindowManager.LayoutParams.MATCH_PARENT
            );
            return;
        }

        int screenWidth = getResources().getDisplayMetrics().widthPixels;
        int screenHeight = getResources().getDisplayMetrics().heightPixels;
        int width = readWindowSize("width", screenWidth);
        int height = readWindowSize("height", screenHeight);

        window.setBackgroundDrawable(new ColorDrawable(Color.WHITE));
        window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        WindowManager.LayoutParams params = window.getAttributes();
        params.gravity = Gravity.TOP | Gravity.START;
        params.width = width;
        params.height = height;
        params.x = clamp(spec.optInt("x", 0), 0, Math.max(0, screenWidth - width));
        params.y = clamp(spec.optInt("y", 0), 0, Math.max(0, screenHeight - height));
        window.setAttributes(params);
    }

    /**
     * 读取窗口像素尺寸。缺少或非正数时占满对应方向，正数不能超过屏幕尺寸。
     */
    private int readWindowSize(String name, int screenSize) {
        int value = spec.optInt(name, screenSize);
        return value > 0 ? Math.min(value, screenSize) : screenSize;
    }

    private static int clamp(int value, int minimum, int maximum) {
        return Math.max(minimum, Math.min(value, maximum));
    }

    @Override
    protected void onDestroy() {
        unregisterCommandReceiver();
        if (webView != null) {
            webView.stopLoading();
            webView.destroy();
            webView = null;
        }
        if (!eventSent && !closedByEngine && !isChangingConfigurations()) {
            ScriptUiEventDispatcher.dispatch(this, sessionId, "closed", makeData("reason", "activity"));
        }
        super.onDestroy();
    }

    @Override
    public void onBackPressed() {
        if (webView != null && webView.canGoBack()) {
            webView.goBack();
            return;
        }
        closeFromPage();
    }

    private void registerCommandReceiver() {
        IntentFilter filter = new IntentFilter();
        filter.addAction(ScriptUiProtocol.ACTION_CLOSE);
        filter.addAction(ScriptUiProtocol.ACTION_CLOSE_ALL);
        filter.addAction(ScriptUiProtocol.ACTION_WEB_MESSAGE);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(commandReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(commandReceiver, filter);
        }
    }

    private void unregisterCommandReceiver() {
        try {
            unregisterReceiver(commandReceiver);
        } catch (IllegalArgumentException ignored) {
            // 配置解析失败时可能尚未注册，直接结束即可。
        }
    }

    /**
     * WebView 按受信任脚本环境初始化。这里明确保留文件、网络、跨域和 JS bridge 能力。
     */
    @SuppressLint("SetJavaScriptEnabled")
    private void configureWebView(WebView target) {
        WebSettings settings = target.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setDatabaseEnabled(true);
        settings.setAllowFileAccess(true);
        settings.setAllowContentAccess(true);
        settings.setAllowFileAccessFromFileURLs(true);
        settings.setAllowUniversalAccessFromFileURLs(true);
        settings.setJavaScriptCanOpenWindowsAutomatically(true);
        settings.setSupportMultipleWindows(true);
        settings.setMediaPlaybackRequiresUserGesture(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);
        settings.setLoadWithOverviewMode(true);
        settings.setUseWideViewPort(true);

        target.addJavascriptInterface(new ScriptWebJavascriptBridge(), "AutoLuaNative");
        documentStartBridgeAvailable = WebViewFeature.isFeatureSupported(
                WebViewFeature.DOCUMENT_START_SCRIPT
        );
        if (documentStartBridgeAvailable) {
            WebViewCompat.addDocumentStartJavaScript(
                    target,
                    PAGE_BRIDGE_SCRIPT,
                    Collections.singleton("*")
            );
        }
        target.setWebChromeClient(new WebChromeClient());
        target.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageStarted(WebView view, String url, Bitmap favicon) {
                super.onPageStarted(view, url, favicon);
                // 主文档被拦截注入失败时，仍在导航开始时补一次。对不支持 document-start
                // 的旧 WebView，这能覆盖没有可读取 HTML 源码的少数页面类型。
                injectJavascriptBridge();
            }

            @Override
            public WebResourceResponse shouldInterceptRequest(
                    WebView view,
                    WebResourceRequest request
            ) {
                if (documentStartBridgeAvailable
                        || request == null
                        || !request.isForMainFrame()) {
                    return null;
                }
                return interceptMainHtmlDocument(request.getUrl());
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                super.onPageFinished(view, url);
                injectJavascriptBridge();
            }

            @Override
            public void onReceivedError(
                    WebView view,
                    WebResourceRequest request,
                    WebResourceError error
            ) {
                super.onReceivedError(view, request, error);
                if (request != null && request.isForMainFrame()) {
                    ScriptUiEventDispatcher.dispatch(
                            ScriptWebActivity.this,
                            sessionId,
                            "error",
                            makeData("message", error == null ? "网页加载失败" : String.valueOf(error.getDescription()))
                    );
                }
            }
        });
    }

    /**
     * 支持 url、file、html 三种页面来源。未提供 scheme 的 file 按脚本目录相对路径处理。
     */
    private void loadPage() {
        String html = spec.optString("html", "");
        if (!html.isEmpty()) {
            String baseUrl = spec.optString(
                    "baseUrl",
                    Uri.fromFile(ScriptCatalog.getScriptDirectory(this)).toString() + "/"
            );
            webView.loadDataWithBaseURL(
                    baseUrl,
                    injectPageBridge(html),
                    "text/html",
                    "utf-8",
                    null
            );
            return;
        }

        String location = spec.optString("url", "");
        if (location.isEmpty()) {
            location = spec.optString("file", "");
        }
        if (location.isEmpty()) {
            webView.loadData("", "text/html", "utf-8");
            return;
        }

        String packagePath = spec.optString("_alpkgPath", "");
        if (!packagePath.isEmpty() && !hasScheme(location)) {
            location = ScriptPackageResources.buildUri(packagePath, location).toString();
        } else if (!hasScheme(location)) {
            location = Uri.fromFile(new java.io.File(ScriptCatalog.getScriptDirectory(this), location)).toString();
        }
        if (!documentStartBridgeAvailable && tryLoadLocalHtmlWithBridge(location)) {
            return;
        }
        webView.loadUrl(location);
    }

    private static boolean hasScheme(String location) {
        int separator = location.indexOf(':');
        return separator > 0;
    }

    /**
     * 在不支持 document-start 的旧 WebView 中读取本地页面并预置 bridge。
     *
     * file/content 页面可以在加载前拿到完整 HTML，因此直接插入脚本比 onPageStarted 的
     * evaluateJavascript 更早，页面首段 inline script 也能使用 AutoLua。
     */
    private boolean tryLoadLocalHtmlWithBridge(String location) {
        Uri uri = Uri.parse(location);
        String scheme = uri.getScheme();
        if (scheme == null || (!"file".equalsIgnoreCase(scheme) && !"content".equalsIgnoreCase(scheme))) {
            return false;
        }

        try (InputStream inputStream = openLocalPageStream(uri)) {
            if (inputStream == null) {
                return false;
            }
            String html = readText(inputStream, StandardCharsets.UTF_8);
            webView.loadDataWithBaseURL(
                    location,
                    injectPageBridge(html),
                    "text/html",
                    "utf-8",
                    null
            );
            return true;
        } catch (IOException ignored) {
            // 页面仍按 WebView 原始路线加载；onPageStarted/onPageFinished 会继续尝试注入。
            return false;
        }
    }

    private InputStream openLocalPageStream(Uri uri) throws IOException {
        if ("file".equalsIgnoreCase(uri.getScheme())) {
            return new FileInputStream(new File(uri.getPath()));
        }
        return getContentResolver().openInputStream(uri);
    }

    /**
     * 拦截 HTTP/HTTPS 主文档并在字节流进入 WebView 前插入 bridge。
     *
     * 旧系统 WebView 没有 document-start API。这里仅替换 HTML 主文档，CSS、JS、图片和
     * iframe 仍按页面自己的网络路径加载；返回响应不保留 CSP，受信任脚本可使用 bridge。
     */
    private WebResourceResponse interceptMainHtmlDocument(Uri uri) {
        String scheme = uri == null ? "" : uri.getScheme();
        if (!"http".equalsIgnoreCase(scheme) && !"https".equalsIgnoreCase(scheme)) {
            return null;
        }

        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) new URL(uri.toString()).openConnection();
            connection.setConnectTimeout(PAGE_CONNECT_TIMEOUT_MS);
            connection.setReadTimeout(PAGE_READ_TIMEOUT_MS);
            connection.setInstanceFollowRedirects(true);
            connection.setRequestProperty("Accept", "text/html,application/xhtml+xml,*/*;q=0.8");

            String contentType = connection.getContentType();
            if (!isHtmlContentType(contentType, uri)) {
                return null;
            }

            Charset charset = resolvePageCharset(contentType);
            try (InputStream inputStream = connection.getInputStream()) {
                String html = readText(inputStream, charset);
                byte[] output = injectPageBridge(html).getBytes(charset);
                return new WebResourceResponse(
                        "text/html",
                        charset.name(),
                        new ByteArrayInputStream(output)
                );
            }
        } catch (IOException ignored) {
            // 网络错误交还给 WebView 本身处理，原有 onReceivedError 会回传脚本事件。
            return null;
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
    }

    private static boolean isHtmlContentType(String contentType, Uri uri) {
        if (contentType != null && contentType.toLowerCase(Locale.US).contains("html")) {
            return true;
        }
        String path = uri == null ? "" : uri.getPath();
        String lowerPath = path == null ? "" : path.toLowerCase(Locale.US);
        return contentType == null || lowerPath.endsWith(".html") || lowerPath.endsWith(".htm");
    }

    private static Charset resolvePageCharset(String contentType) {
        if (contentType != null) {
            String[] parts = contentType.split(";");
            for (String part : parts) {
                String trimmed = part.trim();
                if (trimmed.regionMatches(true, 0, "charset=", 0, "charset=".length())) {
                    try {
                        return Charset.forName(trimmed.substring("charset=".length()).trim());
                    } catch (Exception ignored) {
                        return StandardCharsets.UTF_8;
                    }
                }
            }
        }
        return StandardCharsets.UTF_8;
    }

    private static String readText(InputStream inputStream, Charset charset) throws IOException {
        try (ByteArrayOutputStream outputStream = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int count;
            while ((count = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, count);
            }
            return outputStream.toString(charset.name());
        }
    }

    /**
     * 将 bridge 放到 doctype 后；没有 doctype 时直接前置，保证比用户的任何 inline script
     * 更早执行，不依赖页面是否按标准顺序书写 head/body。
     */
    private static String injectPageBridge(String html) {
        String source = html == null ? "" : html;
        String lowerSource = source.toLowerCase(Locale.US);
        if (lowerSource.startsWith("<!doctype")) {
            int doctypeEnd = source.indexOf('>');
            if (doctypeEnd >= 0) {
                return source.substring(0, doctypeEnd + 1)
                        + PAGE_BRIDGE_TAG
                        + source.substring(doctypeEnd + 1);
            }
        }
        return PAGE_BRIDGE_TAG + source;
    }

    /**
     * 向页面注入 JS 侧的 AutoLua 对象。
     *
     * emit 将网页事件交给 Lua；call 可直接调用现有引擎 JSON-RPC；onMessage 是 Lua
     * 通过 m.web.postMessage 推送数据时的可选回调。
     */
    private void injectJavascriptBridge() {
        if (webView == null) {
            return;
        }
        webView.evaluateJavascript(PAGE_BRIDGE_SCRIPT, null);
    }

    /**
     * 接收 Lua 通过 m.web.postMessage 发送的数据。
     */
    private void postMessageToPage(String messageJson) {
        if (webView == null) {
            return;
        }
        String payload = messageJson == null || messageJson.trim().isEmpty() ? "null" : messageJson;
        String script = "(function(){var data=" + payload + ";"
                + "if(window.AutoLua&&typeof window.AutoLua.onMessage==='function'){window.AutoLua.onMessage(data);}"
                + "window.dispatchEvent(new CustomEvent('autolua-message',{detail:data}));"
                + "})();";
        webView.evaluateJavascript(script, null);
    }

    private void closeFromPage() {
        if (eventSent) {
            finish();
            return;
        }
        eventSent = true;
        ScriptUiEventDispatcher.dispatch(this, sessionId, "closed", makeData("reason", "page"));
        finish();
    }

    private void reportEngineCallResult(String callbackId, String resultJson, String errorMessage) {
        if (webView == null) {
            return;
        }
        String result = resultJson == null || resultJson.isEmpty() ? "null" : resultJson;
        String error = errorMessage == null ? "null" : JSONObject.quote(errorMessage);
        String script = "(function(){if(window.AutoLua&&typeof window.AutoLua.onCallResult==='function'){"
                + "window.AutoLua.onCallResult(" + JSONObject.quote(callbackId == null ? "" : callbackId)
                + "," + result + "," + error + ");}})();";
        runOnUiThread(() -> {
            if (webView != null) {
                webView.evaluateJavascript(script, null);
            }
        });
    }

    private static JSONObject makeData(String key, Object value) {
        JSONObject data = new JSONObject();
        try {
            data.put(key, value == null ? JSONObject.NULL : value);
        } catch (JSONException ignored) {
            // 基础值写入不会失败。
        }
        return data;
    }

    /**
     * JavaScript 暴露对象。网页可自由调用 emit、call 和 close，不对页面来源或方法名限制。
     */
    public final class ScriptWebJavascriptBridge {
        @JavascriptInterface
        public void emit(String event, String dataJson) {
            ScriptUiEventDispatcher.dispatchJson(
                    ScriptWebActivity.this,
                    sessionId,
                    event == null || event.isEmpty() ? "event" : event,
                    dataJson
            );
        }

        @JavascriptInterface
        public void close() {
            runOnUiThread(ScriptWebActivity.this::closeFromPage);
        }

        /**
         * 允许 HTML 直接调用引擎 JSON-RPC。结果以 AutoLua.onCallResult 回调返回页面，
         * 因为 WebView 的 Java bridge 不能同步等待网络请求。
         */
        @JavascriptInterface
        public void call(String method, String paramsJson, String callbackId) {
            new Thread(() -> {
                try {
                    JSONObject params = paramsJson == null || paramsJson.trim().isEmpty()
                            ? new JSONObject()
                            : new JSONObject(paramsJson);
                    JSONObject result = EngineLocalClient.call(
                            getApplicationContext(),
                            method == null ? "" : method,
                            params
                    );
                    reportEngineCallResult(callbackId, result.toString(), null);
                } catch (Exception exception) {
                    reportEngineCallResult(callbackId, null, exception.getMessage());
                }
            }, "ScriptWebEngineCall").start();
        }
    }
}
