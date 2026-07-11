/**
 * 文件用途：App 主界面，负责脚本列表、状态展示、市场占位和设置入口。
 */
package com.autolua.engine;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.MimeTypeMap;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

import androidx.core.content.FileProvider;

/**
 * Android 引擎主界面。
 *
 * 主进程只负责 UI、悬浮窗和权限入口；脚本执行、HTTP 服务、Root 运行层在
 * EngineService 所在的 :engine 进程内运行。这里通过 Intent 和本地 JSON-RPC
 * 控制引擎，避免 UI 进程直接持有脚本线程。
 */
public final class MainActivity extends Activity {
    private static final int REQUEST_OVERLAY_PERMISSION = 1002;
    private static final int TAB_SCRIPT = 0;
    private static final int TAB_STATUS = 1;
    private static final int TAB_MARKET = 2;
    private static final int TAB_SETTINGS = 3;
    private static final int PAGE_PADDING = 18;
    private static final int SECTION_MARGIN = 18;
    private static final int ITEM_MARGIN = 10;
    private static final int MAX_LOG_LINES = 30;
    private static final int COLOR_BACKGROUND = Color.rgb(245, 247, 250);
    private static final int COLOR_SURFACE = Color.WHITE;
    private static final int COLOR_TEXT = Color.rgb(31, 41, 55);
    private static final int COLOR_MUTED = Color.rgb(107, 114, 128);
    private static final int COLOR_LINE = Color.rgb(226, 232, 240);
    private static final int COLOR_PRIMARY = Color.rgb(37, 99, 235);
    private static final int COLOR_SUCCESS = Color.rgb(22, 163, 74);

