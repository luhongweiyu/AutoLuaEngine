/**
 * 文件用途：实现 RapidOCR PP-OCR ONNX 模型的加载、释放、图片识别和找字平台桥。
 */
package com.xiaoyv.engine;

import ai.onnxruntime.OnnxTensor;
import ai.onnxruntime.OnnxValue;
import ai.onnxruntime.OrtEnvironment;
import ai.onnxruntime.OrtException;
import ai.onnxruntime.OrtSession;
import ai.onnxruntime.TensorInfo;
import ai.onnxruntime.ValueInfo;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Matrix;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.nio.FloatBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * RapidOCR Android ONNX 平台实现。
 *
 * 本类只实现 Android 必须依赖 ONNX Runtime 和 Bitmap 的部分。Lua、JS、Go 与插件均通过
 * libengine.so 的统一 C ABI 调用，不会直接接触 OrtSession、Bitmap 或 Java 对象。模型按脚本
 * 显式 load/release 管理；内置和自定义模型最终进入同一组 session 缓存，相同配置重复加载
 * 会直接复用。
 */
public final class OcrPlatformBridge {
    private static final Object MODEL_LOCK = new Object();
    private static final Map<String, OcrModel> MODELS_BY_NAME = new HashMap<>();
    private static final String BUILTIN_MODEL_ASSET_DIRECTORY = "ocr/ppocr_v4_mobile";
    private static final String BUILTIN_MODEL_STORAGE_DIRECTORY = "ocr/ppocr_v4_mobile_v3_9_1";
    private static final String BUILTIN_DET_FILE = "ch_PP-OCRv4_det_mobile.onnx";
    private static final String BUILTIN_REC_FILE = "ch_PP-OCRv4_rec_mobile.onnx";
    private static final String BUILTIN_CLS_FILE = "ch_ppocr_mobile_v2.0_cls_mobile.onnx";
    private static final String BUILTIN_KEYS_FILE = "ppocr_keys_v1.txt";
    private static final long BUILTIN_DET_BYTES = 4_745_517L;
    private static final long BUILTIN_REC_BYTES = 10_857_958L;
    private static final long BUILTIN_CLS_BYTES = 585_532L;
    private static final long BUILTIN_KEYS_BYTES = 26_249L;
    private static final int ASSET_COPY_BUFFER_BYTES = 64 * 1024;
    private static OrtEnvironment environment;

    private OcrPlatformBridge() {
    }

    /**
     * native 侧唯一 OCR 调用入口。
     *
     * operation 只能是固定命令，argumentsJson 由 core/api 构造。所有结果都封装为
     * {"ok":boolean,"data":...}，让 C++ 能区分模型未加载、图片错误和未找到文本。
     */
    public static String call(String operation, String argumentsJson) {
        try {
            JSONObject arguments = new JSONObject(argumentsJson == null ? "{}" : argumentsJson);
            synchronized (MODEL_LOCK) {
                if ("loadBuiltin".equals(operation)) {
                    return loadBuiltin(arguments);
                }
                if ("load".equals(operation)) {
                    return load(arguments);
                }
                if ("release".equals(operation)) {
                    return release(arguments);
                }
                if ("isLoaded".equals(operation)) {
                    return isLoaded(arguments);
                }
                if ("read".equals(operation)) {
                    return read(arguments);
                }
                if ("findText".equals(operation)) {
                    return findText(arguments);
                }
                return failure("未知 OCR 操作：" + safe(operation));
            }
        } catch (JSONException exception) {
            return failure("OCR 参数 JSON 无效：" + safeMessage(exception));
        } catch (RuntimeException exception) {
            return failure("OCR 平台调用失败：" + safeMessage(exception));
        }
    }

    /**
     * 把 APK 内置 PP-OCRv4 mobile 模型准备到应用私有目录，再进入与自定义模型相同的加载路径。
     *
     * ONNX Runtime 的路径接口不能直接读取压缩 assets。模型只在首次使用时复制；后续加载通过
     * 固定版本目录和文件长度确认已有副本，不重复产生磁盘 IO。
     */
    private static String loadBuiltin(JSONObject arguments) {
        String name = required(arguments, "name");
        int threads = Math.max(1, arguments.optInt("threads", 2));
        if (name.isEmpty()) {
            return failure("内置 OCR 模型名称不能为空");
        }

        Context context = AndroidHostBridge.appContext();
        if (context == null) {
            return failure("Android 应用上下文尚未初始化");
        }

        File modelDirectory = new File(
                context.getNoBackupFilesDir(),
                BUILTIN_MODEL_STORAGE_DIRECTORY
        );
        if (!modelDirectory.isDirectory() && !modelDirectory.mkdirs()) {
            return failure("无法创建内置 OCR 模型目录：" + modelDirectory.getAbsolutePath());
        }

        try {
            File det = prepareBuiltinAsset(
                    context,
                    modelDirectory,
                    BUILTIN_DET_FILE,
                    BUILTIN_DET_BYTES
            );
            File rec = prepareBuiltinAsset(
                    context,
                    modelDirectory,
                    BUILTIN_REC_FILE,
                    BUILTIN_REC_BYTES
            );
            File cls = prepareBuiltinAsset(
                    context,
                    modelDirectory,
                    BUILTIN_CLS_FILE,
                    BUILTIN_CLS_BYTES
            );
            File keys = prepareBuiltinAsset(
                    context,
                    modelDirectory,
                    BUILTIN_KEYS_FILE,
                    BUILTIN_KEYS_BYTES
            );

            JSONObject loadArguments = new JSONObject();
            loadArguments.put("name", name);
            loadArguments.put("det", det.getAbsolutePath());
            loadArguments.put("rec", rec.getAbsolutePath());
            loadArguments.put("cls", cls.getAbsolutePath());
            loadArguments.put("keys", keys.getAbsolutePath());
            loadArguments.put("threads", threads);
            return load(loadArguments);
        } catch (IOException | JSONException exception) {
            return failure("准备内置 OCR 模型失败：" + safeMessage(exception));
        }
    }

