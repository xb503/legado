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
    if (g_net) {
        delete g_net;
        g_net = nullptr;
    }
    g_net = new ncnn::Net();
    g_net->opt.use_vulkan_compute = false;
    g_net->opt.num_threads = 4;
    g_net->opt.use_packing_layout = true;
    g_net->opt.use_fp16_arithmetic = false;

    AAssetManager* mgr = AAssetManager_fromJava(env, assetManager);
    if (!mgr) {
        LOGE("AAssetManager_fromJava failed");
        delete g_net;
        g_net = nullptr;
        return JNI_FALSE;
    }

    const char* paramPath = env->GetStringUTFChars(modelParam, nullptr);
    const char* binPath = env->GetStringUTFChars(modelBin, nullptr);

    if (g_net->load_param(mgr, paramPath) != 0) {
        LOGE("load_param failed: %s", paramPath);
        env->ReleaseStringUTFChars(modelParam, paramPath);
        env->ReleaseStringUTFChars(modelBin, binPath);
        delete g_net;
        g_net = nullptr;
        return JNI_FALSE;
    }
    if (g_net->load_model(mgr, binPath) != 0) {
        LOGE("load_model failed: %s", binPath);
        env->ReleaseStringUTFChars(modelParam, paramPath);
        env->ReleaseStringUTFChars(modelBin, binPath);
        delete g_net;
        g_net = nullptr;
        return JNI_FALSE;
    }

    env->ReleaseStringUTFChars(modelParam, paramPath);
    env->ReleaseStringUTFChars(modelBin, binPath);
    LOGI("Real-CUGAN model loaded, scale=%d", g_scale);
    return JNI_TRUE;
}

static ncnn::Mat run_inference(const ncnn::Mat& input) {
    ncnn::Mat output;
    ncnn::Extractor ex = g_net->create_extractor();
    ex.set_light_mode(true);
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

    ncnn::Mat outMat;
    int outW = width * scale;
    int outH = height * scale;

    if (width <= TILE_SIZE && height <= TILE_SIZE) {
        outMat = run_inference(inMat);
    } else {
        outMat = ncnn::Mat(outW, outH, 3);
        outMat.fill(0.0f);
        float* outR = (float*)outMat.channel(0).data;
        float* outG = (float*)outMat.channel(1).data;
        float* outB = (float*)outMat.channel(2).data;

        int tilesX = (width + TILE_SIZE - 1) / TILE_SIZE;
        int tilesY = (height + TILE_SIZE - 1) / TILE_SIZE;

        for (int ty = 0; ty < tilesY; ty++) {
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
                if (tileOut.empty()) {
                    LOGE("tile inference failed at (%d,%d)", tx, ty);
                    continue;
                }

                int sx0 = (x0 - px0) * scale;
                int sy0 = (y0 - py0) * scale;
                int sw = (x1 - x0) * scale;
                int sh = (y1 - y0) * scale;

                float* oR = (float*)tileOut.channel(0).data;
                float* oG = (float*)tileOut.channel(1).data;
                float* oB = (float*)tileOut.channel(2).data;

                int outX = x0 * scale;
                int outY = y0 * scale;
                for (int y = 0; y < sh; y++) {
                    memcpy(outR + (outY + y) * outW + outX,
                           oR + (sy0 + y) * tileOut.w + sx0, sw * sizeof(float));
                    memcpy(outG + (outY + y) * outW + outX,
                           oG + (sy0 + y) * tileOut.w + sx0, sw * sizeof(float));
                    memcpy(outB + (outY + y) * outW + outX,
                           oB + (sy0 + y) * tileOut.w + sx0, sw * sizeof(float));
                }
            }
        }
    }

    if (outMat.empty()) {
        return nullptr;
    }

    jintArray result = env->NewIntArray(outW * outH);
    if (!result) {
        return nullptr;
    }
    jint* dst = env->GetIntArrayElements(result, nullptr);
    if (!dst) {
        return nullptr;
    }

    float* oR = (float*)outMat.channel(0).data;
    float* oG = (float*)outMat.channel(1).data;
    float* oB = (float*)outMat.channel(2).data;
    int outTotal = outW * outH;
    for (int i = 0; i < outTotal; i++) {
        int r = (int)(oR[i] * 255.0f + 0.5f);
        int g = (int)(oG[i] * 255.0f + 0.5f);
        int b = (int)(oB[i] * 255.0f + 0.5f);
        r = r < 0 ? 0 : (r > 255 ? 255 : r);
        g = g < 0 ? 0 : (g > 255 ? 255 : g);
        b = b < 0 ? 0 : (b > 255 ? 255 : b);
        dst[i] = 0xff000000 | (r << 16) | (g << 8) | b;
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
