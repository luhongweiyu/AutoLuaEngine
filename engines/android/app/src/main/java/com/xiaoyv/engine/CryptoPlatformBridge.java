/**
 * 文件用途：实现脚本 cryptLib 所需的 AES、RSA 与安全随机数平台能力。
 */
package com.xiaoyv.engine;

import android.util.Base64;

import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.SecureRandom;
import java.security.interfaces.RSAPrivateKey;
import java.security.interfaces.RSAPublicKey;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.X509EncodedKeySpec;
import java.util.Locale;

import javax.crypto.Cipher;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/**
 * 二进制参数在 JSON 边界统一使用无换行 Base64；Lua 兼容层负责恢复成二进制字符串。
 */
final class CryptoPlatformBridge {
    private static final SecureRandom SECURE_RANDOM = new SecureRandom();
    private static final int AES_BLOCK_BYTES = 16;

    private CryptoPlatformBridge() {
    }

    static Object call(String operation, JSONObject arguments) throws Exception {
        switch (operation) {
            case "crypto.aes":
                return aes(arguments);
            case "crypto.aesKeygen":
                return randomBytes(arguments.getInt("length"));
            case "crypto.aesIvgen":
                return randomBytes(AES_BLOCK_BYTES);
            case "crypto.rsaGenerate":
                return rsaGenerate(arguments.optInt("bits", 2048));
            case "crypto.rsaEncrypt":
                return rsaTransform(arguments, Cipher.ENCRYPT_MODE);
            case "crypto.rsaDecrypt":
                return rsaTransform(arguments, Cipher.DECRYPT_MODE);
            default:
                throw new IllegalArgumentException("不支持的加密能力：" + operation);
        }
    }

    private static String aes(JSONObject arguments) throws Exception {
        byte[] data = decode(arguments.getString("data"));
        byte[] key = decode(arguments.getString("key"));
        if (key.length != 16 && key.length != 24 && key.length != 32) {
            throw new IllegalArgumentException("AES 密钥长度必须是 16、24 或 32 字节");
        }

        String mode = arguments.optString("mode", "cbc").toUpperCase(Locale.ROOT);
        if (!mode.equals("ECB") && !mode.equals("CBC") && !mode.equals("CFB")
                && !mode.equals("OFB") && !mode.equals("CTR")) {
            throw new IllegalArgumentException("不支持的 AES 模式：" + mode);
        }
        boolean encrypt = "encrypt".equalsIgnoreCase(arguments.optString("operation"));
        boolean decrypt = "decrypt".equalsIgnoreCase(arguments.optString("operation"));
        if (!encrypt && !decrypt) {
            throw new IllegalArgumentException("AES operation 必须为 encrypt 或 decrypt");
        }

        boolean padding = arguments.optBoolean("padding", true);
        if (encrypt && padding) {
            data = addPkcs7Padding(data);
        } else if (decrypt && padding && data.length % AES_BLOCK_BYTES != 0) {
            throw new IllegalArgumentException("带填充的 AES 密文长度必须是 16 的倍数");
        }

        Cipher cipher = Cipher.getInstance("AES/" + mode + "/NoPadding");
        SecretKeySpec secretKey = new SecretKeySpec(key, "AES");
        if ("ECB".equals(mode)) {
            cipher.init(encrypt ? Cipher.ENCRYPT_MODE : Cipher.DECRYPT_MODE, secretKey);
        } else {
            byte[] iv = decode(arguments.optString("iv", ""));
            if (iv.length != AES_BLOCK_BYTES) {
                throw new IllegalArgumentException("当前 AES 模式的 IV 必须是 16 字节");
            }
            cipher.init(
                    encrypt ? Cipher.ENCRYPT_MODE : Cipher.DECRYPT_MODE,
                    secretKey,
                    new IvParameterSpec(iv)
            );
        }

        byte[] output = cipher.doFinal(data);
        if (decrypt && padding) {
            output = removePkcs7Padding(output);
        }
        return encode(output);
    }