    /**
     * 取得一个内置模型的应用私有文件。
     *
     * 写入先落到同目录临时文件，长度完整后再原子改名；进程在复制中途退出不会留下被下一次
     * 加载误用的半个 ONNX 文件。asset 文件名和目标文件名保持一致，便于设备侧排查。
     */
    private static File prepareBuiltinAsset(
            Context context,
            File modelDirectory,
            String fileName,
            long expectedBytes
    ) throws IOException {
        File target = new File(modelDirectory, fileName);
        if (target.isFile() && target.canRead() && target.length() == expectedBytes) {
            return target;
        }

        File temporary = new File(modelDirectory, fileName + ".tmp");
        if (temporary.exists() && !temporary.delete()) {
            throw new IOException("无法清理模型临时文件：" + temporary.getAbsolutePath());
        }

        String assetPath = BUILTIN_MODEL_ASSET_DIRECTORY + "/" + fileName;
        try (InputStream input = context.getAssets().open(assetPath);
             FileOutputStream output = new FileOutputStream(temporary, false)) {
            byte[] buffer = new byte[ASSET_COPY_BUFFER_BYTES];
            int readCount;
            while ((readCount = input.read(buffer)) != -1) {
                if (readCount > 0) {
                    output.write(buffer, 0, readCount);
                }
            }
            output.getFD().sync();
        } catch (IOException exception) {
            temporary.delete();
            throw exception;
        }

        if (temporary.length() != expectedBytes) {
            long actualBytes = temporary.length();
            temporary.delete();
            throw new IOException(
                    "内置模型长度不正确：" + fileName
                            + "，期望 " + expectedBytes + "，实际 " + actualBytes
            );
        }
        if (target.exists() && !target.delete()) {
            temporary.delete();
            throw new IOException("无法替换旧模型文件：" + target.getAbsolutePath());
        }
        if (!temporary.renameTo(target)) {
            temporary.delete();
            throw new IOException("无法保存内置模型文件：" + target.getAbsolutePath());
        }
        return target;
    }

    /** 加载或复用一组 RapidOCR ONNX 检测、识别和可选方向分类模型。 */
    private static String load(JSONObject arguments) {
        String name = required(arguments, "name");
        String detPath = required(arguments, "det");
        String recPath = required(arguments, "rec");
        String clsPath = arguments.optString("cls", "");
        String keysPath = required(arguments, "keys");
        int threads = Math.max(1, arguments.optInt("threads", 2));
        if (name.isEmpty() || detPath.isEmpty() || recPath.isEmpty() || keysPath.isEmpty()) {
            return failure("OCR 模型名称、det、rec 和 keys 路径不能为空");
        }

        String canonicalDet = canonicalFile(detPath, "det 模型");
        String canonicalRec = canonicalFile(recPath, "rec 模型");
        String canonicalKeys = canonicalFile(keysPath, "keys 字典");
        String canonicalCls = clsPath.trim().isEmpty() ? "" : canonicalFile(clsPath, "cls 模型");
        if (canonicalDet.isEmpty() || canonicalRec.isEmpty() || canonicalKeys.isEmpty()
                || (!clsPath.trim().isEmpty() && canonicalCls.isEmpty())) {
            return failure("OCR 模型文件不存在或不可读");
        }

        String fingerprint = canonicalDet + "\n" + canonicalRec + "\n" + canonicalCls + "\n"
                + canonicalKeys + "\n" + threads;
        OcrModel namedModel = MODELS_BY_NAME.get(name);
        if (namedModel != null) {
            if (fingerprint.equals(namedModel.fingerprint)) {
                return success(objectOf("reused", true, "name", name));
            }
            return failure("OCR 模型名称已加载不同配置，请先 release：" + name);
        }

        // 相同模型可以被不同脚本名称复用。使用引用计数，任何一个名称 release 都不会提前
        // 关闭仍被其他名称使用的 OrtSession。
        for (OcrModel existing : MODELS_BY_NAME.values()) {
            if (fingerprint.equals(existing.fingerprint)) {
                existing.referenceCount++;
                MODELS_BY_NAME.put(name, existing);
                return success(objectOf("reused", true, "name", name));
            }
        }

        try {
            OcrModel model = createModel(
                    fingerprint,
                    canonicalDet,
                    canonicalRec,
                    canonicalCls,
                    canonicalKeys,
                    threads
            );
            MODELS_BY_NAME.put(name, model);
            return success(objectOf("reused", false, "name", name));
        } catch (Exception exception) {
            return failure("加载 RapidOCR 模型失败：" + safeMessage(exception));
        }
    }

    /** 释放一个脚本名称持有的模型引用；最后一个引用释放时关闭全部 OrtSession。 */
    private static String release(JSONObject arguments) {
        String name = required(arguments, "name");
        OcrModel model = MODELS_BY_NAME.remove(name);
        if (model == null) {
            return success(objectOf("released", false, "name", name));
        }

        model.referenceCount--;
        if (model.referenceCount <= 0) {
            model.close();
        }
        return success(objectOf("released", true, "name", name));
    }

    /** 查询指定脚本模型名称是否已加载。 */
    private static String isLoaded(JSONObject arguments) {
        String name = required(arguments, "name");
        return success(objectOf("loaded", MODELS_BY_NAME.containsKey(name), "name", name));
    }

    /** 识别一张普通图片文件，返回文字、坐标、尺寸和置信度数组。 */
    private static String read(JSONObject arguments) {
        OcrModel model = requireModel(arguments);
        if (model == null) {
            return failure("OCR 模型尚未加载");
        }
        String path = required(arguments, "path");
        if (path.isEmpty()) {
            return failure("OCR 图片路径不能为空");
        }

        Bitmap bitmap = null;
        try {
            bitmap = decodeImage(path);
            if (bitmap == null) {
                return failure("无法解码 OCR 图片：" + path);
            }
            List<OcrBox> boxes = recognize(model, bitmap, options(arguments));
            return success(boxesToJson(boxes));
        } catch (Exception exception) {
            return failure("OCR 识别失败：" + safeMessage(exception));
        } finally {
            recycle(bitmap);
        }
    }

