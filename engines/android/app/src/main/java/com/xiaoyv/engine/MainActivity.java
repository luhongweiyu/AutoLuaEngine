/**
 * 文件用途：App 主界面，负责脚本列表、状态展示、市场占位和设置入口。
 */
package com.xiaoyv.engine;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.text.InputType;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.MimeTypeMap;
import android.widget.Button;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.ScrollView;
import android.widget.Switch;
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
    private static final int REQUEST_SCRIPT_STORAGE_PERMISSION = 1003;
    private static final int TAB_SCRIPT = 0;
    private static final int TAB_STATUS = 1;
    private static final int TAB_MARKET = 2;
    private static final int TAB_SETTINGS = 3;
    private static final int PAGE_PADDING = 16;
    private static final int MAX_LOG_LINES = 30;
    private static final int COLOR_BACKGROUND = Color.rgb(243, 245, 247);
    private static final int COLOR_SURFACE = Color.WHITE;
    private static final int COLOR_TEXT = Color.rgb(29, 39, 51);
    private static final int COLOR_MUTED = Color.rgb(103, 114, 128);
    private static final int COLOR_LINE = Color.rgb(220, 226, 232);
    private static final int COLOR_PRIMARY = Color.rgb(23, 105, 224);
    private static final int COLOR_PRIMARY_SOFT = Color.rgb(234, 242, 255);
    private static final int COLOR_SUCCESS = Color.rgb(22, 138, 85);

    private FrameLayout pageContainer;
    private TextView statusDetailView;
    private TextView settingsPermissionView;
    private TextView[] navItems;
    private Button runButton;
    private Switch rootModeCheckBox;
    private Switch floatingCheckBox;
    private Switch volumeKeyControlCheckBox;
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
        RootDaemonService.ensureForCurrentMode(this);
        EngineService.ensureStarted(this);
        selectedScript = null;
        configureSystemBars();
        setContentView(createContentView());
        showTab(TAB_SCRIPT);
        ensureScriptStorageAccess();
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

    /**
     * 进入 App 时确保已启用的悬浮控制仍然存在。脚本 HTML 关闭后也会回到这里，不能因为
     * MainActivity 重新获得前台状态而隐藏悬浮按钮。
     */
    @Override
    protected void onResume() {
        super.onResume();
        ensureFloatingControlIfEnabled();
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
        nav.setPadding(0, dp(4), 0, dp(3));
        nav.setBackgroundColor(COLOR_SURFACE);

        navItems = new TextView[] {
                createNavItem("脚本", R.drawable.ic_script_file, TAB_SCRIPT),
                createNavItem("状态", R.drawable.ic_nav_status, TAB_STATUS),
                createNavItem("市场", R.drawable.ic_nav_market, TAB_MARKET),
                createNavItem("设置", R.drawable.ic_nav_settings, TAB_SETTINGS)
        };

        for (TextView item : navItems) {
            nav.addView(item, new LinearLayout.LayoutParams(
                    0,
                    dp(58),
                    1f
            ));
        }

        LinearLayout container = new LinearLayout(this);
        container.setOrientation(LinearLayout.VERTICAL);
        container.setBackgroundColor(COLOR_SURFACE);
        container.addView(createDivider());
        container.addView(nav, matchWidthWrapContent());
        return container;
    }

    /**
     * 创建带图标的底部导航项。图标资源编号保存在 tag 中，切页时统一更新颜色。
     */
    private TextView createNavItem(String text, int iconResource, int tabIndex) {
        TextView item = createText(text, 11, COLOR_MUTED, false);
        item.setGravity(Gravity.CENTER);
        item.setClickable(true);
        item.setCompoundDrawablePadding(dp(2));
        item.setTag(iconResource);
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

        if (!ScriptCatalog.isScriptStorageAccessible(this)) {
            list.addView(createEmptyText("脚本目录未授权"), topMarginParams(36));
            list.addView(createSecondaryButton(
                    View.generateViewId(),
                    "授权脚本目录",
                    this::ensureScriptStorageAccess
            ), topMarginParams(12));
        } else {
            ScriptCatalog.ScriptItem[] scripts = ScriptCatalog.listScripts(this);
            if (scripts.length == 0) {
                list.addView(createEmptyText("当前脚本目录没有文件"), topMarginParams(36));
            } else {
                for (int index = 0; index < scripts.length; index++) {
                    list.addView(createScriptRow(scripts[index]), matchWidthWrapContent());
                    if (index + 1 < scripts.length) {
                        list.addView(createScriptDivider());
                    }
                }
            }
        }

        root.addView(createScriptRunBar(), matchWidthWrapContent());
        setRunningControls(scriptRunning);
        return root;
    }

    private View createScriptRunBar() {
        LinearLayout runBar = createHorizontalRow();
        runBar.setPadding(dp(PAGE_PADDING), dp(9), dp(PAGE_PADDING), dp(9));
        runBar.setBackgroundColor(COLOR_SURFACE);
        runButton = createPrimaryButton(R.id.button_run_lua, "运行", this::runSelectedScript);
        runBar.addView(runButton, matchWidthButtonParams());

        LinearLayout container = new LinearLayout(this);
        container.setOrientation(LinearLayout.VERTICAL);
        container.setBackgroundColor(COLOR_SURFACE);
        container.addView(createDivider());
        container.addView(runBar, matchWidthWrapContent());
        return container;
    }

    private View createScriptRow(ScriptCatalog.ScriptItem item) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(14), dp(4), dp(10), dp(4));
        row.setMinimumHeight(dp(62));
        row.setTag(item.filePath);
        updateScriptRowBackground(row, item.filePath);
        row.setClickable(true);
        row.setOnClickListener(view -> selectScript(item));

        RadioButton radioButton = new RadioButton(this);
        radioButton.setTag(item.filePath);
        radioButton.setButtonTintList(new ColorStateList(
                new int[][] {
                        new int[] { android.R.attr.state_checked },
                        new int[] {}
                },
                new int[] { COLOR_PRIMARY, Color.rgb(127, 137, 148) }
        ));
        radioButton.setChecked(selectedScript != null && selectedScript.filePath.equals(item.filePath));
        radioButton.setOnClickListener(view -> selectScript(item));
        row.addView(radioButton, new LinearLayout.LayoutParams(dp(40), dp(48)));

        TextView nameView = createText(item.fileName, 14, COLOR_TEXT, true);
        nameView.setSingleLine(true);
        nameView.setEllipsize(TextUtils.TruncateAt.END);
        row.addView(nameView, new LinearLayout.LayoutParams(
                0,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                1.15f
        ));

        String rightText = makeScriptDetailText(item);
        TextView detailView = createTinyText(rightText);
        detailView.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        detailView.setSingleLine(true);
        detailView.setEllipsize(TextUtils.TruncateAt.END);
        row.addView(detailView, new LinearLayout.LayoutParams(
                0,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                1.55f
        ));

        row.addView(createOpenFileButton(item), new LinearLayout.LayoutParams(dp(44), dp(44)));
        return row;
    }

    private View createStatusPage() {
        ScrollView scrollView = createPageScrollView();
        LinearLayout page = createPageContent();
        scrollView.addView(page);

        statusDetailView = createSmallText("正在读取状态...");
        statusDetailView.setTextSize(14);
        statusDetailView.setTextColor(COLOR_TEXT);
        statusDetailView.setPadding(dp(14), dp(12), dp(14), dp(12));
        statusDetailView.setBackground(makeRoundDrawable(COLOR_SURFACE, dp(6), COLOR_LINE));
        page.addView(statusDetailView, topMarginParams(8));

        LinearLayout row = createHorizontalRow();
        row.addView(createSecondaryButton(View.generateViewId(), "刷新状态", this::queryStatusSummary),
                weightedButtonParams(1f, false));
        row.addView(createSecondaryButton(View.generateViewId(), "读取日志", this::showRecentLogs),
                weightedButtonParams(1f, true));
        page.addView(row, topMarginParams(16));
        return scrollView;
    }

    private View createMarketPage() {
        FrameLayout page = new FrameLayout(this);
        page.setBackgroundColor(COLOR_BACKGROUND);
        TextView emptyView = createEmptyText("市场暂未开放");
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.CENTER
        );
        params.leftMargin = dp(PAGE_PADDING);
        params.rightMargin = dp(PAGE_PADDING);
        page.addView(emptyView, params);
        return page;
    }

    private View createSettingsPage() {
        ScrollView scrollView = createPageScrollView();
        LinearLayout page = createPageContent();
        scrollView.addView(page);

        page.addView(createSectionLabel("运行设置"), matchWidthWrapContent());
        LinearLayout behaviorGroup = createSettingsGroup();

        rootModeCheckBox = createSettingSwitch();
        rootModeCheckBox.setChecked(EngineSettings.isRootModeEnabled(this));
        rootModeCheckBox.setOnCheckedChangeListener(this::handleRootModeChanged);
        behaviorGroup.addView(createSettingRow(
                "Root 模式",
                "启动时准备常驻 Root 运行层",
                rootModeCheckBox
        ));
        behaviorGroup.addView(createSettingsDivider());

        floatingCheckBox = createSettingSwitch();
        floatingCheckBox.setChecked(isFloatingControlVisible());
        floatingCheckBox.setOnCheckedChangeListener(this::handleFloatingChanged);
        behaviorGroup.addView(createSettingRow(
                "悬浮控制",
                "在其他应用上方显示脚本控制按钮",
                floatingCheckBox
        ));
        behaviorGroup.addView(createSettingsDivider());

        volumeKeyControlCheckBox = createSettingSwitch();
        volumeKeyControlCheckBox.setChecked(EngineSettings.isVolumeKeyControlEnabled(this));
        volumeKeyControlCheckBox.setOnCheckedChangeListener(this::handleVolumeKeyControlChanged);
        behaviorGroup.addView(createSettingRow(
                "音量键控制",
                "音量加运行脚本，音量减停止脚本",
                volumeKeyControlCheckBox
        ));
        page.addView(behaviorGroup, topMarginParams(8));

        page.addView(createSectionLabel("脚本与权限"), topMarginParams(22));
        LinearLayout storageGroup = createSettingsGroup();
        storageGroup.addView(createScriptDirectoryRow(), matchWidthWrapContent());
        storageGroup.addView(createSettingsDivider());

        LinearLayout scriptStorageRow = createHorizontalRow();
        scriptStorageRow.setPadding(dp(12), dp(10), dp(12), dp(10));
        scriptStorageRow.addView(createSecondaryButton(
                View.generateViewId(),
                "脚本存储授权",
                this::ensureScriptStorageAccess
        ), weightedButtonParams(1f, false));
        scriptStorageRow.addView(createSecondaryButton(
                View.generateViewId(),
                "无障碍设置",
                this::openAccessibilitySettings
        ), weightedButtonParams(1f, true));
        storageGroup.addView(scriptStorageRow, matchWidthWrapContent());
        page.addView(storageGroup, topMarginParams(8));

        page.addView(createSectionLabel("当前状态"), topMarginParams(22));
        settingsPermissionView = createSmallText("");
        settingsPermissionView.setTextSize(13);
        settingsPermissionView.setTextColor(COLOR_TEXT);
        settingsPermissionView.setPadding(dp(14), dp(12), dp(14), dp(12));
        settingsPermissionView.setBackground(makeRoundDrawable(COLOR_SURFACE, dp(6), COLOR_LINE));
        page.addView(settingsPermissionView, topMarginParams(8));
        updateSettingsPermissionView();
        return scrollView;
    }

    private void updateNavigationState() {
        if (navItems == null) {
            return;
        }

        for (int i = 0; i < navItems.length; i++) {
            TextView item = navItems[i];
            boolean selected = i == currentTab;
            int color = selected ? COLOR_PRIMARY : COLOR_MUTED;
            item.setTextColor(color);
            item.setTypeface(Typeface.DEFAULT, selected ? Typeface.BOLD : Typeface.NORMAL);
            Object iconTag = item.getTag();
            if (iconTag instanceof Integer) {
                Drawable icon = getDrawable((Integer) iconTag).mutate();
                icon.setTint(color);
                icon.setBounds(0, 0, dp(22), dp(22));
                item.setCompoundDrawables(null, icon, null, null);
            }
        }
    }

    private void refreshVisiblePage() {
        selectedScript = ScriptCatalog.getSelectedScript(this);
        if (currentTab == TAB_SCRIPT) {
            // 从外部编辑器返回后重建列表，立即读取磁盘上的真实文件状态。
            showTab(TAB_SCRIPT);
        } else if (currentTab == TAB_STATUS) {
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
            setMessage("当前文件不可运行：" + selectedScript.fileName);
            return;
        }

        EngineService.runScriptFile(this, selectedScript.filePath);
        setRunningControls(true);
        setMessage("准备运行脚本：" + selectedScript.fileName);
    }

    private void stopRunningScriptFromRunButton() {
        EngineService.stopScript(this);
        setMessage("已请求停止脚本");
    }

    private void handleRootModeChanged(CompoundButton button, boolean enabled) {
        EngineSettings.setRootModeEnabled(this, enabled);
        RootDaemonService.setRootModeEnabled(this, enabled);
        setMessage(enabled ? "运行模式已切换为 Root 模式" : "运行模式已切换为无障碍优先");
        updateSettingsPermissionView();
    }

    private void handleVolumeKeyControlChanged(CompoundButton button, boolean enabled) {
        EngineSettings.setVolumeKeyControlEnabled(this, enabled);
        RootDaemonService.syncVolumeKeyControl(this);
        setMessage(enabled ? "音量键控制已开启" : "音量键控制已关闭");
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

        Object tag = view.getTag();
        if (view instanceof LinearLayout && tag instanceof String) {
            updateScriptRowBackground(view, (String) tag);
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
        serviceIntent.setAction(FloatingControlService.ACTION_SHOW);
        startService(serviceIntent);
        setMessage("悬浮控制已开启");
    }

    private void ensureFloatingControlIfEnabled() {
        if (EngineSettings.isFloatingBubbleHidden(this)) {
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && !Settings.canDrawOverlays(this)) {
            return;
        }

        Intent intent = new Intent(this, FloatingControlService.class);
        intent.setAction(FloatingControlService.ACTION_SHOW);
        startService(intent);
    }

    private void openAccessibilitySettings() {
        Intent intent = new Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS);
        startActivity(intent);
        setMessage("已打开无障碍设置");
    }

    private void openScriptFile(ScriptCatalog.ScriptItem item) {
        File file = item == null ? null : new File(item.filePath);
        if (file == null || !file.isFile()) {
            setMessage("文件不存在：" + (item == null ? "" : item.fileName));
            return;
        }

        Uri uri = FileProvider.getUriForFile(
                this,
                getPackageName() + ".fileprovider",
                file
        );
        // ACTION_EDIT 明确要求外部应用打开可写的共享文件 URI；ClipData 让目录授权在
        // Android 的 chooser 和目标编辑器之间稳定传递，避免编辑器保存时失去写权限。
        Intent intent = new Intent(Intent.ACTION_EDIT);
        intent.setDataAndType(uri, resolveMimeType(item.fileName));
        intent.setClipData(ClipData.newRawUri("脚本文件", uri));
        int grantFlags = Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
        intent.addFlags(grantFlags);

        try {
            Intent chooser = Intent.createChooser(intent, "编辑文件");
            chooser.addFlags(grantFlags);
            startActivity(chooser);
            setMessage("已交给外部编辑器：" + item.fileName);
        } catch (ActivityNotFoundException exception) {
            setMessage("没有可编辑该文件的应用：" + item.fileName);
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
        settingsPermissionView.setText("Root 模式：" + formatEnabled(EngineSettings.isRootModeEnabled(this))
                + "\n音量键控制：" + formatEnabled(EngineSettings.isVolumeKeyControlEnabled(this))
                + "\n无障碍服务：" + formatEnabled(AutomationAccessibilityService.isEnabled())
                + "\n悬浮窗权限：" + formatEnabled(overlayEnabled)
                + "\n脚本存储权限："
                + formatEnabled(ScriptCatalog.isScriptStorageAccessible(this))
                + "\n脚本目录：" + ScriptCatalog.getScriptDirectoryDisplayPath(this)
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
        if (requestCode == REQUEST_SCRIPT_STORAGE_PERMISSION) {
            completeScriptStorageSetup();
            return;
        }

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

    @Override
    public void onRequestPermissionsResult(
            int requestCode,
            String[] permissions,
            int[] grantResults
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQUEST_SCRIPT_STORAGE_PERMISSION) {
            return;
        }

        boolean granted = grantResults.length == 2
                && grantResults[0] == PackageManager.PERMISSION_GRANTED
                && grantResults[1] == PackageManager.PERMISSION_GRANTED;
        if (!granted) {
            setMessage("脚本存储权限未开启");
            if (pageContainer != null) {
                showTab(currentTab);
            }
            return;
        }
        completeScriptStorageSetup();
    }

    /**
     * 请求访问真实脚本目录所需的系统授权。
     *
     * Android 11 及以上使用所有文件访问页；Android 10 及以下申请传统读写权限。授权完成后
     * 直接操作固定的 /sdcard/xiaoyv/scripts，不经过目录选择器或私有副本。
     */
    private void ensureScriptStorageAccess() {
        if (ScriptCatalog.isScriptStorageAccessible(this)) {
            completeScriptStorageSetup();
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Intent intent = new Intent(
                    Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName())
            );
            startActivityForResult(intent, REQUEST_SCRIPT_STORAGE_PERMISSION);
            setMessage("请开启所有文件访问权限");
            return;
        }

        requestPermissions(
                new String[] {
                        Manifest.permission.READ_EXTERNAL_STORAGE,
                        Manifest.permission.WRITE_EXTERNAL_STORAGE
                },
                REQUEST_SCRIPT_STORAGE_PERMISSION
        );
    }

    /**
     * 在存储权限可用后创建固定目录、复制内置示例并刷新当前页面。
     */
    private void completeScriptStorageSetup() {
        if (!ScriptCatalog.isScriptStorageAccessible(this)) {
            setMessage("脚本存储权限未开启");
            if (pageContainer != null) {
                showTab(currentTab);
            }
            return;
        }
        if (!ScriptCatalog.ensureScriptDirectory(this)) {
            setMessage("无法创建脚本目录：" + ScriptCatalog.getScriptDirectoryDisplayPath(this));
            if (pageContainer != null) {
                showTab(currentTab);
            }
            return;
        }

        selectedScript = ScriptCatalog.getSelectedScript(this);
        if (pageContainer != null) {
            showTab(currentTab);
        }
        setMessage("脚本目录已就绪：" + ScriptCatalog.getScriptDirectoryDisplayPath(this));
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
        list.setPadding(0, dp(4), 0, dp(4));
        list.setBackgroundColor(COLOR_SURFACE);
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

    /**
     * 创建设置页分组标题。标题只用于区分信息层级，不占用独立页面标题空间。
     */
    private TextView createSectionLabel(String text) {
        return createText(text, 12, COLOR_MUTED, true);
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
        button.setStateListAnimator(null);
        button.setOnClickListener(view -> action.run());
        return button;
    }

    private ImageButton createOpenFileButton(ScriptCatalog.ScriptItem item) {
        ImageButton button = new ImageButton(this);
        button.setImageResource(R.drawable.ic_script_file);
        button.setScaleType(ImageButton.ScaleType.CENTER);
        button.setClickable(true);
        button.setContentDescription("打开文件：" + item.fileName);
        button.setPadding(dp(10), dp(10), dp(10), dp(10));
        button.setBackgroundColor(Color.TRANSPARENT);
        button.setOnClickListener(view -> openScriptFile(item));
        return button;
    }

    /**
     * 创建可更换目录的设置行。点击文字区域编辑路径，右侧文件夹图标打开当前目录。
     */
    private View createScriptDirectoryRow() {
        LinearLayout row = createHorizontalRow();
        row.setPadding(dp(14), dp(10), dp(8), dp(10));
        row.setMinimumHeight(dp(68));
        row.setClickable(true);
        row.setOnClickListener(view -> showScriptDirectoryInputDialog());

        LinearLayout textColumn = new LinearLayout(this);
        textColumn.setOrientation(LinearLayout.VERTICAL);
        TextView titleView = createText("脚本目录", 15, COLOR_TEXT, false);
        TextView pathView = createText(
                ScriptCatalog.getScriptDirectoryDisplayPath(this),
                12,
                COLOR_MUTED,
                false
        );
        pathView.setSingleLine(true);
        pathView.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        pathView.setPadding(0, dp(2), 0, 0);
        textColumn.addView(titleView, matchWidthWrapContent());
        textColumn.addView(pathView, matchWidthWrapContent());
        row.addView(textColumn, new LinearLayout.LayoutParams(
                0,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                1f
        ));

        ImageButton openButton = new ImageButton(this);
        openButton.setImageResource(R.drawable.ic_script_folder);
        openButton.setScaleType(ImageButton.ScaleType.CENTER);
        openButton.setPadding(dp(10), dp(10), dp(10), dp(10));
        openButton.setBackgroundColor(Color.TRANSPARENT);
        openButton.setContentDescription("打开脚本目录");
        openButton.setOnClickListener(view -> openScriptDirectory());
        row.addView(openButton, new LinearLayout.LayoutParams(dp(44), dp(44)));
        return row;
    }

    /**
     * 显示脚本目录输入框。输入值必须是主共享存储中的绝对路径。
     */
    private void showScriptDirectoryInputDialog() {
        if (!ScriptCatalog.isScriptStorageAccessible(this)) {
            ensureScriptStorageAccess();
            return;
        }

        EditText input = new EditText(this);
        input.setSingleLine(true);
        input.setText(ScriptCatalog.getScriptDirectoryDisplayPath(this));
        input.setSelection(input.getText().length());
        input.setSelectAllOnFocus(false);
        input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);

        LinearLayout inputContainer = new LinearLayout(this);
        inputContainer.setPadding(dp(20), 0, dp(20), 0);
        inputContainer.addView(input, matchWidthWrapContent());

        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("脚本目录")
                .setView(inputContainer)
                .setNegativeButton("取消", null)
                .setPositiveButton("确定", null)
                .create();
        dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE)
                .setOnClickListener(view -> applyScriptDirectoryInput(dialog, input)));
        dialog.show();
    }

    /**
     * 校验并保存输入路径。校验失败时保留弹窗，并在输入框上显示具体原因。
     */
    private void applyScriptDirectoryInput(AlertDialog dialog, EditText input) {
        File directory = ScriptCatalog.resolveScriptDirectoryPath(input.getText().toString());
        if (directory == null) {
            input.setError("请输入 /sdcard 下的子文件夹绝对路径");
            return;
        }
        if (!ScriptCatalog.setScriptDirectory(this, directory)
                || !ScriptCatalog.ensureScriptDirectory(this)) {
            input.setError("无法创建或访问该目录");
            return;
        }

        selectedScript = ScriptCatalog.getSelectedScript(this);
        showTab(currentTab);
        setMessage("脚本目录已切换：" + ScriptCatalog.getScriptDirectoryDisplayPath(this));
        dialog.dismiss();
    }

    /**
     * 把当前脚本目录交给用户安装的第三方文件管理器打开。
     *
     * 这里使用 FileProvider 暴露可读写的目录 URI，并通过 ACTION_VIEW 调起应用
     * 选择器；它只负责浏览目录，不使用系统目录选择器修改脚本目录配置。
     */
    private void openScriptDirectory() {
        File directory = ScriptCatalog.getScriptDirectory(this);
        if (!directory.isDirectory()) {
            setMessage("脚本目录不存在：" + ScriptCatalog.getScriptDirectoryDisplayPath(this));
            return;
        }

        Uri uri = FileProvider.getUriForFile(
                this,
                getPackageName() + ".fileprovider",
                directory
        );
        int grantFlags = Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setDataAndType(uri, "resource/folder");
        intent.setClipData(ClipData.newRawUri("脚本目录", uri));
        intent.addFlags(grantFlags);

        try {
            Intent chooser = Intent.createChooser(intent, "选择文件管理器");
            chooser.addFlags(grantFlags);
            startActivity(chooser);
            setMessage("已交给外部文件管理器");
        } catch (ActivityNotFoundException exception) {
            setMessage("没有可打开脚本目录的文件管理器");
        }
    }

    /**
     * 脚本行之间从名称区域开始绘制分隔线，避免单行卡片造成过多边框。
     */
    private View createScriptDivider() {
        View divider = new View(this);
        divider.setBackgroundColor(COLOR_LINE);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(1)
        );
        params.leftMargin = dp(54);
        params.rightMargin = dp(12);
        divider.setLayoutParams(params);
        return divider;
    }

    /**
     * 根据当前选择更新整行底色，让文件选中状态在快速浏览时仍然清晰。
     */
    private void updateScriptRowBackground(View row, String filePath) {
        boolean selected = selectedScript != null && selectedScript.filePath.equals(filePath);
        int color = selected ? COLOR_PRIMARY_SOFT : COLOR_SURFACE;
        row.setBackgroundColor(color);
    }

    /**
     * 创建设置页的白色分组容器，同类设置共享一个边框和统一内边距。
     */
    private LinearLayout createSettingsGroup() {
        LinearLayout group = new LinearLayout(this);
        group.setOrientation(LinearLayout.VERTICAL);
        group.setBackground(makeRoundDrawable(COLOR_SURFACE, dp(6), COLOR_LINE));
        return group;
    }

    /**
     * 创建只显示滑块、不显示 ON/OFF 文本的系统开关。
     */
    private Switch createSettingSwitch() {
        Switch settingSwitch = new Switch(this);
        settingSwitch.setShowText(false);
        settingSwitch.setMinWidth(dp(48));
        settingSwitch.setPadding(dp(8), 0, 0, 0);
        return settingSwitch;
    }

    /**
     * 创建左侧标题说明、右侧开关的设置行。
     */
    private View createSettingRow(String title, String summary, Switch settingSwitch) {
        LinearLayout row = createHorizontalRow();
        row.setPadding(dp(14), dp(10), dp(12), dp(10));
        row.setMinimumHeight(dp(68));

        LinearLayout textColumn = new LinearLayout(this);
        textColumn.setOrientation(LinearLayout.VERTICAL);
        TextView titleView = createText(title, 15, COLOR_TEXT, false);
        TextView summaryView = createText(summary, 12, COLOR_MUTED, false);
        summaryView.setPadding(0, dp(2), 0, 0);
        textColumn.addView(titleView, matchWidthWrapContent());
        textColumn.addView(summaryView, matchWidthWrapContent());
        row.addView(textColumn, new LinearLayout.LayoutParams(
                0,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                1f
        ));
        row.addView(settingSwitch, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
        ));
        return row;
    }

    /**
     * 创建设置分组内部的缩进分隔线。
     */
    private View createSettingsDivider() {
        View divider = new View(this);
        divider.setBackgroundColor(COLOR_LINE);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(1)
        );
        params.leftMargin = dp(14);
        params.rightMargin = dp(14);
        divider.setLayoutParams(params);
        return divider;
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
                dp(48)
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
