package io.legado.app.ui.book.manga.entities

import android.graphics.Bitmap
import android.util.Log
import com.bumptech.glide.load.engine.bitmap_recycle.BitmapPool
import com.bumptech.glide.load.resource.bitmap.BitmapTransformation
import io.legado.app.manga.MangaEnhanceNcnn
import java.security.MessageDigest

/**
 * 漫画图片画质增强转换器（固定 2x 放大）。
 *
 * @param mode [MangaEnhanceNcnn.MODE_LANCZOS] / MODE_REALCUGAN / MODE_ANIME6B
 *
 * 说明：
 * - EXIF 旋转由 Glide 在解码阶段（Downsampler）自动矫正，进入此转换器的 Bitmap
 *   方向已经正确，无需重复处理。
 * - Bitmap 为 ARGB_8888，RGBA→RGB 的通道转换在 native 层完成（忽略 alpha）。
 * - 本方法由 Glide 工作线程调用，绝不在主线程执行推理。
 * - 模型未就绪或推理失败时原样返回输入图。
 */
class MangaEnhanceTransformation(
    private val mode: Int,
) : BitmapTransformation() {

    companion object {
        private const val TAG = "MangaEnhanceTransform"
        private const val SCALE = 2

        /**
         * 参与放大的最大输入像素数（约 2048x2440）。
         * 超过则跳过直接返回原图，避免 2x 输出导致内存压力过大。
         */
        private const val MAX_INPUT_PIXELS = 5_000_000L

        private const val ID =
            "io.legado.app.ui.book.manga.entities.MangaEnhanceTransformation.v3"
        private val ID_BYTES = ID.toByteArray(Charsets.UTF_8)
    }

    override fun transform(
        pool: BitmapPool,
        toTransform: Bitmap,
        outWidth: Int,
        outHeight: Int,
    ): Bitmap {
        // Lanczos 无需加载模型；AI 模式要求模型已就绪
        val ready = if (mode == MangaEnhanceNcnn.MODE_LANCZOS) {
            MangaEnhanceNcnn.isLibraryLoaded()
        } else {
            MangaEnhanceNcnn.isReady(mode)
        }
        if (!ready) {
            return toTransform
        }
        val width = toTransform.width
        val height = toTransform.height
        if (width <= 0 || height <= 0) {
            return toTransform
        }
        if (width.toLong() * height > MAX_INPUT_PIXELS) {
            Log.i(TAG, "skip enhance, image too large: ${width}x${height}")
            return toTransform
        }

        val pixels = IntArray(width * height)
        toTransform.getPixels(pixels, 0, width, 0, 0, width, height)

        val upscaled = MangaEnhanceNcnn.upscale(pixels, width, height, mode)
        if (upscaled == null) {
            return toTransform
        }

        // AI 模型实际运行在 CPU 时提示一次
        MangaEnhanceNcnn.maybeNotifyCpuSlow(mode)

        val resultW = width * SCALE
        val resultH = height * SCALE
        if (upscaled.size < resultW * resultH) {
            Log.e(TAG, "output size mismatch: ${upscaled.size} < ${resultW * resultH}")
            return toTransform
        }

        val resultBitmap = pool.get(resultW, resultH, Bitmap.Config.ARGB_8888)
        resultBitmap.setPixels(upscaled, 0, resultW, 0, 0, resultW, resultH)
        return resultBitmap
    }

    override fun updateDiskCacheKey(messageDigest: MessageDigest) {
        messageDigest.update(ID_BYTES)
        messageDigest.update(mode.toString().toByteArray(Charsets.UTF_8))
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        return other is MangaEnhanceTransformation && other.mode == mode
    }

    override fun hashCode(): Int {
        return ID.hashCode() * 31 + mode
    }
}