    /** 识别图片后查找指定文字，返回首次命中坐标和尺寸。 */
    private static String findText(JSONObject arguments) {
        OcrModel model = requireModel(arguments);
        if (model == null) {
            return failure("OCR 模型尚未加载");
        }
        String path = required(arguments, "path");
        String target = required(arguments, "text");
        if (path.isEmpty() || target.isEmpty()) {
            return failure("OCR 图片路径和要查找的文字不能为空");
        }

        boolean exact = options(arguments).optBoolean("exact", false);
        Bitmap bitmap = null;
        try {
            bitmap = decodeImage(path);
            if (bitmap == null) {
                return failure("无法解码 OCR 图片：" + path);
            }
            List<OcrBox> boxes = recognize(model, bitmap, options(arguments));
            for (OcrBox box : boxes) {
                boolean matched = exact ? target.equals(box.text) : box.text.contains(target);
                if (matched) {
                    JSONObject data = new JSONObject();
                    data.put("found", true);
                    data.put("x", box.x);
                    data.put("y", box.y);
                    data.put("w", box.width);
                    data.put("h", box.height);
                    data.put("text", box.text);
                    data.put("score", box.score);
                    return success(data);
                }
            }
            return success(objectOf("found", false));
        } catch (Exception exception) {
            return failure("OCR 找字失败：" + safeMessage(exception));
        } finally {
            recycle(bitmap);
        }
    }

