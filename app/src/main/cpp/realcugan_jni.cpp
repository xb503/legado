#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <cstring>
#include <algorithm>
#include "ncnn/net.h"
#include "ncnn/mat.h"

#define LOG_TAG "RealCuganNcnn"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static ncnn::Net* g_net = nullptr;
static const int g_scale = 2;
static const int TILE_SIZE = 256;
static const int TILE_PAD = 32;

extern "C" {

JNIEXPORT jboolean JNICALL
Java_io_legado_app_manga_RealCuganNcnn_init(JNIEnv* env, jobject /*thiz*/, jobject assetManager, jstring modelParam, jstring modelBin) {
    // 先在局部指针上完成全部加载，成功后再发布到 g_net，
    // 避免加载期间其他线程的 upscale 访问半成品 net
    ncnn::Net* net = new ncnn::Net();
    net->opt.use_vulkan_compute = false;
    net->opt.num_threads = 4;
    // 官方 CPU 配置：关闭打包布局与 fp16 存储，保证输出为平面 fp32 Mat，
    // 否则 extract 返回的 Mat 为打包/半精度布局，按 float* 平面访问会越界崩溃
    net->opt.use_packing_layout = false;
    net->opt.use_fp16_packed = false;
    net->opt.use_fp16_storage = false;
    net->opt.use_fp16_arithmetic = false;
    net->opt.use_int8_storage = false;

    AAssetManager* mgr = AAssetManager_fromJava(env, assetManager);
    if (!mgr) {
        LOGE("AAssetManager_fromJava failed");
        delete net;
        return JNI_FALSE;
    }

    const char* paramPath = env->GetStringUTFChars(modelParam, nullptr);
    const char* binPath = env->GetStringUTFChars(modelBin, nullptr);

    if (net->load_param(mgr, paramPath) != 0) {
        LOGE("load_param failed: %s", paramPath);
        env->ReleaseStringUTFChars(modelParam, paramPath);
        env->ReleaseStringUTFChars(modelBin, binPath);
        delete net;
        return JNI_FALSE;
    }
    if (net->load_model(mgr, binPath) != 0) {
        LOGE("load_model failed: %s", binPath);
        env->ReleaseStringUTFChars(modelParam, paramPath);
        env->ReleaseStringUTFChars(modelBin, binPath);
        delete net;
        return JNI_FALSE;
    }

    env->ReleaseStringUTFChars(modelParam, paramPath);
    env->ReleaseStringUTFChars(modelBin, binPath);

    if (g_net) {
        delete g_net;
    }
    g_net = net;
    LOGI("Real-CUGAN model loaded, scale=%d", g_scale);
    return JNI_TRUE;
}

static bool is_planar_rgb_fp32(const ncnn::Mat& m, int expectW, int expectH) {
    return !m.empty() && m.dims == 3 && m.c == 3
        && m.w == expectW && m.h == expectH
        && m.elempack == 1 && m.elemsize == (int)sizeof(float);
}

static ncnn::Mat run_inference(const ncnn::Mat& input) {
    ncnn::Mat output;
    ncnn::Extractor ex = g_net->create_extractor();
    // Real-CUGAN 含 Split 分支层，必须关闭 light mode（与官方一致）
    ex.set_light_mode(false);
    ex.input("in0", input);
    if (ex.extract("out0", output) != 0) {
        LOGE("extract out0 failed");
        return ncnn::Mat();
    }
    return output;
}

JNIEXPORT jintArray JNICALL
Java_io_legado_app_manga_RealCuganNcnn_upscale(JNIEnv* env, jobject /*thiz*/, jintArray pixels, jint width, jint height) {
    if (!g_net || width <= 0 || height <= 0) {
        return nullptr;
    }

    jint* srcPixels = env->GetIntArrayElements(pixels, nullptr);
    if (!srcPixels) {
        return nullptr;
    }

    const int total = width * height;
    const int scale = g_scale;

    ncnn::Mat inMat(width, height, 3);
    float* inR = (float*)inMat.channel(0).data;
    float* inG = (float*)inMat.channel(1).data;
    float* inB = (float*)inMat.channel(2).data;
    for (int i = 0; i < total; i++) {
        uint32_t p = (uint32_t)srcPixels[i];
        inR[i] = ((p >> 16) & 0xff) / 255.0f;
        inG[i] = ((p >> 8) & 0xff) / 255.0f;
        inB[i] = (p & 0xff) / 255.0f;
    }
    env->ReleaseIntArrayElements(pixels, srcPixels, JNI_ABORT);

    int outW = width * scale;
    int outH = height * scale;
    const int outTotal = outW * outH;

    // 直接分配最终像素数组，分块推理结果即时写入，避免整幅 fp32 输出占用过大内存
    jintArray result = env->NewIntArray(outTotal);
    if (!result) {
        return nullptr;
    }
    jint* dst = env->GetIntArrayElements(result, nullptr);
    if (!dst) {
        return nullptr;
    }

    // 将平面 fp32 RGB Mat 的指定矩形转成 ARGB 写入 dst
    auto writeRegion = [&](const ncnn::Mat& m, int sx0, int sy0, int sw, int sh,
                           int outX, int outY) {
        const float* mR = (const float*)m.channel(0).data;
        const float* mG = (const float*)m.channel(1).data;
        const float* mB = (const float*)m.channel(2).data;
        for (int y = 0; y < sh; y++) {
            const int srcRow = (sy0 + y) * m.w + sx0;
            jint* drow = dst + (long)(outY + y) * outW + outX;
            for (int x = 0; x < sw; x++) {
                int si = srcRow + x;
                int r = (int)(mR[si] * 255.0f + 0.5f);
                int g = (int)(mG[si] * 255.0f + 0.5f);
                int b = (int)(mB[si] * 255.0f + 0.5f);
                r = r < 0 ? 0 : (r > 255 ? 255 : r);
                g = g < 0 ? 0 : (g > 255 ? 255 : g);
                b = b < 0 ? 0 : (b > 255 ? 255 : b);
                drow[x] = 0xff000000 | (r << 16) | (g << 8) | b;
            }
        }
    };

    bool ok = true;

    if (width <= TILE_SIZE && height <= TILE_SIZE) {
        ncnn::Mat outMat = run_inference(inMat);
        if (!is_planar_rgb_fp32(outMat, outW, outH)) {
            LOGE("unexpected output layout: dims=%d c=%d w=%d h=%d pack=%d es=%zu",
                 outMat.dims, outMat.c, outMat.w, outMat.h, outMat.elempack, outMat.elemsize);
            ok = false;
        } else {
            writeRegion(outMat, 0, 0, outW, outH, 0, 0);
        }
    } else {
        int tilesX = (width + TILE_SIZE - 1) / TILE_SIZE;
        int tilesY = (height + TILE_SIZE - 1) / TILE_SIZE;

        for (int ty = 0; ty < tilesY && ok; ty++) {
            for (int tx = 0; tx < tilesX; tx++) {
                int x0 = tx * TILE_SIZE;
                int y0 = ty * TILE_SIZE;
                int x1 = std::min(x0 + TILE_SIZE, width);
                int y1 = std::min(y0 + TILE_SIZE, height);

                int px0 = std::max(0, x0 - TILE_PAD);
                int py0 = std::max(0, y0 - TILE_PAD);
                int px1 = std::min(width, x1 + TILE_PAD);
                int py1 = std::min(height, y1 + TILE_PAD);
                int pw = px1 - px0;
                int ph = py1 - py0;

                ncnn::Mat tileIn(pw, ph, 3);
                float* tR = (float*)tileIn.channel(0).data;
                float* tG = (float*)tileIn.channel(1).data;
                float* tB = (float*)tileIn.channel(2).data;
                for (int y = 0; y < ph; y++) {
                    const float* sR = inR + (py0 + y) * width + px0;
                    const float* sG = inG + (py0 + y) * width + px0;
                    const float* sB = inB + (py0 + y) * width + px0;
                    memcpy(tR + y * pw, sR, pw * sizeof(float));
                    memcpy(tG + y * pw, sG, pw * sizeof(float));
                    memcpy(tB + y * pw, sB, pw * sizeof(float));
                }

                ncnn::Mat tileOut = run_inference(tileIn);
                if (!is_planar_rgb_fp32(tileOut, pw * scale, ph * scale)) {
                    LOGE("tile output unexpected at (%d,%d): dims=%d c=%d w=%d h=%d pack=%d es=%zu",
                         tx, ty, tileOut.dims, tileOut.c, tileOut.w, tileOut.h,
                         tileOut.elempack, tileOut.elemsize);
                    ok = false;
                    break;
                }

                int sx0 = (x0 - px0) * scale;
                int sy0 = (y0 - py0) * scale;
                int sw = (x1 - x0) * scale;
                int sh = (y1 - y0) * scale;

                writeRegion(tileOut, sx0, sy0, sw, sh, x0 * scale, y0 * scale);
            }
        }
    }

    if (!ok) {
        // 任一块失败则整体放弃，交回 Kotlin 层回退原图，避免出现局部黑块
        env->ReleaseIntArrayElements(result, dst, JNI_ABORT);
        return nullptr;
    }

    env->ReleaseIntArrayElements(result, dst, 0);
    return result;
}

JNIEXPORT void JNICALL
Java_io_legado_app_manga_RealCuganNcnn_release(JNIEnv* env, jobject /*thiz*/) {
    if (g_net) {
        delete g_net;
        g_net = nullptr;
    }
}

} // extern "C"
