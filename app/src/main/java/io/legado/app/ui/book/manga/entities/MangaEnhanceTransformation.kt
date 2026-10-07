package io.legado.app.ui.book.manga.entities

import android.graphics.Bitmap
import android.util.Log
import com.bumptech.glide.load.engine.bitmap_recycle.BitmapPool
import com.bumptech.glide.load.resource.bitmap.BitmapTransformation
import io.legado.app.manga.RealCuganNcnn
import java.security.MessageDigest

/**
 * 漫画图片 AI 超分辨率转换器（Real-CUGAN ncnn）。
 *
 * 使用本地 Real-CUGAN 模型对漫画图片进行 2 倍超分辨率放大，
 * 增强线条与对白文字边缘清晰度。若模型未初始化或推理失败，
 * 则原样返回输入图。
 *
 * 模型需要在使用前通过 [RealCuganNcnn.ensureInit] 完成初始化。
 */
class MangaEnhanceTransformation : BitmapTransformation() {

    companion object {
        private const val TAG = "MangaEnhanceTransform"
        private const val SCALE = 2

        /**
         * 参与 AI 超分的最大输入像素数（约 2048x2440）。
         * 超过则跳过超分直接返回原图，避免 2x 输出导致内存压力过大。
         */
        private const val MAX_INPUT_PIXELS = 5_000_000L

        private const val ID =
            "io.legado.app.ui.book.manga.entities.MangaEnhanceTransformation.realcugan2x.v2"
        private val ID_BYTES = ID.toByteArray(Charsets.UTF_8)
    }

    override fun transform(
        pool: BitmapPool,
        toTransform: Bitmap,
        outWidth: Int,
        outHeight: Int,
    ): Bitmap {
        if (!RealCuganNcnn.isAvailable()) {
            return toTransform
        }
        val width = toTransform.width
        val height = toTransform.height
        if (width <= 0 || height <= 0) {
            return toTransform
        }
        if (width.toLong() * height > MAX_INPUT_PIXELS) {
            Log.i(TAG, "skip super-resolution, image too large: ${width}x${height}")
            return toTransform
        }
        val pixels = IntArray(width * height)
        toTransform.getPixels(pixels, 0, width, 0, 0, width, height)

        val upscaled = try {
            RealCuganNcnn.upscale(pixels, width, height)
        } catch (e: Throwable) {
            Log.e(TAG, "Real-CUGAN upscale exception", e)
            null
        }

        if (upscaled == null) {
            return toTransform
        }

        val resultW = width * SCALE
        val resultH = height * SCALE
        if (upscaled.size < resultW * resultH) {
            Log.e(TAG, "Real-CUGAN output size mismatch: ${upscaled.size} < ${resultW * resultH}")
            return toTransform
        }

        val resultBitmap = pool.get(resultW, resultH, Bitmap.Config.ARGB_8888)
        resultBitmap.setPixels(upscaled, 0, resultW, 0, 0, resultW, resultH)
        return resultBitmap
    }

    override fun updateDiskCacheKey(messageDigest: MessageDigest) {
        messageDigest.update(ID_BYTES)
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        return javaClass == other?.javaClass
    }

    override fun hashCode(): Int {
        return ID.hashCode()
    }
}