    /** 创建一组可关闭的 ONNX Runtime session，并读取识别字典。 */
    private static OcrModel createModel(
            String fingerprint,
            String detPath,
            String recPath,
            String clsPath,
            String keysPath,
            int threads
    ) throws Exception {
        OrtEnvironment runtime = environment();
        OrtSession detSession = null;
        OrtSession recSession = null;
        OrtSession clsSession = null;
        OrtSession.SessionOptions sessionOptions = new OrtSession.SessionOptions();
        try {
            sessionOptions.setIntraOpNumThreads(threads);
            sessionOptions.setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT);
            detSession = runtime.createSession(detPath, sessionOptions);
            recSession = runtime.createSession(recPath, sessionOptions);
            if (!clsPath.isEmpty()) {
                clsSession = runtime.createSession(clsPath, sessionOptions);
            }
            List<String> keys = readKeys(keysPath);
            if (keys.isEmpty()) {
                throw new IOException("识别字典为空");
            }
            // RapidOCR 的 ONNX 模型并不固定使用 48 高输入。旧项目中的 crnn_lite_lstm
            // 就要求 32 高，所以模型加载时读取真实 NCHW shape，后续推理不再写死尺寸。
            ModelInput detInput = readModelInput(detSession, "det");
            ModelInput recInput = readModelInput(recSession, "rec");
            ModelInput clsInput = clsSession == null ? null : readModelInput(clsSession, "cls");
            return new OcrModel(
                    fingerprint,
                    detSession,
                    recSession,
                    clsSession,
                    detInput,
                    recInput,
                    clsInput,
                    keys
            );
        } catch (Exception exception) {
            closeQuietly(detSession);
            closeQuietly(recSession);
            closeQuietly(clsSession);
            throw exception;
        } finally {
            sessionOptions.close();
        }
    }

    /** Lazily 初始化全局 ORT 环境。环境本身不因单个模型 release 而关闭。 */
    private static OrtEnvironment environment() throws OrtException {
        if (environment == null) {
            environment = OrtEnvironment.getEnvironment("XiaoyvRapidOCR");
        }
        return environment;
    }

    /**
     * 从文本字典读取可识别字符。
     *
     * 不同 RapidOCR 导出模型对 CTC blank 的编码略有差异：有的输出类别数与 keys.txt 行数
     * 完全一致，有的额外保留 blank 或 blank+空格。具体映射在识别结果的真实输出 shape 已知
     * 后再判定，加载阶段绝不擅自改写用户字典。
     */
    private static List<String> readKeys(String path) throws IOException {
        List<String> keys = new ArrayList<>();
        try (BufferedReader reader = new BufferedReader(new FileReader(path))) {
            String line;
            while ((line = reader.readLine()) != null) {
                if (!line.isEmpty()) {
                    keys.add(line);
                }
            }
        }
        return keys;
    }

    /** 读取一个 ONNX 模型的 NCHW 输入约束，动态维度用 0 表示。 */
    private static ModelInput readModelInput(OrtSession session, String label) throws OrtException {
        String inputName = session.getInputNames().iterator().next();
        ValueInfo valueInfo = session.getInputInfo().get(inputName).getInfo();
        if (!(valueInfo instanceof TensorInfo)) {
            throw new IllegalStateException(label + " 模型输入不是 Tensor");
        }
        long[] shape = ((TensorInfo) valueInfo).getShape();
        if (shape.length != 4) {
            throw new IllegalStateException(label + " 模型输入必须是 NCHW 四维 Tensor");
        }
        if (shape[1] > 0 && shape[1] != 3) {
            throw new IllegalStateException(label + " 模型输入通道数必须为 3");
        }
        return new ModelInput(inputName, fixedDimension(shape[2]), fixedDimension(shape[3]));
    }

    /** 只接受合理的固定图像尺寸；-1 和 0 均代表 ONNX 动态维度。 */
    private static int fixedDimension(long value) {
        if (value <= 0L) {
            return 0;
        }
        if (value > 4096L) {
            throw new IllegalStateException("OCR 模型固定输入尺寸过大");
        }
        return (int) value;
    }

    /**
     * 把原图按 DBNet 要求缩放到 32 的倍数。
     *
     * 动态输入模型保持比例并向下取整，避免超过 maxSideLen；固定输入模型则严格使用模型
     * 声明的尺寸，兼容不同版本 RapidOCR 导出的 ONNX 文件。
     */
    private static ResizeInfo detectorResize(
            int width,
            int height,
            int maxSideLen,
            ModelInput input
    ) {
        if (input.fixedWidth > 0 && input.fixedHeight > 0) {
            return new ResizeInfo(
                    input.fixedWidth,
                    input.fixedHeight,
                    input.fixedWidth / (float) width,
                    input.fixedHeight / (float) height
            );
        }

        float scale = 1.0f;
        if (input.fixedHeight > 0) {
            scale = input.fixedHeight / (float) height;
        } else if (input.fixedWidth > 0) {
            scale = input.fixedWidth / (float) width;
        } else {
            int longest = Math.max(width, height);
            if (longest > maxSideLen) {
                scale = maxSideLen / (float) longest;
            }
        }
        int resizedWidth = input.fixedWidth > 0
                ? input.fixedWidth
                : roundDown32(Math.max(1, Math.round(width * scale)));
        int resizedHeight = input.fixedHeight > 0
                ? input.fixedHeight
                : roundDown32(Math.max(1, Math.round(height * scale)));
        return new ResizeInfo(resizedWidth, resizedHeight, resizedWidth / (float) width, resizedHeight / (float) height);
    }

    /** DBNet 文本检测，返回原图坐标系中的文本候选框。 */
    private static List<OcrBox> detect(OcrModel model, Bitmap original, JSONObject options) throws Exception {
        int maxSideLen = clamp(options.optInt("maxSideLen", 960), 32, 2048);
        float binaryThreshold = clampFloat((float) options.optDouble("threshold", 0.30), 0.01f, 0.99f);
        float boxThreshold = clampFloat((float) options.optDouble("boxThreshold", 0.50), 0.01f, 0.99f);
        int padding = Math.max(0, options.optInt("padding", 2));
        float unClipRatio = clampFloat((float) options.optDouble("unClipRatio", 1.5), 0.0f, 4.0f);

        ResizeInfo resize = detectorResize(
                original.getWidth(),
                original.getHeight(),
                maxSideLen,
                model.detInput
        );
        Bitmap scaled = Bitmap.createScaledBitmap(original, resize.width, resize.height, true);
        try {
            float[] input = detectorInput(scaled);
            TensorOutput output = run(
                    model.detSession,
                    model.detInput.name,
                    input,
                    new long[] {1L, 3L, resize.height, resize.width}
            );
            ScoreMap scoreMap = scoreMap(output, resize.width, resize.height);
            List<Component> components = components(scoreMap, binaryThreshold, boxThreshold);
            List<OcrBox> boxes = new ArrayList<>();
            for (Component component : components) {
                int componentWidth = component.right - component.left + 1;
                int componentHeight = component.bottom - component.top + 1;
                float perimeter = Math.max(1.0f, 2.0f * (componentWidth + componentHeight));
                int expand = Math.round(componentWidth * componentHeight * unClipRatio / perimeter);
                int left = clamp(Math.round((component.left - expand) * resize.width / (float) scoreMap.width / resize.scaleX) - padding,
                        0, original.getWidth() - 1);
                int top = clamp(Math.round((component.top - expand) * resize.height / (float) scoreMap.height / resize.scaleY) - padding,
                        0, original.getHeight() - 1);
                int right = clamp(Math.round((component.right + 1 + expand) * resize.width / (float) scoreMap.width / resize.scaleX) + padding,
                        left + 1, original.getWidth());
                int bottom = clamp(Math.round((component.bottom + 1 + expand) * resize.height / (float) scoreMap.height / resize.scaleY) + padding,
                        top + 1, original.getHeight());
                if (right - left < 2 || bottom - top < 2) {
                    continue;
                }
                boxes.add(new OcrBox(left, top, right - left, bottom - top, component.score));
            }
            Collections.sort(boxes, new Comparator<OcrBox>() {
                @Override
                public int compare(OcrBox left, OcrBox right) {
                    int lineTolerance = Math.max(8, Math.min(left.height, right.height) / 2);
                    if (Math.abs(left.y - right.y) <= lineTolerance) {
                        return Integer.compare(left.x, right.x);
                    }
                    return Integer.compare(left.y, right.y);
                }
            });
            return boxes;
        } finally {
            recycle(scaled);
        }
    }

    /** 执行文字检测、可选方向分类和 CTC 文字识别。 */
    private static List<OcrBox> recognize(OcrModel model, Bitmap original, JSONObject options) throws Exception {
        List<OcrBox> boxes = detect(model, original, options);
        boolean useAngle = options.optBoolean("useAngle", false) && model.clsSession != null;
        float minScore = clampFloat((float) options.optDouble("minScore", 0.0), 0.0f, 1.0f);
        List<OcrBox> result = new ArrayList<>();
        for (OcrBox box : boxes) {
            Bitmap crop = Bitmap.createBitmap(original, box.x, box.y, box.width, box.height);
            Bitmap oriented = crop;
            try {
                if (useAngle && shouldRotate(model, crop)) {
                    Matrix matrix = new Matrix();
                    matrix.postRotate(180.0f);
                    oriented = Bitmap.createBitmap(crop, 0, 0, crop.getWidth(), crop.getHeight(), matrix, true);
                }
                Recognition recognition = recognizeCrop(model, oriented, options);
                if (!recognition.text.isEmpty() && recognition.score >= minScore) {
                    box.text = recognition.text;
                    box.score = recognition.score;
                    result.add(box);
                }
            } finally {
                if (oriented != crop) {
                    recycle(oriented);
                }
                recycle(crop);
            }
        }
        return result;
    }

    /**
     * RapidOCR PP-OCR DBNet 输入预处理。
     *
     * RapidOCR 的参考实现先把 Android RGBA 转为 OpenCV BGR，再按 BGR 平面进行标准化。
     * Bitmap 读取到的是 ARGB，因此这里显式写入 B、G、R，不能按直觉直接填 RGB。
     */
    private static float[] detectorInput(Bitmap bitmap) {
        int width = bitmap.getWidth();
        int height = bitmap.getHeight();
        int[] pixels = new int[width * height];
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height);
        float[] result = new float[3 * width * height];
        int plane = width * height;
        for (int index = 0; index < pixels.length; ++index) {
            int color = pixels[index];
            float red = ((color >> 16) & 0xff) / 255.0f;
            float green = ((color >> 8) & 0xff) / 255.0f;
            float blue = (color & 0xff) / 255.0f;
            result[index] = (blue - 0.485f) / 0.229f;
            result[plane + index] = (green - 0.456f) / 0.224f;
            result[plane * 2 + index] = (red - 0.406f) / 0.225f;
        }
        return result;
    }

    /**
     * PP-OCR 识别或方向分类输入预处理。
     *
     * targetWidth 为 0 时代表模型宽度动态，缩放后不额外补白；固定宽模型才在右侧补白。
     */
    private static float[] recognizerInput(Bitmap bitmap, int targetHeight, int targetWidth) {
        int contentWidth = clamp(
                Math.round(bitmap.getWidth() * (targetHeight / (float) Math.max(1, bitmap.getHeight()))),
                8,
                Math.max(8, targetWidth == 0 ? Integer.MAX_VALUE : targetWidth)
        );
        int actualTargetWidth = targetWidth == 0 ? contentWidth : targetWidth;
        Bitmap scaled = Bitmap.createScaledBitmap(bitmap, contentWidth, targetHeight, true);
        try {
            int[] source = new int[contentWidth * targetHeight];
            scaled.getPixels(source, 0, contentWidth, 0, 0, contentWidth, targetHeight);
            int plane = actualTargetWidth * targetHeight;
            float[] result = new float[plane * 3];
            for (int y = 0; y < targetHeight; ++y) {
                for (int x = 0; x < actualTargetWidth; ++x) {
                    int targetIndex = y * actualTargetWidth + x;
                    int color = x < contentWidth ? source[y * contentWidth + x] : 0xffffffff;
                    result[targetIndex] = ((color & 0xff) / 255.0f - 0.5f) / 0.5f;
                    result[plane + targetIndex] = (((color >> 8) & 0xff) / 255.0f - 0.5f) / 0.5f;
                    result[plane * 2 + targetIndex] = (((color >> 16) & 0xff) / 255.0f - 0.5f) / 0.5f;
                }
            }
            return result;
        } finally {
            recycle(scaled);
        }
    }

    /** 识别单个文本框，并按 CTC 规则去掉 blank 和连续重复字符。 */
    private static Recognition recognizeCrop(OcrModel model, Bitmap crop, JSONObject options) throws Exception {
        int targetHeight = model.recInput.fixedHeight > 0 ? model.recInput.fixedHeight : 48;
        int maximumWidth = clamp(options.optInt("maxRecognizerWidth", 1024), 32, 4096);
        int scaledWidth = clamp(
                Math.round(crop.getWidth() * (targetHeight / (float) Math.max(1, crop.getHeight()))),
                8,
                maximumWidth
        );
        int targetWidth = model.recInput.fixedWidth > 0 ? model.recInput.fixedWidth : scaledWidth;
        TensorOutput output = run(
                model.recSession,
                model.recInput.name,
                recognizerInput(crop, targetHeight, targetWidth),
                new long[] {1L, 3L, targetHeight, targetWidth}
        );
        int classes = output.lastDimension();
        CtcLayout layout = ctcLayout(model.keys, classes);
        if (layout == null || output.values.length == 0 || output.values.length % classes != 0) {
            throw new IllegalStateException(
                    "识别模型输出维度与 keys 字典不匹配：输出=" + Arrays.toString(output.shape)
                            + "，类别=" + classes + "，字典行数=" + model.keys.size()
            );
        }
        int steps = output.values.length / classes;
        StringBuilder text = new StringBuilder();
        float scoreTotal = 0.0f;
        int selectedCount = 0;
        int previousIndex = -1;
        for (int step = 0; step < steps; ++step) {
            int offset = step * classes;
            int bestIndex = 0;
            float bestValue = output.values[offset];
            for (int index = 1; index < classes; ++index) {
                float value = output.values[offset + index];
                if (value > bestValue) {
                    bestValue = value;
                    bestIndex = index;
                }
            }
            if (bestIndex != layout.blankIndex && bestIndex != previousIndex) {
                String character = layout.characterAt(bestIndex, model.keys);
                if (character != null) {
                    text.append(character);
                    // CRNN 导出模型的输出既可能是概率，也可能是原始 logits。不能直接将最大值
                    // 截断到 0 到 1，否则常见的负 logits 会把所有识别置信度错误写成 0。以
                    // 已找到的最大值为基准计算稳定 softmax，既避免溢出，也保留每个 CTC 字符
                    // 的真实置信度语义。
                    scoreTotal += maximumClassProbability(output.values, offset, classes, bestValue);
                    selectedCount++;
                }
            }
            previousIndex = bestIndex;
        }
        return new Recognition(text.toString(), selectedCount == 0 ? 0.0f : scoreTotal / selectedCount);
    }

    /** 可选 PP-OCR 方向分类，模型判断为倒置且置信度足够时旋转 180 度。 */
    private static boolean shouldRotate(OcrModel model, Bitmap crop) throws Exception {
        int targetHeight = model.clsInput.fixedHeight > 0 ? model.clsInput.fixedHeight : 48;
        int targetWidth = model.clsInput.fixedWidth > 0 ? model.clsInput.fixedWidth : 192;
        TensorOutput output = run(
                model.clsSession,
                model.clsInput.name,
                recognizerInput(crop, targetHeight, targetWidth),
                new long[] {1L, 3L, targetHeight, targetWidth}
        );
        if (output.values.length < 2) {
            return false;
        }
        int bestIndex = output.values[1] > output.values[0] ? 1 : 0;
        return bestIndex == 1 && classProbability(output.values, 0, 2, bestIndex) >= 0.90f;
    }

    /** 执行一个 ONNX 模型并读取紧凑 float 输出，避免 getValue() 的多维数组和装箱分配。 */
    private static TensorOutput run(
            OrtSession session,
            String inputName,
            float[] input,
            long[] shape
    ) throws Exception {
        try (OnnxTensor tensor = OnnxTensor.createTensor(environment(), FloatBuffer.wrap(input), shape);
             OrtSession.Result result = session.run(Collections.singletonMap(inputName, tensor))) {
            if (!result.iterator().hasNext()) {
                throw new IllegalStateException("ONNX 模型没有输出");
            }
            OnnxValue output = result.iterator().next().getValue();
            if (!(output instanceof OnnxTensor)) {
                throw new IllegalStateException("ONNX 模型输出不是 Tensor");
            }
            OnnxTensor tensorOutput = (OnnxTensor) output;
            FloatBuffer buffer = tensorOutput.getFloatBuffer();
            float[] values = new float[buffer.remaining()];
            buffer.get(values);
            ValueInfo info = tensorOutput.getInfo();
            if (!(info instanceof TensorInfo)) {
                throw new IllegalStateException("ONNX 输出信息不是 Tensor");
            }
            return new TensorOutput(values, ((TensorInfo) info).getShape());
        }
    }

    /** 根据 ONNX 输出 shape 读取 DBNet 概率图尺寸，并兼容缺失 shape 的旧导出模型。 */
    private static ScoreMap scoreMap(TensorOutput output, int inputWidth, int inputHeight) {
        float[] values = output.values;
        long[] shape = output.shape;
        if (shape.length >= 2) {
            long width = shape[shape.length - 1];
            long height = shape[shape.length - 2];
            long area = width * height;
            if (width > 0 && height > 0 && area > 0 && area <= values.length) {
                return new ScoreMap(values, (int) width, (int) height, 0);
            }
        }

        int[] divisors = new int[] {1, 2, 4, 8, 16};
        for (int divisor : divisors) {
            int width = Math.max(1, inputWidth / divisor);
            int height = Math.max(1, inputHeight / divisor);
            int area = width * height;
            if (values.length == area) {
                return new ScoreMap(values, width, height, 0);
            }
            if (values.length == area * 2) {
                return new ScoreMap(values, width, height, area);
            }
        }

        // 兼容个别导出模型的动态输出尺寸：按输入宽高比例估算二维概率图。
        float aspect = inputWidth / (float) Math.max(1, inputHeight);
        int height = Math.max(1, Math.round((float) Math.sqrt(values.length / Math.max(0.01f, aspect))));
        int width = Math.max(1, values.length / height);
        if (width * height > values.length) {
            height = Math.max(1, values.length / width);
        }
        return new ScoreMap(values, width, height, 0);
    }

    /**
     * 在 DBNet 概率图上提取连通文本区域。
     *
     * RapidOCR 的参考实现会先进行 2x2 膨胀，再提取轮廓。这里以布尔点阵完成同样的合并，
     * 不引入 OpenCV 依赖；框分数仍只统计原始前景像素，避免膨胀把分数拉低。
     */
    private static List<Component> components(ScoreMap map, float binaryThreshold, float boxThreshold) {
        int area = map.width * map.height;
        boolean[] source = new boolean[area];
        boolean[] selected = new boolean[area];
        for (int y = 0; y < map.height; ++y) {
            for (int x = 0; x < map.width; ++x) {
                int index = y * map.width + x;
                if (map.valueAt(index) < binaryThreshold) {
                    continue;
                }
                source[index] = true;
                for (int offsetY = 0; offsetY <= 1; ++offsetY) {
                    int dilatedY = y + offsetY;
                    if (dilatedY >= map.height) {
                        continue;
                    }
                    for (int offsetX = 0; offsetX <= 1; ++offsetX) {
                        int dilatedX = x + offsetX;
                        if (dilatedX < map.width) {
                            selected[dilatedY * map.width + dilatedX] = true;
                        }
                    }
                }
            }
        }

        List<Component> result = new ArrayList<>();
        int[] queue = new int[area];
        for (int start = 0; start < area; ++start) {
            if (!selected[start]) {
                continue;
            }
            selected[start] = false;
            int head = 0;
            int tail = 0;
            queue[tail++] = start;
            int left = start % map.width;
            int right = left;
            int top = start / map.width;
            int bottom = top;
            float scoreTotal = 0.0f;
            int sourceCount = 0;

            while (head < tail) {
                int current = queue[head++];
                int x = current % map.width;
                int y = current / map.width;
                left = Math.min(left, x);
                right = Math.max(right, x);
                top = Math.min(top, y);
                bottom = Math.max(bottom, y);
                if (source[current]) {
                    scoreTotal += map.valueAt(current);
                    sourceCount++;
                }

                for (int dy = -1; dy <= 1; ++dy) {
                    int neighborY = y + dy;
                    if (neighborY < 0 || neighborY >= map.height) {
                        continue;
                    }
                    for (int dx = -1; dx <= 1; ++dx) {
                        int neighborX = x + dx;
                        if (neighborX < 0 || neighborX >= map.width || (dx == 0 && dy == 0)) {
                            continue;
                        }
                        int neighbor = neighborY * map.width + neighborX;
                        if (selected[neighbor]) {
                            selected[neighbor] = false;
                            queue[tail++] = neighbor;
                        }
                    }
                }
            }

            if (sourceCount >= 3 && scoreTotal / sourceCount >= boxThreshold) {
                result.add(new Component(left, top, right, bottom, scoreTotal / sourceCount));
            }
        }
        return result;
    }

    /** 将识别结果转换为跨语言稳定 JSON。 */
    private static JSONObject boxesToJson(List<OcrBox> boxes) throws JSONException {
        JSONArray items = new JSONArray();
        for (OcrBox box : boxes) {
            JSONObject item = new JSONObject();
            item.put("text", box.text);
            item.put("x", box.x);
            item.put("y", box.y);
            item.put("w", box.width);
            item.put("h", box.height);
            item.put("score", box.score);
            items.put(item);
        }
        return objectOf("items", items);
    }

    /** 读取图片时优先请求 ARGB_8888，确保模型预处理不会受到 RGB_565 精度损失。 */
    private static Bitmap decodeImage(String path) {
        String normalized = path.startsWith("file://") ? path.substring("file://".length()) : path;
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inPreferredConfig = Bitmap.Config.ARGB_8888;
        return BitmapFactory.decodeFile(normalized, options);
    }

    /**
     * 从参数中取出模型对象。模型加载与识别均在 MODEL_LOCK 下执行，因此 release 不会在推理
     * 中途关闭 session。
     */
    private static OcrModel requireModel(JSONObject arguments) {
        return MODELS_BY_NAME.get(required(arguments, "name"));
    }

    /** options 可省略，统一返回空对象简化下游默认值读取。 */
    private static JSONObject options(JSONObject arguments) {
        JSONObject options = arguments.optJSONObject("options");
        return options == null ? new JSONObject() : options;
    }

    /** 检查并规范化模型文件路径。 */
    private static String canonicalFile(String path, String label) {
        try {
            File file = new File(path.startsWith("file://") ? path.substring("file://".length()) : path)
                    .getCanonicalFile();
            return file.isFile() && file.canRead() ? file.getPath() : "";
        } catch (IOException exception) {
            return "";
        }
    }

    /** 创建 JSON 成功信封。 */
    private static String success(JSONObject data) {
        try {
            JSONObject root = new JSONObject();
            root.put("ok", true);
            root.put("data", data == null ? new JSONObject() : data);
            return root.toString();
        } catch (JSONException exception) {
            return "{\"ok\":false,\"error\":\"OCR 返回结果编码失败\"}";
        }
    }

    /** 创建 JSON 失败信封。 */
    private static String failure(String message) {
        try {
            JSONObject root = new JSONObject();
            root.put("ok", false);
            root.put("error", safe(message));
            return root.toString();
        } catch (JSONException exception) {
            return "{\"ok\":false,\"error\":\"OCR 调用失败\"}";
        }
    }

    /** 以键值对快速创建 JSON 对象，内部调用只传基础类型或 JSONArray。 */
    private static JSONObject objectOf(Object... values) {
        JSONObject result = new JSONObject();
        try {
            for (int index = 0; index + 1 < values.length; index += 2) {
                result.put(String.valueOf(values[index]), values[index + 1]);
            }
            return result;
        } catch (JSONException exception) {
            // 键和值都来自固定内部代码。这里失败代表平台 JSON 实现异常，由 call() 统一
            // 转成脚本可读的错误，不让受检异常污染所有模型管理分支。
            throw new IllegalStateException("OCR 返回结果编码失败", exception);
        }
    }

    private static int roundDown32(int value) {
        return Math.max(32, (value / 32) * 32);
    }

    private static int clamp(int value, int minimum, int maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }

    private static float clampFloat(float value, float minimum, float maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }

    /**
     * 返回指定类别的真实概率。
     *
     * 不同 ONNX 导出版本可能输出已归一化概率或原始 logits。概率行直接读取，logits 行才做
     * softmax，避免把 PP-OCRv4 已接近 1 的置信度再次压缩到约 1 / 类别数。
     */
    private static float classProbability(float[] values, int offset, int classes, int targetIndex) {
        float maximum = values[offset];
        for (int index = 1; index < classes; ++index) {
            maximum = Math.max(maximum, values[offset + index]);
        }
        float maximumProbability = maximumClassProbability(values, offset, classes, maximum);
        if (targetIndex < 0 || targetIndex >= classes || maximum == values[offset + targetIndex]) {
            return maximumProbability;
        }

        // 该分支只用于方向分类等非最大类别查询。先判断整行是否已经归一化；否则以同一最大值
        // 执行稳定 softmax，避免大 logits 指数溢出。
        double probabilitySum = 0.0;
        boolean normalized = true;
        for (int index = 0; index < classes; ++index) {
            float value = values[offset + index];
            normalized &= Float.isFinite(value) && value >= 0.0f && value <= 1.0f;
            probabilitySum += value;
        }
        if (normalized && Math.abs(probabilitySum - 1.0) <= 0.01) {
            return clampFloat(values[offset + targetIndex], 0.0f, 1.0f);
        }
        double exponentTotal = 0.0;
        for (int index = 0; index < classes; ++index) {
            exponentTotal += Math.exp(values[offset + index] - maximum);
        }
        return exponentTotal <= 0.0
                ? 0.0f
                : (float) (Math.exp(values[offset + targetIndex] - maximum) / exponentTotal);
    }

    /**
     * 返回已知最大类别的概率。
     *
     * CTC 解码已经扫描过一次类别并得到 maximum，复用它避免额外寻找最大值。整行位于
     * `[0, 1]` 且总和接近 1 时，模型已经完成 softmax，直接返回 maximum；否则按 logits
     * 计算稳定 softmax。blank 或连续重复字符不会调用本函数。
     */
    private static float maximumClassProbability(
            float[] values,
            int offset,
            int classes,
            float maximum
    ) {
        double probabilitySum = 0.0;
        boolean normalized = true;
        for (int index = 0; index < classes; ++index) {
            float value = values[offset + index];
            normalized &= Float.isFinite(value) && value >= 0.0f && value <= 1.0f;
            probabilitySum += value;
        }
        if (normalized && Math.abs(probabilitySum - 1.0) <= 0.01) {
            return clampFloat(maximum, 0.0f, 1.0f);
        }
        double exponentTotal = 0.0;
        for (int index = 0; index < classes; ++index) {
            exponentTotal += Math.exp(values[offset + index] - maximum);
        }
        return exponentTotal <= 0.0 ? 0.0f : (float) (1.0 / exponentTotal);
    }

    /**
     * 根据模型输出类别数推断 CTC 字典布局。
     *
     * RapidOCR 的历史模型存在三种常见导出：
     * - 类别数等于 keys 行数：类别 0 为 CTC blank，其余类别对应字典前 n-1 行；
     * - 多 1 个类别：类别 0 为 CTC blank；
     * - 多 2 个类别：类别 0 为 blank，末尾为半角空格。
     */
    private static CtcLayout ctcLayout(List<String> keys, int classes) {
        if (classes == keys.size()) {
            return new CtcLayout(0, 1, -1);
        }
        if (classes == keys.size() + 1) {
            return new CtcLayout(0, 1, -1);
        }
        if (classes == keys.size() + 2) {
            return new CtcLayout(0, 1, classes - 1);
        }
        return null;
    }

    private static String required(JSONObject object, String key) {
        return object.optString(key, "").trim();
    }

    private static String safe(String value) {
        return value == null ? "" : value;
    }

    private static String safeMessage(Exception exception) {
        String message = exception.getMessage();
        return message == null || message.isEmpty() ? exception.getClass().getSimpleName() : message;
    }

    private static void recycle(Bitmap bitmap) {
        if (bitmap != null && !bitmap.isRecycled()) {
            bitmap.recycle();
        }
    }

    private static void closeQuietly(OrtSession session) {
        if (session != null) {
            try {
                session.close();
            } catch (Exception ignored) {
                // 释放失败时无可恢复资源；后续模型加载仍应继续。
            }
        }
    }

    /** ONNX NCHW 输入约束；fixedWidth/fixedHeight 为 0 时表示对应维度可动态变化。 */
    private static final class ModelInput {
        final String name;
        final int fixedHeight;
        final int fixedWidth;

        ModelInput(String name, int fixedHeight, int fixedWidth) {
            this.name = name;
            this.fixedHeight = fixedHeight;
            this.fixedWidth = fixedWidth;
        }
    }

    /** 单次 ONNX 推理的紧凑 float 数据和真实输出 shape。 */
    private static final class TensorOutput {
        final float[] values;
        final long[] shape;

        TensorOutput(float[] values, long[] shape) {
            this.values = values;
            this.shape = shape == null ? new long[0] : shape;
        }

        int lastDimension() {
            if (shape.length == 0 || shape[shape.length - 1] <= 0L || shape[shape.length - 1] > Integer.MAX_VALUE) {
                throw new IllegalStateException("ONNX 输出类别维度无效");
            }
            return (int) shape[shape.length - 1];
        }
    }

    /** CTC 空白类别、字典偏移和可选尾部空格类别。 */
    private static final class CtcLayout {
        final int blankIndex;
        final int dictionaryOffset;
        final int trailingSpaceIndex;

        CtcLayout(int blankIndex, int dictionaryOffset, int trailingSpaceIndex) {
            this.blankIndex = blankIndex;
            this.dictionaryOffset = dictionaryOffset;
            this.trailingSpaceIndex = trailingSpaceIndex;
        }

        String characterAt(int classIndex, List<String> keys) {
            if (classIndex == trailingSpaceIndex) {
                return " ";
            }
            int keyIndex = classIndex - dictionaryOffset;
            return keyIndex >= 0 && keyIndex < keys.size() ? keys.get(keyIndex) : null;
        }
    }

    /** 已加载模型的共享状态。 */
    private static final class OcrModel {
        final String fingerprint;
        final OrtSession detSession;
        final OrtSession recSession;
        final OrtSession clsSession;
        final ModelInput detInput;
        final ModelInput recInput;
        final ModelInput clsInput;
        final List<String> keys;
        int referenceCount = 1;

        OcrModel(
                String fingerprint,
                OrtSession detSession,
                OrtSession recSession,
                OrtSession clsSession,
                ModelInput detInput,
                ModelInput recInput,
                ModelInput clsInput,
                List<String> keys
        ) {
            this.fingerprint = fingerprint;
            this.detSession = detSession;
            this.recSession = recSession;
            this.clsSession = clsSession;
            this.detInput = detInput;
            this.recInput = recInput;
            this.clsInput = clsInput;
            this.keys = keys;
        }

        void close() {
            closeQuietly(detSession);
            closeQuietly(recSession);
            closeQuietly(clsSession);
        }
    }

    /** DBNet 缩放结果。 */
    private static final class ResizeInfo {
        final int width;
        final int height;
        final float scaleX;
        final float scaleY;

        ResizeInfo(int width, int height, float scaleX, float scaleY) {
            this.width = width;
            this.height = height;
            this.scaleX = scaleX;
            this.scaleY = scaleY;
        }
    }

    /** DBNet 概率图视图。 */
    private static final class ScoreMap {
        final float[] values;
        final int width;
        final int height;
        final int offset;

        ScoreMap(float[] values, int width, int height, int offset) {
            this.values = values;
            this.width = width;
            this.height = height;
            this.offset = offset;
        }

        float valueAt(int index) {
            return values[offset + index];
        }
    }

    /** 概率图连通区域。 */
    private static final class Component {
        final int left;
        final int top;
        final int right;
        final int bottom;
        final float score;

        Component(int left, int top, int right, int bottom, float score) {
            this.left = left;
            this.top = top;
            this.right = right;
            this.bottom = bottom;
            this.score = score;
        }
    }

    /** OCR 文本框。 */
    private static final class OcrBox {
        final int x;
        final int y;
        final int width;
        final int height;
        final float detScore;
        String text = "";
        float score = 0.0f;

        OcrBox(int x, int y, int width, int height, float detScore) {
            this.x = x;
            this.y = y;
            this.width = width;
            this.height = height;
            this.detScore = detScore;
        }
    }

    /** 单个文本框的 CTC 解码结果。 */
    private static final class Recognition {
        final String text;
        final float score;

        Recognition(String text, float score) {
            this.text = text;
            this.score = score;
        }
    }
}
