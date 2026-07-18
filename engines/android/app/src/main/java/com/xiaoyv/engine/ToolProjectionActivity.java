/**
 * 文件用途：在设备真实屏幕上以 1:1 像素显示 PC 工具上传的投影图片。
 */
package com.xiaoyv.engine;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import java.io.File;
import java.io.IOException;

/**
 * 投影只负责显示图片。退出方式就是设备返回键；桌面端不维护额外状态，也不提供关闭协议。
 */
public final class ToolProjectionActivity extends Activity {
    private static final String EXTRA_FILE_ID = "fileId";

    private File imageFile;
    private Bitmap bitmap;

    /** 从 :engine 进程启动主进程 Activity。 */
    static void open(Context context, String fileId) throws IOException {
        ToolImageStore.resolve(context, fileId);
        Intent intent = new Intent(context, ToolProjectionActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        intent.putExtra(EXTRA_FILE_ID, fileId);
        context.startActivity(intent);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setStatusBarColor(Color.BLACK);
        getWindow().setNavigationBarColor(Color.BLACK);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // 内容坐标从物理屏幕左上角开始；系统栏隐藏后不为其保留任何 inset。
            getWindow().setDecorFitsSystemWindows(false);
        }
        hideSystemBars();

        try {
            imageFile = ToolImageStore.resolve(this, getIntent().getStringExtra(EXTRA_FILE_ID));
            bitmap = BitmapFactory.decodeFile(imageFile.getAbsolutePath());
            if (bitmap == null) throw new IOException("设备无法解码投影图片");
            setContentView(new PixelImageView(this, bitmap));
            sendFloatingControlAction(FloatingControlService.ACTION_TEMPORARY_HIDE);
        } catch (IOException exception) {
            finish();
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    @Override
    protected void onDestroy() {
        sendFloatingControlAction(FloatingControlService.ACTION_RESTORE_AFTER_PROJECTION);
        if (bitmap != null) {
            bitmap.recycle();
            bitmap = null;
        }
        if (imageFile != null) imageFile.delete();
        super.onDestroy();
    }

    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                );
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            );
        }
    }

    private void sendFloatingControlAction(String action) {
        Intent intent = new Intent(this, FloatingControlService.class);
        intent.setAction(action);
        startService(intent);
    }

    /** 不缩放、不滤波，从屏幕左上角逐像素绘制。超出屏幕的部分自然裁剪。 */
    private static final class PixelImageView extends View {
        private final Bitmap bitmap;
        private final Paint paint = new Paint(Paint.DITHER_FLAG);

        private PixelImageView(Context context, Bitmap bitmap) {
            super(context);
            this.bitmap = bitmap;
            paint.setFilterBitmap(false);
            setBackgroundColor(Color.BLACK);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            canvas.drawBitmap(bitmap, 0, 0, paint);
        }
    }
}
