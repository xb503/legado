package io.legado.app.manga

import android.content.Context
import android.content.res.AssetManager
import android.util.Log

/**
 * Real-CUGAN (ncnn) super-resolution native bridge.
 *
 * Provides 2x upscaling for manga images via a locally bundled ncnn model.
 */
object RealCuganNcnn {

    private const val TAG = "RealCuganNcnn"
    private const val MODEL_PARAM = "realcugan/up2x-no-denoise.param"
    private const val MODEL_BIN = "realcugan/up2x-no-denoise.bin"

    private var libLoaded = false
    private var initialized = false

    init {
        try {
            System.loadLibrary("realcugan_ncnn")
            libLoaded = true
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "Failed to load realcugan_ncnn library", e)
        }
    }

    private external fun init(assetManager: AssetManager, modelParam: String, modelBin: String): Boolean

    /**
     * Upscale an ARGB_8888 pixel buffer by 2x.
     * @return upscaled pixel buffer, or null on failure
     */
    external fun upscale(pixels: IntArray, width: Int, height: Int): IntArray?

    external fun release()

    @Synchronized
    fun ensureInit(context: Context): Boolean {
        if (initialized) return true
        if (!libLoaded) return false
        return try {
            initialized = init(context.assets, MODEL_PARAM, MODEL_BIN)
            if (initialized) {
                Log.i(TAG, "Real-CUGAN initialized successfully")
            } else {
                Log.e(TAG, "Real-CUGAN init returned false")
            }
            initialized
        } catch (e: Exception) {
            Log.e(TAG, "Real-CUGAN init exception", e)
            false
        }
    }

    fun isAvailable(): Boolean = libLoaded && initialized
}
