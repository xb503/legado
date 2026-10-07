#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <cstring>
#include <cmath>
#include <mutex>
#include <string>
#include <algorithm>
#include <chrono>
#include "ncnn/net.h"
#include "ncnn/mat.h"
#include "ncnn/gpu.h"

#define LOG_TAG "MangaEnhanceNcnn"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 放大模式
#define MODE_LANCZOS 0
#define MODE_REALCUGAN 1
#define MODE_ANIME6B 2

static const int SCALE = 2;
static const int TILE_SIZE = 256;   // CPU 分块大小（源像素）
static const int GPU_TILE_SIZE = 512; // Vulkan 分块大小：大块显著减少 tile 数与提交次数
static const int TILE_PAD = 16;     // 分块重叠边（源像素），消除拼接接缝
static const int BLEND_WIDTH = TILE_PAD * SCALE; // 输出侧羽化混合宽度

// 同一时刻只保留一个模型的全部上下文
struct ModelContext {
    ncnn::Net* net = nullptr;
    int mode = MODE_LANCZOS;
    bool vulkanAvailable = false; // 设备是否支持 Vulkan
    bool usingVulkan = false;     // 当前网络是否实际运行在 Vulkan 上
    bool vulkanFailed = false;    // Vulkan 推理曾失败 -> 永久降级 CPU
    std::string inputBlob = "data";
    std::string outputBlob = "output";
    std::string paramPath;
    std::string binPath;
    jobject assetManagerRef = nullptr; // GlobalRef，保证重建网络时仍可读取 assets
    AAssetManager* assetMgr = nullptr; // 缓存 native 指针，降级重建时无需再调 JNI
};

static ModelContext g_ctx;
static std::mutex g_lock;

static bool is_planar_rgb_fp32(const ncnn::Mat& m, int expectW, int expectH);
static bool load_net_assets(ncnn::Net* net, JNIEnv* env);
static void destroy_net();

// ---------------- 模型网络创建 / 销毁 ----------------

static ncnn::Net* create_net(bool useVulkan) {
    ncnn::Net* net = new ncnn::Net();
    net->opt.use_vulkan_compute = useVulkan;
    net->opt.num_threads = 4;
    if (useVulkan) {
        // 关闭打包布局与 fp16 存储：保证从 GPU 下载到 CPU 的输出 Mat
        // 始终为平面 fp32（否则 c=3 时输出可能是 fp16/打包格式，无法直接按
        // planar float* 读取）。分块尺寸固定 256，fp32 显存开销可控。
        net->opt.use_vulkan_compute = true;
        net->opt.use_packing_layout = false;
        net->opt.use_fp16_packed = false;
        net->opt.use_fp16_storage = false;
        net->opt.use_fp16_arithmetic = false;
        net->set_vulkan_device(ncnn::get_gpu_device(0));
    } else {
        // CPU：关闭打包布局与 fp16 存储，保证输出为平面 fp32 Mat
        net->opt.use_packing_layout = false;
        net->opt.use_fp16_packed = false;
        net->opt.use_fp16_storage = false;
        net->opt.use_fp16_arithmetic = false;
        net->opt.use_int8_storage = false;
    }
    return net;
}

static bool load_net_assets(ncnn::Net* net, JNIEnv* env) {
    if (!g_ctx.assetMgr) {
        g_ctx.assetMgr = AAssetManager_fromJava(env, g_ctx.assetManagerRef);
    }
    AAssetManager* mgr = g_ctx.assetMgr;
    if (!mgr) {
        LOGE("AAssetManager_fromJava failed");
        return false;
    }
    if (net->load_param(mgr, g_ctx.paramPath.c_str()) != 0) {
        LOGE("load_param failed: %s", g_ctx.paramPath.c_str());
        return false;
    }
    if (net->load_model(mgr, g_ctx.binPath.c_str()) != 0) {
        LOGE("load_model failed: %s", g_ctx.binPath.c_str());
        return false;
    }
    return true;
}