    private static String randomBytes(int length) {
        if (length != 16 && length != 24 && length != 32) {
            throw new IllegalArgumentException("AES 密钥长度必须是 16、24 或 32 字节");
        }
        byte[] value = new byte[length];
        SECURE_RANDOM.nextBytes(value);
        return encode(value);
    }

    private static JSONObject rsaGenerate(int bits) throws Exception {
        if (bits < 1024 || bits > 4096 || bits % 256 != 0) {
            throw new IllegalArgumentException("RSA 位数必须是 1024 到 4096 之间的 256 倍数");
        }
        KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA");
        generator.initialize(bits, SECURE_RANDOM);
        KeyPair pair = generator.generateKeyPair();

        JSONObject result = new JSONObject();
        result.put("publicKey", toPem("PUBLIC KEY", pair.getPublic().getEncoded()));
        result.put("privateKey", toPem("PRIVATE KEY", pair.getPrivate().getEncoded()));
        return result;
    }

    private static String rsaTransform(JSONObject arguments, int cipherMode) throws Exception {
        byte[] data = decode(arguments.getString("data"));
        String pem = arguments.getString("key");
        boolean publicKey = arguments.optBoolean("publicKey", true);
        KeyFactory factory = KeyFactory.getInstance("RSA");

        Cipher cipher = Cipher.getInstance("RSA/ECB/PKCS1Padding");
        if (publicKey) {
            RSAPublicKey key = (RSAPublicKey) factory.generatePublic(
                    new X509EncodedKeySpec(parsePem(pem))
            );
            cipher.init(cipherMode, key);
        } else {
            RSAPrivateKey key = (RSAPrivateKey) factory.generatePrivate(
                    new PKCS8EncodedKeySpec(parsePem(pem))
            );
            cipher.init(cipherMode, key);
        }
        return encode(cipher.doFinal(data));
    }

    private static byte[] addPkcs7Padding(byte[] input) {
        int count = AES_BLOCK_BYTES - input.length % AES_BLOCK_BYTES;
        byte[] output = new byte[input.length + count];
        System.arraycopy(input, 0, output, 0, input.length);
        for (int index = input.length; index < output.length; index++) {
            output[index] = (byte) count;
        }
        return output;
    }

    private static byte[] removePkcs7Padding(byte[] input) {
        if (input.length == 0 || input.length % AES_BLOCK_BYTES != 0) {
            throw new IllegalArgumentException("AES 填充数据无效");
        }
        int count = input[input.length - 1] & 0xff;
        if (count < 1 || count > AES_BLOCK_BYTES || count > input.length) {
            throw new IllegalArgumentException("AES 填充数据无效");
        }
        for (int index = input.length - count; index < input.length; index++) {
            if ((input[index] & 0xff) != count) {
                throw new IllegalArgumentException("AES 填充数据无效");
            }
        }
        byte[] output = new byte[input.length - count];
        System.arraycopy(input, 0, output, 0, output.length);
        return output;
    }

    private static String toPem(String type, byte[] encoded) {
        String body = Base64.encodeToString(encoded, Base64.NO_WRAP);
        StringBuilder pem = new StringBuilder();
        pem.append("-----BEGIN ").append(type).append("-----\n");
        for (int offset = 0; offset < body.length(); offset += 64) {
            pem.append(body, offset, Math.min(offset + 64, body.length())).append('\n');
        }
        return pem.append("-----END ").append(type).append("-----\n").toString();
    }

    private static byte[] parsePem(String pem) {
        String body = pem
                .replaceAll("-----BEGIN [^-]+-----", "")
                .replaceAll("-----END [^-]+-----", "")
                .replaceAll("\\s", "");
        try {
            return Base64.decode(body.getBytes(StandardCharsets.US_ASCII), Base64.DEFAULT);
        } catch (IllegalArgumentException exception) {
            throw new IllegalArgumentException("RSA PEM 密钥无效");
        }
    }

    private static String encode(byte[] value) {
        return Base64.encodeToString(value, Base64.NO_WRAP);
    }

    private static byte[] decode(String value) {
        try {
            return Base64.decode(value, Base64.DEFAULT);
        } catch (IllegalArgumentException exception) {
            throw new IllegalArgumentException("二进制参数 Base64 无效");
        }
    }
}
