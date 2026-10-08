package io.legado.app.manga

import android.content.Context
import android.content.res.AssetManager
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.widget.Toast
import io.legado.app.R
import splitties.init.appCtx

/**
 * 漫画画质增强 native 桥接。
 *
 * 支持三种 2x 放大模式：
 * - [MODE_LANCZOS]：Lanczos3 传统插值，纯 CPU、不加载模型，作为快速兜底
 * - [MODE_REALCUGAN]：Real-CUGAN 2x，黑白漫画优先，占用低
 * - [MODE_ANIME6B]：RealESRGAN Anime6B（realesr-animevideov3-x2），彩色漫画/条漫
 *
 * 规则：
 * - 同一时刻只加载一个 AI 模型，切换时 native 层先销毁上一个模型
 * - 自动检测 Vulkan，支持则 GPU 加速，否则/失败时自动降级 CPU
 * - 所有推理仅允许在子线程调用
 */
object MangaEnhanceNcnn {

    const val MODE_LANCZOS = 0
    const val MODE_REALCUGAN = 1
    const val MODE_ANIME6B = 2

    private const val TAG = "MangaEnhanceNcnn"

    private data class ModelSpec(
        val paramPath: String,
        val binPath: String,
        val inputBlob: String,
        val outputBlob: String,
    )

    private val MODELS = mapOf(
        MODE_REALCUGAN to ModelSpec(
            "realcugan/up2x-no-denoise.param",
            "realcugan/up2x-no-denoise.bin",
            "in0", "out0",
        ),
        MODE_ANIME6B to ModelSpec(
            "realesrgan/realesr-animevideov3-x2.param",
            "realesrgan/realesr-animevideov3-x2.bin",
            "data", "output",
        ),
    )

    private var libLoaded = false

    init {
        try {
            System.loadLibrary("realcugan_ncnn")
            libLoaded = true
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "Failed to load realcugan_ncnn library", e)
        }
    }

    @Volatile
    private var currentMode = -1

    @Volatile
    private var initialized = false

    private var vulkanAvailableCached: Boolean? = null

    private var cpuSlowWarned = false

    private external fun nativeInit(
        mode: Int,
        assetManager: AssetManager?,
        modelParam: String,
        modelBin: String,
        inputBlob: String,
        outputBlob: String,
    ): Boolean

    private external fun nativeIsVulkanAvailable(): Boolean
    private external fun nativeIsUsingVulkan(): Boolean
    external fun nativeUpscale(pixels: IntArray, width: Int, height: Int, mode: Int): IntArray?
    private external fun nativeRelease()

    /**
     * 设置中止标志：C++ 分块推理循环在每个 tile 完成后检查此标志，若为 true
     * 立即停止后续 tile 推理。调度器在用户翻页到不在此轮增强窗口时调用，
     * 避免老推理独占 GPU 导致整机卡顿。下一次 [nativeInit] 会自动清除此标志。
     */
    external fun nativeSetAbort(abort: Boolean)

    fun isLibraryLoaded(): Boolean = libLoaded

    /**
     * 加载（或切换到）指定模式。切换模型时 native 层会先释放上一个模型。
     * 必须在子线程调用（模型文件读取与 Vulkan 初始化较慢）。
     */
    @Synchronized
    fun ensureInit(context: Context, mode: Int): Boolean {
        if (!libLoaded) return false
        if (mode == MODE_LANCZOS) {
            // 纯插值模式：释放已加载模型
            val ok = try {
                nativeInit(MODE_LANCZOS, context.assets, "", "", "", "")
            } catch (e: Throwable) {
                Log.e(TAG, "nativeInit lanczos exception", e)
                false
            }
            currentMode = MODE_LANCZOS
            initialized = ok
            return ok
        }
        val spec = MODELS[mode] ?: return false
        val ok = try {
            nativeInit(
                mode, context.assets,
                spec.paramPath, spec.binPath,
                spec.inputBlob, spec.outputBlob,
            )
        } catch (e: Throwable) {
            Log.e(TAG, "nativeInit mode=$mode exception", e)
            false
        }
        if (ok) {
            currentMode = mode
            initialized = true
            Log.i(TAG, "enhance model ready: mode=$mode vulkan=${nativeIsUsingVulkan()}")
        } else {
            initialized = false
            Log.e(TAG, "enhance model init failed: mode=$mode")
        }
        return ok
    }

    /** 指定模式是否已就绪可直接推理 */
    fun isReady(mode: Int): Boolean {
        if (!libLoaded) return false
        return if (mode == MODE_LANCZOS) true else initialized && currentMode == mode
    }

    /** 设备是否支持 Vulkan */
    fun isVulkanAvailable(): Boolean {
        if (!libLoaded) return false
        vulkanAvailableCached?.let { return it }
        val v = try {
            nativeIsVulkanAvailable()
        } catch (e: Throwable) {
            Log.e(TAG, "vulkan query exception", e)
            false
        }
        vulkanAvailableCached = v
        return v
    }

    /** 当前模型是否实际运行在 Vulkan 上 */
    fun isUsingVulkan(): Boolean {
        if (!libLoaded) return false
        return try {
            nativeIsUsingVulkan()
        } catch (e: Throwable) {
            false
        }
    }

    /**
     * 2x 放大。仅允许在子线程调用（Glide transform 线程）。
     * @return 放大后的像素数组，失败返回 null 由调用方回退原图
     */
    fun upscale(pixels: IntArray, width: Int, height: Int, mode: Int): IntArray? {
        if (!libLoaded) return null
        if (Looper.myLooper() == Looper.getMainLooper()) {
            // 禁止主线程推理，防止 ANR
            Log.e(TAG, "upscale called on main thread, rejected")
            return null
        }
        return try {
            nativeUpscale(pixels, width, height, mode)
        } catch (e: Throwable) {
            Log.e(TAG, "nativeUpscale exception mode=$mode", e)
            null
        }
    }

    /**
     * AI 模型运行在 CPU 时给出一次性慢速提示（主线程 Toast）。
     */
    fun maybeNotifyCpuSlow(mode: Int) {
        if (mode == MODE_LANCZOS || cpuSlowWarned) return
        if (!isUsingVulkan()) {
            cpuSlowWarned = true
            Handler(Looper.getMainLooper()).post {
                Toast.makeText(appCtx, R.string.manga_enhance_cpu_slow, Toast.LENGTH_LONG).show()
            }
        }
    }
}