static void destroy_net() {
    if (g_ctx.net) {
        delete g_ctx.net;
        g_ctx.net = nullptr;
    }
    g_ctx.usingVulkan = false;
}

// ---------------- 推理（含 Vulkan 失败自动降级 CPU 重试） ----------------

static bool extract_once(ncnn::Net* net, const ncnn::Mat& input, ncnn::Mat& output) {
    ncnn::Extractor ex = net->create_extractor();
    // 两类模型均含 Split 分支，必须关闭 light mode
    ex.set_light_mode(false);
    ex.input(g_ctx.inputBlob.c_str(), input);
    return ex.extract(g_ctx.outputBlob.c_str(), output) == 0;
}

// 调用者必须持有 g_lock
static bool extract_into_planar_locked(const ncnn::Mat& input, ncnn::Mat& output,
                                       int expectW, int expectH, JNIEnv* env) {
    auto attempt = [&](bool* extractOk) -> bool {
        ncnn::Mat out;
        *extractOk = extract_once(g_ctx.net, input, out);
        if (!*extractOk) return false;
        if (!is_planar_rgb_fp32(out, expectW, expectH)) {
            LOGE("bad output layout: dims=%d c=%d w=%d h=%d ep=%d es=%d expect=%dx%d",
                 out.dims, out.c, out.w, out.h, out.elempack, out.elemsize,
                 expectW, expectH);
            return false;
        }
        output = out;
        return true;
    };

    bool extractOk = false;
    if (attempt(&extractOk)) {
        return true;
    }

    // 推理失败或输出不是平面 fp32：若当前在 Vulkan 上，销毁 GPU 网络并重建
    // 纯 CPU 网络后重试一次，之后永久走 CPU
    if (g_ctx.usingVulkan && !g_ctx.vulkanFailed) {
        LOGE("vulkan inference unusable (extract=%d), fallback to CPU", (int)extractOk);
        g_ctx.vulkanFailed = true;
        destroy_net();
        ncnn::Net* cpuNet = create_net(false);
        if (load_net_assets(cpuNet, env)) {
            g_ctx.net = cpuNet;
            g_ctx.usingVulkan = false;
            LOGI("fallback to CPU inference");
            if (attempt(&extractOk)) {
                return true;
            }
        } else {
            delete cpuNet;
        }
    }
    return false;
}

static bool is_planar_rgb_fp32(const ncnn::Mat& m, int expectW, int expectH) {
    return !m.empty() && m.dims == 3 && m.c == 3
        && m.w == expectW && m.h == expectH
        && m.elempack == 1 && m.elemsize == (int)sizeof(float);
}

static inline int clamp255(float v) {
    int i = (int)(v * 255.0f + 0.5f);
    return i < 0 ? 0 : (i > 255 ? 255 : i);
}

