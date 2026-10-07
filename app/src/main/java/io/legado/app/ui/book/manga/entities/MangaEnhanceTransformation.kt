package io.legado.app.ui.book.manga.entities

import android.graphics.Bitmap
import com.bumptech.glide.load.engine.bitmap_recycle.BitmapPool
import com.bumptech.glide.load.resource.bitmap.BitmapTransformation
import java.security.MessageDigest
import kotlin.math.roundToInt

/**
 * 漫画图片清晰度增强转换器。
 *
 * 配合更高分辨率的解码请求（放大）使用，对图片做非锐化蒙版（Unsharp Mask）处理：
 * out = 原图 + amount * (原图 - 模糊图)
 * 可增强漫画线条、对白文字的边缘清晰度，本转换器不改变图片尺寸。
 *
 * @param amount 锐化强度，0 表示不处理
 */
class MangaEnhanceTransformation(
    private val amount: Float = DEFAULT_AMOUNT,
) : BitmapTransformation() {

    companion object {
        const val DEFAULT_AMOUNT = 0.6f

        private const val ID =
            "io.legado.app.ui.book.manga.entities.MangaEnhanceTransformation"
        private val ID_BYTES = ID.toByteArray(Charsets.UTF_8)

        private fun clampChannel(value: Float): Int {
            return when {
                value <= 0f -> 0
                value >= 255f -> 255
                else -> value.roundToInt()
            }
        }
    }

    override fun transform(
        pool: BitmapPool,
        toTransform: Bitmap,
        outWidth: Int,
        outHeight: Int,
    ): Bitmap {
        if (amount <= 0f) {
            return toTransform
        }
        val width = toTransform.width
        val height = toTransform.height
        if (width <= 2 || height <= 2) {
            return toTransform
        }
        val resultBitmap = pool.get(width, height, Bitmap.Config.ARGB_8888)
        val pixels = IntArray(width * height)
        toTransform.getPixels(pixels, 0, width, 0, 0, width, height)
        // 半径1的盒子模糊（水平、垂直两个一维通道）
        val blurred = boxBlur(pixels, width, height)
        for (i in pixels.indices) {
            val pixel = pixels[i]
            val blurPixel = blurred[i]
            val alpha = pixel ushr 24
            val red = clampChannel(
                (pixel shr 16 and 0xFF) +
                        amount * ((pixel shr 16 and 0xFF) - (blurPixel shr 16 and 0xFF))
            )
            val green = clampChannel(
                (pixel shr 8 and 0xFF) +
                        amount * ((pixel shr 8 and 0xFF) - (blurPixel shr 8 and 0xFF))
            )
            val blue = clampChannel(
                (pixel and 0xFF) +
                        amount * ((pixel and 0xFF) - (blurPixel and 0xFF))
            )
            pixels[i] = (alpha shl 24) or (red shl 16) or (green shl 8) or blue
        }
        resultBitmap.setPixels(pixels, 0, width, 0, 0, width, height)
        return resultBitmap
    }

    /**
     * 可分离的3x3盒子模糊（权重 1/4、2/4、1/4），边缘像素使用邻值钳制
     */
    private fun boxBlur(pixels: IntArray, width: Int, height: Int): IntArray {
        val size = pixels.size
        val horizontal = IntArray(size)
        val result = IntArray(size)
        var index = 0
        for (y in 0 until height) {
            val rowStart = y * width
            for (x in 0 until width) {
                val left = pixels[rowStart + (x - 1).coerceAtLeast(0)]
                val center = pixels[rowStart + x]
                val right = pixels[rowStart + (x + 1).coerceAtMost(width - 1)]
                horizontal[index++] = blendQuarter(left, center, right)
            }
        }
        index = 0
        for (y in 0 until height) {
            val topRow = (y - 1).coerceAtLeast(0) * width
            val centerRow = y * width
            val bottomRow = (y + 1).coerceAtMost(height - 1) * width
            for (x in 0 until width) {
                result[index] = blendQuarter(
                    horizontal[topRow + x],
                    horizontal[centerRow + x],
                    horizontal[bottomRow + x]
                )
                index++
            }
        }
        return result
    }

    private fun blendQuarter(first: Int, second: Int, third: Int): Int {
        val alpha = first ushr 24
        val red = (((first shr 16 and 0xFF) +
                (second shr 16 and 0xFF) * 2 +
                (third shr 16 and 0xFF)) shr 2)
        val green = (((first shr 8 and 0xFF) +
                (second shr 8 and 0xFF) * 2 +
                (third shr 8 and 0xFF)) shr 2)
        val blue = (((first and 0xFF) +
                (second and 0xFF) * 2 +
                (third and 0xFF)) shr 2)
        return (alpha shl 24) or (red shl 16) or (green shl 8) or blue
    }

    override fun updateDiskCacheKey(messageDigest: MessageDigest) {
        messageDigest.update(ID_BYTES)
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as MangaEnhanceTransformation
        return amount == other.amount
    }

    override fun hashCode(): Int {
        return ID.hashCode() + amount.hashCode()
    }
}
