package io.legado.app.ui.book.manga.entities

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import androidx.annotation.IntRange
import com.bumptech.glide.load.engine.bitmap_recycle.BitmapPool
import com.bumptech.glide.load.resource.bitmap.BitmapTransformation
import java.nio.charset.StandardCharsets
import java.security.MessageDigest

/**
 * 图片清晰度放大转换器。
 *
 * 先将图片放大到指定倍数（双线性插值），再使用锐化卷积核增强边缘，
 * 以提升漫画图片（尤其是线条、文字）的显示清晰度。
 *
 * 为避免内存溢出，放大后的最长边会被限制在 [MAX_DIMENSION] 以内。
 *
 * @param scaleFactor 放大倍数（建议 2）。
 * @param sharpenStrength 锐化强度（中心系数，建议 5）。
 */
class ImageEnhanceTransformation(
    @IntRange(from = 1, to = 4) private val scaleFactor: Int = 2,
    @IntRange(from = 1, to = 255) private val sharpenStrength: Int = 5,
) : BitmapTransformation() {

    private val ID =
        "io.legado.app.model.ImageEnhanceTransformation.${scaleFactor}.${sharpenStrength}"
    private val ID_BYTES = ID.toByteArray(StandardCharsets.UTF_8)

    companion object {
        /** 放大后最长边的最大值，避免 OOM */
        private const val MAX_DIMENSION = 4096
    }

    override fun transform(
        pool: BitmapPool,
        toTransform: Bitmap,
        outWidth: Int,
        outHeight: Int,
    ): Bitmap {
        val srcWidth = toTransform.width
        val srcHeight = toTransform.height
        if (srcWidth <= 0 || srcHeight <= 0) {
            return toTransform
        }

        // 计算放大后的尺寸，并限制最长边不超过 MAX_DIMENSION
        var scaledWidth = srcWidth * scaleFactor
        var scaledHeight = srcHeight * scaleFactor
        val maxSide = maxOf(scaledWidth, scaledHeight)
        if (maxSide > MAX_DIMENSION) {
            val ratio = MAX_DIMENSION.toFloat() / maxSide
            scaledWidth = (scaledWidth * ratio).toInt().coerceAtLeast(1)
            scaledHeight = (scaledHeight * ratio).toInt().coerceAtLeast(1)
        }

        // 第一步：使用双线性插值放大图片
        val scaledBitmap = pool.get(scaledWidth, scaledHeight, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(scaledBitmap)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG or Paint.DITHER_FLAG)
        canvas.drawBitmap(
            toTransform,
            null,
            Rect(0, 0, scaledWidth, scaledHeight),
            paint
        )

        // 第二步：对放大后的图片进行锐化处理，增强边缘清晰度
        val resultBitmap = pool.get(scaledWidth, scaledHeight, Bitmap.Config.ARGB_8888)
        sharpen(scaledBitmap, resultBitmap)

        // 将临时放大图归还到对象池
        pool.put(scaledBitmap)

        return resultBitmap
    }

    /**
     * 使用经典锐化卷积核对图片进行锐化。
     * 卷积核：
     *   0  -1   0
     *  -1   5  -1
     *   0  -1   0
     */
    private fun sharpen(src: Bitmap, dst: Bitmap) {
        val width = src.width
        val height = src.height
        if (width < 3 || height < 3) {
            // 过小的图片直接拷贝
            val canvas = Canvas(dst)
            canvas.drawBitmap(src, 0f, 0f, Paint(Paint.FILTER_BITMAP_FLAG))
            return
        }

        val pixels = IntArray(width * height)
        src.getPixels(pixels, 0, width, 0, 0, width, height)
        val out = IntArray(width * height)

        val kc = sharpenStrength
        val kn = -1

        var idx = 0
        for (y in 0 until height) {
            val isEdgeRow = y == 0 || y == height - 1
            for (x in 0 until width) {
                if (isEdgeRow || x == 0 || x == width - 1) {
                    out[idx] = pixels[idx]
                    idx++
                    continue
                }

                val center = pixels[idx]
                val top = pixels[idx - width]
                val bottom = pixels[idx + width]
                val left = pixels[idx - 1]
                val right = pixels[idx + 1]

                val r = clamp(
                    kc * (center shr 16 and 0xFF) +
                            kn * ((top shr 16 and 0xFF) + (bottom shr 16 and 0xFF) +
                            (left shr 16 and 0xFF) + (right shr 16 and 0xFF))
                )
                val g = clamp(
                    kc * (center shr 8 and 0xFF) +
                            kn * ((top shr 8 and 0xFF) + (bottom shr 8 and 0xFF) +
                            (left shr 8 and 0xFF) + (right shr 8 and 0xFF))
                )
                val b = clamp(
                    kc * (center and 0xFF) +
                            kn * ((top and 0xFF) + (bottom and 0xFF) +
                            (left and 0xFF) + (right and 0xFF))
                )
                val a = center ushr 24

                out[idx] = (a shl 24) or (r shl 16) or (g shl 8) or b
                idx++
            }
        }

        dst.setPixels(out, 0, width, 0, 0, width, height)
    }

    private fun clamp(value: Int): Int {
        return when {
            value < 0 -> 0
            value > 255 -> 255
            else -> value
        }
    }

    override fun updateDiskCacheKey(messageDigest: MessageDigest) {
        messageDigest.update(ID_BYTES)
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as ImageEnhanceTransformation
        return scaleFactor == other.scaleFactor &&
                sharpenStrength == other.sharpenStrength &&
                ID == other.ID
    }

    override fun hashCode(): Int {
        var result = scaleFactor
        result = 31 * result + sharpenStrength
        result = 31 * result + ID.hashCode()
        return result
    }
}