static inline uint32_t argb_from_rgb(int r, int g, int b) {
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// ---------------- Lanczos3 纯 CPU 插值（分条带，内存友好） ----------------

static inline float lanczos_weight(float x) {
    if (x == 0.0f) return 1.0f;
    if (x < 0.0f) x = -x;
    if (x >= 3.0f) return 0.0f;
    const float PI = 3.14159265358979323846f;
    float px = PI * x;
    return (3.0f * std::sin(px) * std::sin(px / 3.0f)) / (px * px);
}

static void upscale_lanczos_locked(const uint32_t* src, int width, int height,
                                   uint32_t* dst, int outW, int outH) {
    const float invScale = 1.0f / SCALE;
    const int BAND = 64; // 每次处理 64 个输出行
    for (int bandY = 0; bandY < outH; bandY += BAND) {
        int bandH = std::min(BAND, outH - bandY);
        for (int dy = bandY; dy < bandY + bandH; dy++) {
            // 输出像素中心映射回源图坐标
            float sy = (dy + 0.5f) * invScale - 0.5f;
            int iy0 = (int)std::floor(sy) - 2;
            int iy1 = (int)std::floor(sy) + 3;
            iy0 = std::max(0, iy0);
            iy1 = std::min(height - 1, iy1);

            uint32_t* drow = dst + (long)dy * outW;
            for (int dx = 0; dx < outW; dx++) {
                float sx = (dx + 0.5f) * invScale - 0.5f;
                int ix0 = (int)std::floor(sx) - 2;
                int ix1 = (int)std::floor(sx) + 3;
                ix0 = std::max(0, ix0);
                ix1 = std::min(width - 1, ix1);

                float wr[6];
                for (int i = 0; i < 6; i++) wr[i] = 0.0f;
                for (int ix = ix0; ix <= ix1; ix++) {
                    wr[ix - ix0] = lanczos_weight((float)ix - sx);
                }

                float r = 0, g = 0, b = 0, wsum = 0;
                for (int iy = iy0; iy <= iy1; iy++) {
                    float wy = lanczos_weight((float)iy - sy);
                    if (wy == 0.0f) continue;
                    const uint32_t* srow = src + (long)iy * width;
                    for (int ix = ix0; ix <= ix1; ix++) {
                        float w = wy * wr[ix - ix0];
                        uint32_t p = srow[ix];
                        r += w * ((p >> 16) & 0xff);
                        g += w * ((p >> 8) & 0xff);
                        b += w * (p & 0xff);
                        wsum += w;
                    }
                }
                if (wsum != 0.0f) { r /= wsum; g /= wsum; b /= wsum; }
                drow[dx] = argb_from_rgb(clamp255(r / 255.0f),
                                        clamp255(g / 255.0f),
                                        clamp255(b / 255.0f));
            }
        }
    }
}

// ---------------- AI 分块推理（带重叠 + 羽化混合） ----------------

// 按 tent 权重把 tile 输出混合进 dst（已有的输出像素参与 alpha 混合）
static void blend_tile(uint32_t* dst, int outW, int outH,
                       const ncnn::Mat& tileOut,
                       int sx0, int sy0, int sw, int sh,
                       int outX, int outY) {
    const float* mR = (const float*)tileOut.channel(0).data;
    const float* mG = (const float*)tileOut.channel(1).data;
    const float* mB = (const float*)tileOut.channel(2).data;
    const int W = tileOut.w;

    // 贴图像边界的方向没有相邻 tile，不做羽化
    const bool edgeL = (outX <= 0);
    const bool edgeT = (outY <= 0);
    const bool edgeR = (outX + sw >= outW);
    const bool edgeB = (outY + sh >= outH);

    for (int y = 0; y < sh; y++) {
        int oy = outY + y;
        if (oy < 0 || oy >= outH) continue;
        // 垂直方向 tent 权重（仅在有相邻 tile 的方向衰减）
        float wy0 = edgeT ? 1.0f
                          : (float)std::min(BLEND_WIDTH, y + 1) / BLEND_WIDTH;
        float wy1 = edgeB ? 1.0f
                          : (float)std::min(BLEND_WIDTH, sh - y) / BLEND_WIDTH;
        float ay = std::min(1.0f, std::min(wy0, wy1));

        for (int x = 0; x < sw; x++) {
            int ox = outX + x;
            if (ox < 0 || ox >= outW) continue;
            float wx0 = edgeL ? 1.0f
                              : (float)std::min(BLEND_WIDTH, x + 1) / BLEND_WIDTH;
            float wx1 = edgeR ? 1.0f
                              : (float)std::min(BLEND_WIDTH, sw - x) / BLEND_WIDTH;
            float alpha = ay * std::min(1.0f, std::min(wx0, wx1));

            int si = (sy0 + y) * W + sx0 + x;
            int nr = clamp255(mR[si]);
            int ng = clamp255(mG[si]);
            int nb = clamp255(mB[si]);

            uint32_t* dp = dst + (long)oy * outW + ox;
            uint32_t old = *dp;
            if (old == 0) {
                // 首次写入：直接写满色。重叠带相邻 tile 权重互补（a+b=1），
                // 后续 alpha 混合即可得到无接缝结果
                *dp = argb_from_rgb(nr, ng, nb);
            } else {
                int or_ = (old >> 16) & 0xff;
                int og = (old >> 8) & 0xff;
                int ob = old & 0xff;
                float ia = 1.0f - alpha;
                *dp = argb_from_rgb(
                    (int)(or_ * ia + nr * alpha + 0.5f),
                    (int)(og * ia + ng * alpha + 0.5f),
                    (int)(ob * ia + nb * alpha + 0.5f));
            }
        }
    }
}

// 调用者持有 g_lock
static bool upscale_ai_locked(const uint32_t* src, int width, int height,
                              uint32_t* dst, int outW, int outH, JNIEnv* env) {
    bool ok = true;
    auto t0 = std::chrono::steady_clock::now();
    int tileCount = 0;

    if (width <= TILE_SIZE && height <= TILE_SIZE) {
        ncnn::Mat in(width, height, 3);
        float* inR = (float*)in.channel(0).data;
        float* inG = (float*)in.channel(1).data;
        float* inB = (float*)in.channel(2).data;
        for (int i = 0; i < width * height; i++) {
            uint32_t p = src[i];
            inR[i] = ((p >> 16) & 0xff) / 255.0f;
            inG[i] = ((p >> 8) & 0xff) / 255.0f;
            inB[i] = (p & 0xff) / 255.0f;
        }
        ncnn::Mat out;
        if (!extract_into_planar_locked(in, out, outW, outH, env)) {
            LOGE("full-frame inference failed: %dx%d", width, height);
            return false;
        }
        for (int y = 0; y < outH; y++) {
            for (int x = 0; x < outW; x++) {
                int i = y * outW + x;
                dst[i] = argb_from_rgb(
                    clamp255(((float*)out.channel(0).data)[i]),
                    clamp255(((float*)out.channel(1).data)[i]),
                    clamp255(((float*)out.channel(2).data)[i]));
            }
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        LOGI("ai upscale mode=%d full %dx%d vulkan=%d took=%lldms",
             g_ctx.mode, width, height, (int)g_ctx.usingVulkan, (long long)ms);
        return true;
    }

    // 分块：构造平面 fp32 输入
    ncnn::Mat inMat(width, height, 3);
    float* inR = (float*)inMat.channel(0).data;
    float* inG = (float*)inMat.channel(1).data;
    float* inB = (float*)inMat.channel(2).data;
    for (int i = 0; i < width * height; i++) {
        uint32_t p = src[i];
        inR[i] = ((p >> 16) & 0xff) / 255.0f;
        inG[i] = ((p >> 8) & 0xff) / 255.0f;
        inB[i] = (p & 0xff) / 255.0f;
    }

    // Vulkan 显存充足，使用 512 大块：1440 宽页面的 tile 数从约 40+ 降到
    // 约 12，GPU 提交与重叠带开销大幅下降；CPU 降级时保持 256 小块控制内存
    const int tileSize = g_ctx.usingVulkan ? GPU_TILE_SIZE : TILE_SIZE;
    int tilesX = (width + tileSize - 1) / tileSize;
    int tilesY = (height + tileSize - 1) / tileSize;

    for (int ty = 0; ty < tilesY && ok; ty++) {
        for (int tx = 0; tx < tilesX; tx++) {
            int x0 = tx * tileSize;
            int y0 = ty * tileSize;
            int x1 = std::min(x0 + tileSize, width);
            int y1 = std::min(y0 + tileSize, height);

            // 分块向四周扩展重叠边
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
                memcpy(tR + y * pw, inR + (py0 + y) * width + px0, pw * sizeof(float));
                memcpy(tG + y * pw, inG + (py0 + y) * width + px0, pw * sizeof(float));
                memcpy(tB + y * pw, inB + (py0 + y) * width + px0, pw * sizeof(float));
            }

            ncnn::Mat tileOut;
            if (!extract_into_planar_locked(tileIn, tileOut,
                                            pw * SCALE, ph * SCALE, env)) {
                LOGE("tile inference failed at (%d,%d) tile=%dx%d",
                     tx, ty, pw, ph);
                ok = false;
                break;
            }
            tileCount++;

            // 有重叠区域（含 pad 放大结果）整体参与羽化混合
            int sx0 = 0;
            int sy0 = 0;
            int sw = pw * SCALE;
            int sh = ph * SCALE;
            blend_tile(dst, outW, outH, tileOut, sx0, sy0, sw, sh,
                       px0 * SCALE, py0 * SCALE);
        }
    }
    if (ok) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        LOGI("ai upscale mode=%d tiled %dx%d tiles=%d/%d vulkan=%d took=%lldms",
             g_ctx.mode, width, height, tileCount, tilesX * tilesY,
             (int)g_ctx.usingVulkan, (long long)ms);
    }
    return ok;
}