    private FrameLayout pageContainer;
    private TextView statusDetailView;
    private TextView settingsPermissionView;
    private TextView[] navItems;
    private Button runButton;
    private CheckBox rootModeCheckBox;
    private CheckBox floatingCheckBox;
    private ScriptCatalog.ScriptItem selectedScript;
    private int currentTab = TAB_SCRIPT;
    private String latestMessage = "就绪";
    private boolean scriptRunning;
    private final BroadcastReceiver engineStatusReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            handleEngineStatus(intent);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        ensureAppFilesDir();
        EngineService.ensureStarted(this);
        selectedScript = ScriptCatalog.getSelectedScript(this);
        ensureFloatingControlIfEnabled();
        configureSystemBars();
        setContentView(createContentView());
        showTab(TAB_SCRIPT);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
    }

    @Override
    protected void onStart() {
        super.onStart();
        IntentFilter filter = new IntentFilter(EngineService.ACTION_STATUS);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(engineStatusReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(engineStatusReceiver, filter);
        }
        refreshVisiblePage();
        refreshRunningStateFromEngine();
    }

    @Override
    protected void onStop() {
        unregisterReceiver(engineStatusReceiver);
        super.onStop();
    }

    private View createContentView() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(COLOR_BACKGROUND);

        pageContainer = new FrameLayout(this);
        LinearLayout.LayoutParams pageParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                0,
                1f
        );
        root.addView(pageContainer, pageParams);

        root.addView(createBottomNavigation(), matchWidthWrapContent());
        return root;
    }

    private View createBottomNavigation() {
        LinearLayout nav = new LinearLayout(this);
        nav.setOrientation(LinearLayout.HORIZONTAL);
        nav.setPadding(0, dp(6), 0, dp(6));
        nav.setBackgroundColor(COLOR_SURFACE);

        navItems = new TextView[] {
                createNavItem("脚本", TAB_SCRIPT),
                createNavItem("状态", TAB_STATUS),
                createNavItem("市场", TAB_MARKET),
                createNavItem("设置", TAB_SETTINGS)
        };

        for (TextView item : navItems) {
            nav.addView(item, new LinearLayout.LayoutParams(
                    0,
                    dp(54),
                    1f
            ));
        }
        return nav;
    }

    private TextView createNavItem(String text, int tabIndex) {
        TextView item = createText(text, 14, COLOR_MUTED, false);
        item.setGravity(Gravity.CENTER);
        item.setClickable(true);
        item.setOnClickListener(view -> showTab(tabIndex));
        return item;
    }

    private void showTab(int tabIndex) {
        currentTab = tabIndex;
        if (pageContainer == null) {
            return;
        }

        pageContainer.removeAllViews();
        pageContainer.addView(createPageForTab(tabIndex), matchParent());
        updateNavigationState();

        if (tabIndex == TAB_STATUS) {
            queryStatusSummary();
        }
        if (tabIndex == TAB_SETTINGS) {
            updateSettingsPermissionView();
        }
    }

    private View createPageForTab(int tabIndex) {
        if (tabIndex == TAB_STATUS) {
            return createStatusPage();
        }
        if (tabIndex == TAB_MARKET) {
            return createMarketPage();
        }
        if (tabIndex == TAB_SETTINGS) {
            return createSettingsPage();
        }
        return createScriptPage();
    }

    private View createScriptPage() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(COLOR_BACKGROUND);

        ScrollView scrollView = createPageScrollView();
        LinearLayout list = createListContent();
        scrollView.addView(list);
        root.addView(scrollView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                0,
                1f
        ));

        ScriptCatalog.ScriptItem[] scripts = ScriptCatalog.listScripts(this);
        if (scripts.length == 0) {
            list.addView(createEmptyText("当前脚本目录没有文件"), topMarginParams(24));
        } else {
            for (ScriptCatalog.ScriptItem item : scripts) {
                list.addView(createScriptRow(item), topMarginParams(ITEM_MARGIN));
            }
        }

        root.addView(createScriptRunBar(), matchWidthWrapContent());
        setRunningControls(scriptRunning);
        return root;
    }

    private View createScriptRunBar() {
        LinearLayout runBar = createHorizontalRow();
        runBar.setPadding(dp(PAGE_PADDING), dp(10), dp(PAGE_PADDING), dp(10));
        runBar.setBackgroundColor(COLOR_SURFACE);
        runButton = createPrimaryButton(R.id.button_run_lua, "运行", this::runSelectedScript);
        runBar.addView(runButton, matchWidthButtonParams());
        return runBar;
    }

    private View createScriptRow(ScriptCatalog.ScriptItem item) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(10), dp(6), dp(10), dp(6));
        row.setBackground(makeRoundDrawable(COLOR_SURFACE, dp(8), COLOR_LINE));
        row.setClickable(true);
        row.setOnClickListener(view -> selectScript(item));

        RadioButton radioButton = new RadioButton(this);
        radioButton.setTag(item.filePath);
        radioButton.setChecked(selectedScript != null && selectedScript.filePath.equals(item.filePath));
        radioButton.setOnClickListener(view -> selectScript(item));
        row.addView(radioButton, new LinearLayout.LayoutParams(dp(42), dp(42)));

        TextView nameView = createText(item.fileName, 15, COLOR_TEXT, true);
        nameView.setSingleLine(true);
        nameView.setEllipsize(TextUtils.TruncateAt.END);
        row.addView(nameView, new LinearLayout.LayoutParams(
                0,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                1.2f
        ));

        String rightText = makeScriptDetailText(item);
        TextView detailView = createTinyText(rightText);
        detailView.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        detailView.setSingleLine(true);
        detailView.setEllipsize(TextUtils.TruncateAt.END);
        row.addView(detailView, new LinearLayout.LayoutParams(
                0,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                1.8f
        ));

        row.addView(createOpenFileButton(item), new LinearLayout.LayoutParams(dp(42), dp(42)));
        return row;
    }

    private View createStatusPage() {
        ScrollView scrollView = createPageScrollView();
        LinearLayout page = createPageContent();
        scrollView.addView(page);

        statusDetailView = createSmallText("正在读取状态...");
        statusDetailView.setTextSize(14);
        page.addView(statusDetailView, matchWidthWrapContent());

        LinearLayout row = createHorizontalRow();
        row.addView(createSecondaryButton(View.generateViewId(), "刷新状态", this::queryStatusSummary),
                weightedButtonParams(1f, false));
        row.addView(createSecondaryButton(View.generateViewId(), "读取日志", this::showRecentLogs),
                weightedButtonParams(1f, true));
        page.addView(row, topMarginParams(16));
        return scrollView;
    }

    private View createMarketPage() {
        ScrollView scrollView = createPageScrollView();
        LinearLayout page = createPageContent();
        scrollView.addView(page);

        page.addView(createEmptyText("暂未开放"), topMarginParams(24));
        return scrollView;
    }

    private View createSettingsPage() {
        ScrollView scrollView = createPageScrollView();
        LinearLayout page = createPageContent();
        scrollView.addView(page);

        rootModeCheckBox = new CheckBox(this);
        rootModeCheckBox.setText("Root 模式（默认）");
        rootModeCheckBox.setTextSize(16);
        rootModeCheckBox.setTextColor(COLOR_TEXT);
        rootModeCheckBox.setChecked(EngineSettings.isRootModeEnabled(this));
        rootModeCheckBox.setOnCheckedChangeListener(this::handleRootModeChanged);
        page.addView(rootModeCheckBox, topMarginParams(12));

        TextView rootHint = createSmallText("勾选后引擎启动和切换模式时准备 Root 运行层；当前已实现截图核心。");
        page.addView(rootHint, topMarginParams(4));

        floatingCheckBox = new CheckBox(this);
        floatingCheckBox.setText("显示悬浮按钮");
        floatingCheckBox.setTextSize(16);
        floatingCheckBox.setTextColor(COLOR_TEXT);
        floatingCheckBox.setChecked(isFloatingControlVisible());
        floatingCheckBox.setOnCheckedChangeListener(this::handleFloatingChanged);
        page.addView(floatingCheckBox, topMarginParams(SECTION_MARGIN));

        TextView floatingHint = createSmallText("悬浮按钮在主进程显示，只负责发送运行、暂停、停止等控制命令。");
        page.addView(floatingHint, topMarginParams(4));

        settingsPermissionView = createSmallText("");
        settingsPermissionView.setTextSize(14);
        page.addView(settingsPermissionView, topMarginParams(SECTION_MARGIN));
        updateSettingsPermissionView();

        LinearLayout permissionRow = createHorizontalRow();
        permissionRow.addView(createSecondaryButton(
                View.generateViewId(),
                "无障碍设置",
                this::openAccessibilitySettings
        ), weightedButtonParams(1f, false));
        page.addView(permissionRow, topMarginParams(12));
        return scrollView;
    }

    private void updateNavigationState() {
        if (navItems == null) {
            return;
        }

        for (int i = 0; i < navItems.length; i++) {
            TextView item = navItems[i];
            boolean selected = i == currentTab;
            item.setTextColor(selected ? COLOR_PRIMARY : COLOR_MUTED);
            item.setTypeface(Typeface.DEFAULT, selected ? Typeface.BOLD : Typeface.NORMAL);
        }
    }

    private void refreshVisiblePage() {
        selectedScript = ScriptCatalog.getSelectedScript(this);
        if (currentTab == TAB_STATUS) {
            queryStatusSummary();
        } else if (currentTab == TAB_SETTINGS) {
            updateSettingsPermissionView();
        }
    }

    private void selectScript(ScriptCatalog.ScriptItem item) {
        selectedScript = item;
        ScriptCatalog.setSelectedScript(this, item);
        setMessage("已选择脚本：" + item.fileName);
        updateScriptSelectionState();
    }

    private void runSelectedScript() {
        if (scriptRunning) {
            stopRunningScriptFromRunButton();
            return;
        }

        selectedScript = ScriptCatalog.getSelectedScript(this);
        if (selectedScript == null) {
            setMessage("脚本目录为空，无法运行");
            return;
        }
        if (!selectedScript.runnable) {
            setMessage("当前只支持运行 Lua 文件：" + selectedScript.fileName);
            return;
        }

        EngineService.runScriptFile(this, selectedScript.filePath);
        setRunningControls(true);
        setMessage("准备运行脚本：" + selectedScript.fileName);
    }

    private void stopRunningScriptFromRunButton() {
        EngineService.stopScript(this);
        setRunningControls(true);
        setMessage("已请求停止脚本");
    }

    private void handleRootModeChanged(CompoundButton button, boolean enabled) {
        EngineSettings.setRootModeEnabled(this, enabled);
        EngineService.setRootModeEnabled(this, enabled);
        setMessage(enabled ? "运行模式已切换为 Root 模式" : "运行模式已切换为无障碍优先");
        updateSettingsPermissionView();
    }

    private void handleFloatingChanged(CompoundButton button, boolean checked) {
        if (checked) {
            startFloatingControl();
        } else {
            EngineSettings.setFloatingBubbleHidden(this, true);
            EngineSettings.setFloatingPanelExpanded(this, false);
            stopService(new Intent(this, FloatingControlService.class));
            setMessage("悬浮按钮已隐藏");
        }
        updateSettingsPermissionView();
    }

    private void setRunningControls(boolean running) {
        scriptRunning = running;
        if (runButton != null) {
            runButton.setEnabled(true);
            runButton.setAlpha(1f);
            runButton.setText(running ? "停止" : "运行");
            int color = running ? COLOR_SUCCESS : COLOR_PRIMARY;
            runButton.setBackground(makeRoundDrawable(color, dp(7), color));
        }
    }

    private void handleEngineStatus(Intent intent) {
        String state = intent.getStringExtra(EngineService.EXTRA_STATE);
        String message = intent.getStringExtra(EngineService.EXTRA_MESSAGE);
        setRunningControls(isActiveEngineState(state));

        if (message != null && !message.isEmpty()) {
            setMessage(message);
        }

        if (currentTab == TAB_STATUS) {
            queryStatusSummary();
        }
    }

    private void refreshRunningStateFromEngine() {
        new Thread(() -> {
            try {
                JSONObject taskStatus = EngineLocalClient.call(this, "script.status", makeTaskIdParams(0));
                boolean running = isActiveEngineState(taskStatus.optString("status", ""));
                runOnUiThread(() -> setRunningControls(running));
            } catch (Exception ignored) {
                // HTTP 不通时说明引擎进程没有可确认的运行任务，按钮直接恢复“运行”。
                runOnUiThread(() -> setRunningControls(false));
            }
        }, "MainActivityRunningStateQuery").start();
    }

    private void updateScriptSelectionState() {
        if (pageContainer == null) {
            return;
        }
        updateScriptSelectionState(pageContainer);
    }

    private void updateScriptSelectionState(View view) {
        if (view instanceof RadioButton) {
            Object tag = view.getTag();
            boolean checked = selectedScript != null
                    && tag instanceof String
                    && selectedScript.filePath.equals(tag);
            ((RadioButton) view).setChecked(checked);
            return;
        }

        if (!(view instanceof ViewGroup)) {
            return;
        }

        ViewGroup group = (ViewGroup) view;
        for (int i = 0; i < group.getChildCount(); i++) {
            updateScriptSelectionState(group.getChildAt(i));
        }
    }

    private boolean isActiveEngineState(String state) {
        return EngineService.STATE_RUNNING.equals(state)
                || EngineService.STATE_PAUSING.equals(state)
                || EngineService.STATE_PAUSED.equals(state)
                || EngineService.STATE_STOPPING.equals(state);
    }

    private void startFloatingControl() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && !Settings.canDrawOverlays(this)) {
            Intent intent = new Intent(
                    Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                    Uri.parse("package:" + getPackageName())
            );
            startActivityForResult(intent, REQUEST_OVERLAY_PERMISSION);
            setMessage("请开启悬浮窗权限后再启动悬浮控制");
            return;
        }

        EngineSettings.setFloatingBubbleHidden(this, false);
        EngineSettings.setFloatingPanelExpanded(this, false);
        Intent serviceIntent = new Intent(this, FloatingControlService.class);
        startService(serviceIntent);
        setMessage("悬浮控制已启动");
    }

    private void ensureFloatingControlIfEnabled() {
        if (EngineSettings.isFloatingBubbleHidden(this)) {
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && !Settings.canDrawOverlays(this)) {
            return;
        }

        startService(new Intent(this, FloatingControlService.class));
    }

    private void openAccessibilitySettings() {
        Intent intent = new Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS);
        startActivity(intent);
        setMessage("已打开无障碍设置");
    }

    private void openScriptFile(ScriptCatalog.ScriptItem item) {
        File file = new File(item.filePath);
        if (!file.exists()) {
            setMessage("文件不存在：" + item.fileName);
            return;
        }

        Uri uri = FileProvider.getUriForFile(
                this,
                getPackageName() + ".fileprovider",
                file
        );
        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setDataAndType(uri, resolveMimeType(item.fileName));
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);

        try {
            startActivity(Intent.createChooser(intent, "打开文件"));
            setMessage("已请求打开文件：" + item.fileName);
        } catch (ActivityNotFoundException exception) {
            setMessage("没有可打开该文件的应用：" + item.fileName);
        }
    }

    private void queryStatusSummary() {
        if (statusDetailView == null) {
            return;
        }

        statusDetailView.setText("正在读取状态...");
        runEngineStatusQuery("正在读取状态...", "读取状态失败：", () -> {
            JSONObject deviceInfo = EngineLocalClient.call(this, "device.info", new JSONObject());
            JSONObject taskStatus = EngineLocalClient.call(this, "script.status", makeTaskIdParams(0));
            boolean overlayEnabled = Build.VERSION.SDK_INT < Build.VERSION_CODES.M
                    || Settings.canDrawOverlays(this);
            ScriptCatalog.ScriptItem currentScript = ScriptCatalog.getSelectedScript(this);

            return "设备：Android API " + deviceInfo.optInt("apiLevel", Build.VERSION.SDK_INT)
                    + "\n包名：" + deviceInfo.optString("packageName", getPackageName())
                    + "\n调试端口："
                    + deviceInfo.optString("httpHost", "127.0.0.1")
                    + ":"
                    + deviceInfo.optInt("httpPort", EngineSettings.getHttpPort(this))
                    + "\n引擎版本：" + deviceInfo.optString("engineVersion", "unknown")
                    + "\nLua 版本：" + deviceInfo.optString("luaVersion", "unknown")
                    + "\n任务状态：" + taskStatus.optString("status", "unknown")
                    + "\nRoot 模式：" + formatEnabled(deviceInfo.optBoolean("rootModeEnabled", true))
                    + "\nRoot 权限：" + formatEnabled(deviceInfo.optBoolean("rootAvailable", false))
                    + "\n无障碍服务：" + formatEnabled(AutomationAccessibilityService.isEnabled())
                    + "\n悬浮窗权限：" + formatEnabled(overlayEnabled)
                    + "\n就绪状态：" + resolveReadyText(deviceInfo, overlayEnabled)
                    + "\n当前脚本：" + (currentScript == null ? "未选择" : currentScript.fileName);
        }, text -> {
            if (statusDetailView != null) {
                statusDetailView.setText(text);
            }
        });
    }

    /**
     * 通过本地 JSON-RPC 读取最近日志。
     *
     * 悬浮窗的“日志”入口会切到状态页并调用这里，便于手机端快速看脚本输出。
     */
    private void showRecentLogs() {
        showTab(TAB_STATUS);
        runEngineStatusQuery("正在读取日志...", "读取日志失败：", () -> {
            JSONObject result = EngineLocalClient.call(this, "log.drain", makeAfterIdParams(0));
            JSONArray entries = result.optJSONArray("entries");
            if (entries == null || entries.length() == 0) {
                return "暂无脚本日志";
            }

            int startIndex = Math.max(0, entries.length() - MAX_LOG_LINES);
            StringBuilder builder = new StringBuilder("最近日志：");
            for (int i = startIndex; i < entries.length(); i++) {
                JSONObject entry = entries.getJSONObject(i);
                builder.append('\n')
                        .append(entry.optInt("id"))
                        .append(" [")
                        .append(entry.optString("level", "info"))
                        .append("] ")
                        .append(entry.optString("message", ""));
            }
            return builder.toString();
        }, text -> {
            if (statusDetailView != null) {
                statusDetailView.setText(text);
            }
        });
    }

    private void updateSettingsPermissionView() {
        if (settingsPermissionView == null) {
            return;
        }

        boolean overlayEnabled = Build.VERSION.SDK_INT < Build.VERSION_CODES.M
                || Settings.canDrawOverlays(this);
        settingsPermissionView.setText("权限状态"
                + "\nRoot 模式：" + formatEnabled(EngineSettings.isRootModeEnabled(this))
                + "\n无障碍服务：" + formatEnabled(AutomationAccessibilityService.isEnabled())
                + "\n悬浮窗权限：" + formatEnabled(overlayEnabled)
                + "\n调试端口：127.0.0.1:" + EngineSettings.getHttpPort(this));
    }

    private boolean isFloatingControlVisible() {
        boolean overlayEnabled = Build.VERSION.SDK_INT < Build.VERSION_CODES.M
                || Settings.canDrawOverlays(this);
        return overlayEnabled && !EngineSettings.isFloatingBubbleHidden(this);
    }

    private String resolveReadyText(JSONObject deviceInfo, boolean overlayEnabled) {
        boolean rootModeEnabled = deviceInfo.optBoolean("rootModeEnabled", true);
        boolean rootAvailable = deviceInfo.optBoolean("rootAvailable", false);
        if (rootModeEnabled && !rootAvailable) {
            return "Root 模式未就绪";
        }
        if (!overlayEnabled) {
            return "悬浮窗未授权";
        }
        return "就绪";
    }

    private String formatEnabled(boolean enabled) {
        return enabled ? "已开启" : "未开启";
    }

    private JSONObject makeAfterIdParams(int afterId) throws JSONException {
        JSONObject params = new JSONObject();
        params.put("afterId", afterId);
        return params;
    }

    private JSONObject makeTaskIdParams(int taskId) throws JSONException {
        JSONObject params = new JSONObject();
        params.put("taskId", taskId);
        return params;
    }

    private void runEngineStatusQuery(
            String loadingText,
            String errorPrefix,
            EngineStatusTextLoader loader,
            StatusTextConsumer consumer) {
        setMessage(loadingText);
        new Thread(() -> {
            String text;
            try {
                text = loader.load();
            } catch (Exception exception) {
                text = errorPrefix + exception.getMessage();
            }

            String finalText = text;
            runOnUiThread(() -> {
                consumer.accept(finalText);
                setMessage("就绪");
            });
        }, "MainActivityEngineStatusQuery").start();
    }

    private interface EngineStatusTextLoader {
        String load() throws Exception;
    }

    private interface StatusTextConsumer {
        void accept(String text);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_OVERLAY_PERMISSION) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M
                    || Settings.canDrawOverlays(this)) {
                startFloatingControl();
            } else {
                setMessage("悬浮窗权限未开启");
                if (floatingCheckBox != null) {
                    floatingCheckBox.setOnCheckedChangeListener(null);
                    floatingCheckBox.setChecked(false);
                    floatingCheckBox.setOnCheckedChangeListener(this::handleFloatingChanged);
                }
            }
            updateSettingsPermissionView();
        }
    }

    private void ensureAppFilesDir() {
        getFilesDir();
        ScriptCatalog.ensureScriptDirectory(this);
    }

    private void setMessage(String message) {
        latestMessage = message == null || message.isEmpty() ? "就绪" : message;
    }

    private void configureSystemBars() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            getWindow().setStatusBarColor(COLOR_BACKGROUND);
            getWindow().setNavigationBarColor(COLOR_SURFACE);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            int flags = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                flags |= View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
            }
            getWindow().getDecorView().setSystemUiVisibility(flags);
        }
    }

    private ScrollView createPageScrollView() {
        ScrollView scrollView = new ScrollView(this);
        scrollView.setFillViewport(false);
        scrollView.setBackgroundColor(COLOR_BACKGROUND);
        return scrollView;
    }

    private LinearLayout createPageContent() {
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(PAGE_PADDING), dp(PAGE_PADDING), dp(PAGE_PADDING), dp(PAGE_PADDING));
        page.setLayoutParams(matchWidthWrapContent());
        return page;
    }

    private LinearLayout createListContent() {
        LinearLayout list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setPadding(dp(PAGE_PADDING), 0, dp(PAGE_PADDING), dp(PAGE_PADDING));
        list.setLayoutParams(matchWidthWrapContent());
        return list;
    }

    private LinearLayout createHorizontalRow() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        return row;
    }

    private TextView createSmallText(String text) {
        TextView textView = createText(text, 13, COLOR_MUTED, false);
        textView.setLineSpacing(dp(2), 1.0f);
        return textView;
    }

    private TextView createTinyText(String text) {
        return createText(text, 12, COLOR_MUTED, false);
    }

    private TextView createEmptyText(String text) {
        TextView textView = createText(text, 15, COLOR_MUTED, false);
        textView.setGravity(Gravity.CENTER);
        return textView;
    }

    private TextView createText(String text, int sp, int color, boolean bold) {
        TextView textView = new TextView(this);
        textView.setText(text);
        textView.setTextSize(sp);
        textView.setTextColor(color);
        textView.setIncludeFontPadding(true);
        textView.setTypeface(Typeface.DEFAULT, bold ? Typeface.BOLD : Typeface.NORMAL);
        return textView;
    }

    private Button createPrimaryButton(int id, String text, Runnable action) {
        Button button = createButton(id, text, action);
        button.setTextColor(Color.WHITE);
        button.setBackground(makeRoundDrawable(COLOR_PRIMARY, dp(7), COLOR_PRIMARY));
        return button;
    }

    private Button createSecondaryButton(int id, String text, Runnable action) {
        Button button = createButton(id, text, action);
        button.setTextColor(COLOR_TEXT);
        button.setBackground(makeRoundDrawable(COLOR_SURFACE, dp(7), COLOR_LINE));
        return button;
    }

    private Button createButton(int id, String text, Runnable action) {
        Button button = new Button(this);
        button.setId(id);
        button.setText(text);
        button.setTextSize(14);
        button.setAllCaps(false);
        button.setMinHeight(dp(44));
        button.setPadding(dp(8), 0, dp(8), 0);
        button.setOnClickListener(view -> action.run());
        return button;
    }

    private ImageButton createOpenFileButton(ScriptCatalog.ScriptItem item) {
        ImageButton button = new ImageButton(this);
        button.setImageResource(R.drawable.ic_script_file);
        button.setScaleType(ImageButton.ScaleType.CENTER);
        button.setClickable(true);
        button.setContentDescription("打开文件：" + item.fileName);
        button.setBackground(makeRoundDrawable(Color.TRANSPARENT, dp(7), COLOR_LINE));
        button.setOnClickListener(view -> openScriptFile(item));
        return button;
    }

    private View createDivider() {
        View divider = new View(this);
        divider.setBackgroundColor(COLOR_LINE);
        divider.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(1)
        ));
        return divider;
    }

    private LinearLayout.LayoutParams matchWidthWrapContent() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
        );
    }

    private FrameLayout.LayoutParams matchParent() {
        return new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
        );
    }

    private LinearLayout.LayoutParams topMarginParams(int marginDp) {
        LinearLayout.LayoutParams params = matchWidthWrapContent();
        params.topMargin = dp(marginDp);
        return params;
    }

    private LinearLayout.LayoutParams weightedButtonParams(float weight, boolean withLeftMargin) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                0,
                dp(46),
                weight
        );
        if (withLeftMargin) {
            params.leftMargin = dp(8);
        }
        return params;
    }

    private LinearLayout.LayoutParams matchWidthButtonParams() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(46)
        );
    }

    private GradientDrawable makeRoundDrawable(int color, int radiusPx, int strokeColor) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(radiusPx);
        drawable.setStroke(dp(1), strokeColor);
        return drawable;
    }

    private String formatFileSize(long sizeBytes) {
        if (sizeBytes < 1024) {
            return sizeBytes + " B";
        }
        if (sizeBytes < 1024 * 1024) {
            return String.format(Locale.US, "%.1f KB", sizeBytes / 1024.0);
        }
        return String.format(Locale.US, "%.1f MB", sizeBytes / 1024.0 / 1024.0);
    }

    private String makeScriptDetailText(ScriptCatalog.ScriptItem item) {
        StringBuilder builder = new StringBuilder();
        if (item.description != null && !item.description.isEmpty()) {
            builder.append(item.description).append("  ");
        }
        builder.append(formatFileSize(item.sizeBytes))
                .append("  ")
                .append(formatTime(item.modifiedAt));
        return builder.toString();
    }

    private String formatTime(long timeMillis) {
        if (timeMillis <= 0) {
            return "未知时间";
        }
        return new SimpleDateFormat("MM-dd HH:mm", Locale.CHINA).format(new Date(timeMillis));
    }

    private String resolveMimeType(String fileName) {
        int dotIndex = fileName.lastIndexOf('.');
        if (dotIndex >= 0 && dotIndex + 1 < fileName.length()) {
            String extension = fileName.substring(dotIndex + 1).toLowerCase(Locale.US);
            String mimeType = MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension);
            if (mimeType != null && !mimeType.isEmpty()) {
                return mimeType;
            }
        }
        return "text/plain";
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }
}