extern "C" {

JNIEXPORT jboolean JNICALL
Java_io_legado_app_manga_MangaEnhanceNcnn_nativeInit(
        JNIEnv* env, jobject /*thiz*/, jint mode, jobject assetManager,
        jstring modelParam, jstring modelBin,
        jstring inputBlob, jstring outputBlob) {
    std::lock_guard<std::mutex> guard(g_lock);

    // 释放上一个模型，保证同一时刻只加载一个
    destroy_net();
    g_ctx.mode = mode;
    g_ctx.vulkanAvailable = ncnn::get_gpu_count() > 0;
    g_ctx.usingVulkan = false;
    g_ctx.vulkanFailed = false;

    if (mode == MODE_LANCZOS) {
        // 纯插值，无需加载模型
        return JNI_TRUE;
    }

    const char* paramPath = env->GetStringUTFChars(modelParam, nullptr);
    const char* binPath = env->GetStringUTFChars(modelBin, nullptr);
    const char* inBlob = env->GetStringUTFChars(inputBlob, nullptr);
    const char* outBlob = env->GetStringUTFChars(outputBlob, nullptr);

    g_ctx.paramPath = paramPath ? paramPath : "";
    g_ctx.binPath = binPath ? binPath : "";
    g_ctx.inputBlob = inBlob ? inBlob : "data";
    g_ctx.outputBlob = outBlob ? outBlob : "output";

    // 持有 AssetManager 的全局引用，供 Vulkan 失败后重建 CPU 网络时复用
    if (!g_ctx.assetManagerRef) {
        g_ctx.assetManagerRef = env->NewGlobalRef(assetManager);
    }

    bool useVulkan = g_ctx.vulkanAvailable;
    ncnn::Net* net = create_net(useVulkan);
    bool loaded = load_net_assets(net, env);

    if (!loaded && useVulkan) {
        // 极个别设备能枚举到 GPU 但着色器管线初始化失败，直接退回 CPU 加载
        LOGE("vulkan net load failed, retry with CPU net");
        delete net;
        net = create_net(false);
        g_ctx.vulkanFailed = true;
        loaded = load_net_assets(net, env);
    }

    env->ReleaseStringUTFChars(modelParam, paramPath);
    env->ReleaseStringUTFChars(modelBin, binPath);
    env->ReleaseStringUTFChars(inputBlob, inBlob);
    env->ReleaseStringUTFChars(outputBlob, outBlob);

    if (!loaded) {
        delete net;
        return JNI_FALSE;
    }
    g_ctx.net = net;
    g_ctx.usingVulkan = useVulkan && !g_ctx.vulkanFailed;

    // 预热：用小图跑一次真实推理，提前编译 Vulkan 着色器管线（避免首张图
    // 卡顿），并验证输出 Mat 确为平面 fp32；若 Vulkan 结果异常会在此一次性
    // 销毁并重建 CPU 网络（见 extract_into_planar_locked）
    auto warmT0 = std::chrono::steady_clock::now();
    ncnn::Mat probe(64, 64, 3);
    probe.fill(0.5f);
    ncnn::Mat probeOut;
    if (!extract_into_planar_locked(probe, probeOut, 64 * SCALE, 64 * SCALE, env)) {
        LOGE("warmup probe failed mode=%d, model unusable", mode);
        destroy_net();
        return JNI_FALSE;
    }
    auto warmMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - warmT0).count();
    LOGI("model loaded mode=%d vulkan=%d (available=%d) warmup=%lldms",
         mode, (int)g_ctx.usingVulkan, (int)g_ctx.vulkanAvailable, (long long)warmMs);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_io_legado_app_manga_MangaEnhanceNcnn_nativeIsVulkanAvailable(
        JNIEnv* /*env*/, jobject /*thiz*/) {
    std::lock_guard<std::mutex> guard(g_lock);
    return ncnn::get_gpu_count() > 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_io_legado_app_manga_MangaEnhanceNcnn_nativeIsUsingVulkan(
        JNIEnv* /*env*/, jobject /*thiz*/) {
    std::lock_guard<std::mutex> guard(g_lock);
    return (g_ctx.net && g_ctx.usingVulkan) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jintArray JNICALL
Java_io_legado_app_manga_MangaEnhanceNcnn_nativeUpscale(
        JNIEnv* env, jobject /*thiz*/, jintArray pixels, jint width, jint height, jint mode) {
    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    std::lock_guard<std::mutex> guard(g_lock);

    if (mode != MODE_LANCZOS && (!g_ctx.net || g_ctx.mode != mode)) {
        LOGE("model not ready for mode=%d (current=%d)", mode, g_ctx.mode);
        return nullptr;
    }

    jint* srcJint = env->GetIntArrayElements(pixels, nullptr);
    if (!srcJint) {
        return nullptr;
    }
    const uint32_t* src = reinterpret_cast<const uint32_t*>(srcJint);

    const int outW = width * SCALE;
    const int outH = height * SCALE;
    jintArray result = env->NewIntArray(outW * outH);
    if (!result) {
        env->ReleaseIntArrayElements(pixels, srcJint, JNI_ABORT);
        return nullptr;
    }
    // 初始填充 0（黑），blend_tile 依赖初值判断是否首次写入
    jint* dstJint = env->GetIntArrayElements(result, nullptr);
    if (!dstJint) {
        env->ReleaseIntArrayElements(pixels, srcJint, JNI_ABORT);
        return nullptr;
    }
    uint32_t* dst = reinterpret_cast<uint32_t*>(dstJint);

    bool ok;
    if (mode == MODE_LANCZOS) {
        upscale_lanczos_locked(src, width, height, dst, outW, outH);
        ok = true;
    } else {
        ok = upscale_ai_locked(src, width, height, dst, outW, outH, env);
    }

    env->ReleaseIntArrayElements(pixels, srcJint, JNI_ABORT);
    if (!ok) {
        env->ReleaseIntArrayElements(result, dstJint, JNI_ABORT);
        return nullptr;
    }
    env->ReleaseIntArrayElements(result, dstJint, 0);
    return result;
}

JNIEXPORT void JNICALL
Java_io_legado_app_manga_MangaEnhanceNcnn_nativeRelease(
        JNIEnv* env, jobject /*thiz*/) {
    std::lock_guard<std::mutex> guard(g_lock);
    destroy_net();
    g_ctx.mode = MODE_LANCZOS;
    g_ctx.paramPath.clear();
    g_ctx.binPath.clear();
    if (g_ctx.assetManagerRef) {
        env->DeleteGlobalRef(g_ctx.assetManagerRef);
        g_ctx.assetManagerRef = nullptr;
    }
    // AAssetManager* 由 Java AssetManager 持有，GlobalRef 释放后不可再用
    g_ctx.assetMgr = nullptr;
}

} // extern "C"
