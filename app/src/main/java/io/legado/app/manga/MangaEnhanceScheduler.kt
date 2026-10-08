package io.legado.app.manga

import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.util.LruCache
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView

/**
 * 漫画 AI 画质增强任务调度器。
 *
 * 设计目标：避免 AI 推理独占 GPU 导致整机卡顿，同时保留翻页预读体验。
 *
 * 调度规则（三张窗口 + 位置触发 + 串行执行 + abort 中断）：
 * - 仅阅读位置变化时触发增强队列，位置不变不新增任务
 * - 串行执行，同一时间仅处理 1 张图
 * - 队列顺序：当前页 → 下一页 → 下下页（共最多 3 张）
 * - 翻页到新位置 P 时构建新窗口 [P, P+1, P+2]：
 *   - 若正在跑的页 ∈ 新窗口：保留它跑完，清空 pending 重建为新窗口中除它外的项
 *   - 否则：调 [MangaEnhanceNcnn.nativeSetAbort] 中止正在跑的，清空 pending 重建为新窗口
 * - 快速滑动 / fling 期间不新增任务，不中止正在跑的；停下后按落点重建窗口
 *
 * 缓存：内存 LRU 按字节数限制（默认 32MB），最多保留约 3 张全量增强 Bitmap，
 * 超出立即淘汰最远页面。需要展示时若缓存未命中则重新推理。
 *
 * 集成约定（方案 B：先加载原图，增强后替换）：
 * - Glide 仅加载原图（可叠加灰度 / 墨水屏等轻量变换，但不含增强变换）
 * - 调度器串行推理完成后通过 [deliverFn] 在主线程替换 ImageView
 * - 用户翻页时新页面先显示原图，停下后调度器替换为增强图
 */
class MangaEnhanceScheduler(
    private val enhanceFn: (position: Int) -> Bitmap?,
    private val deliverFn: (position: Int, Bitmap) -> Unit,
) {
    companion object {
        private const val TAG = "MangaEnhanceScheduler"
        private const val WINDOW_SIZE = 3
        // 增强图内存缓存上限（字节）。需保证三张窗口 [P,P+1,P+2] 都能驻留：
        // 921x1200 页 2x 后约 17.7MB/张，3 张约 53MB；690 宽页约 11MB/张
        private const val CACHE_LIMIT_BYTES = 64L * 1024L * 1024L
    }

    @Volatile var currentPosition: Int = -1
        private set

    @Volatile private var runningPosition: Int = -1

    private val pendingLock = Any()
    private val pendingQueue = ArrayDeque<Int>()

    // 已增强 Bitmap 内存缓存（key=position）。LRU 按字节数淘汰最远页面
    private val enhancedCache = object : LruCache<Int, Bitmap>(CACHE_LIMIT_BYTES.toInt()) {
        override fun sizeOf(key: Int, value: Bitmap): Int = value.byteCount
    }

    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile private var running = true
    private val workerThread = Thread({ workerLoop() }, "MangaEnhanceWorker").apply {
        isDaemon = true
    }

    init {
        workerThread.start()
    }

    /**
     * 阅读位置变化时调用：构建新窗口 [newPos, newPos+1, newPos+2]，
     * 按规则重建 pending 队列，必要时 abort 正在跑的任务。
     */
    fun onPositionChanged(newPos: Int) {
        if (newPos < 0) return
        currentPosition = newPos
        val window = IntArray(WINDOW_SIZE) { newPos + it }
        synchronized(pendingLock) {
            // 正在跑的页 ∈ 新窗口：保留它跑完；否则 abort 它
            val keepRunning = runningPosition in window
            if (!keepRunning && runningPosition != -1) {
                MangaEnhanceNcnn.nativeSetAbort(true)
            }
            // 重建 pending：窗口中除正在跑的页以外、且尚无增强缓存的页
            pendingQueue.clear()
            window.forEach { pos ->
                if (pos != runningPosition && enhancedCache.get(pos) == null) {
                    pendingQueue.addLast(pos)
                }
            }
            Log.i(
                TAG,
                "window=$newPos..${newPos + WINDOW_SIZE - 1} " +
                        "keepRunning=$keepRunning running=$runningPosition queued=$pendingQueue"
            )
            (pendingLock as java.lang.Object).notifyAll()
        }
    }

    /**
     * 滚动状态变化：fling / drag 期间不新增任务，停下后按落点重建窗口。
     */
    fun onScrollStateChanged(rv: RecyclerView, newState: Int) {
        if (newState == RecyclerView.SCROLL_STATE_IDLE) {
            val lm = rv.layoutManager as? LinearLayoutManager ?: return
            val pos = lm.findFirstVisibleItemPosition()
            if (pos != RecyclerView.NO_POSITION && pos >= 0) {
                onPositionChanged(pos)
            }
        }
        // SCROLL_STATE_DRAGGING / SCROLL_STATE_SETTLING：不新增，保留正在跑的
    }

    /**
     * 取已增强 Bitmap（缓存命中）。Adapter 在 onBindViewHolder 时可调，
     * 命中则直接显示增强图，否则显示原图等待调度器替换。
     */
    fun getCachedBitmap(position: Int): Bitmap? = enhancedCache.get(position)

    fun clearCache() {
        enhancedCache.evictAll()
    }

    fun shutdown() {
        running = false
        MangaEnhanceNcnn.nativeSetAbort(true)
        synchronized(pendingLock) {
            pendingQueue.clear()
            (pendingLock as java.lang.Object).notifyAll()
        }
        try {
            workerThread.join(1000)
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
        }
        enhancedCache.evictAll()
    }

    private fun workerLoop() {
        while (running) {
            val task = nextTask() ?: return
            processTask(task)
        }
    }

    private fun nextTask(): Int? {
        synchronized(pendingLock) {
            while (pendingQueue.isEmpty() && running) {
                try {
                    (pendingLock as java.lang.Object).wait()
                } catch (_: InterruptedException) {
                    Thread.currentThread().interrupt()
                }
            }
            if (!running) return null
            val task = pendingQueue.removeFirst()
            runningPosition = task
            // 必须在锁内、且在取出新任务之后清除 abort：
            // 与 onPositionChanged 的 abort(true) 互斥，既避免新任务被
            // 上轮残留标志误杀，也避免清掉针对旧任务的新一轮 abort
            MangaEnhanceNcnn.nativeSetAbort(false)
            return task
        }
    }

    private fun processTask(task: Int) {
        try {
            // 窗口内所有页（当前页 + 预读页）都要执行，不能用
            // currentPosition == task 过滤，否则预读永远不会发生。
            // 任务是否已失效由 onPositionChanged 清队列 + abort 负责。

            // 先查缓存（理论上入队时已过滤，双重保险）
            enhancedCache.get(task)?.let { cached ->
                deliverResult(task, cached)
                return
            }

            // 推理（enhanceFn 内部已包含原图加载 + nativeUpscale + Bitmap 创建；
            // abort 生效时 nativeUpscale 返回 null，enhanceFn 返回 null）
            val result = enhanceFn(task)
            if (result != null) {
                enhancedCache.put(task, result)
                Log.i(TAG, "enhanced pos=$task cacheHint=${result.byteCount}")
                deliverResult(task, result)
            } else {
                Log.i(TAG, "enhance skipped pos=$task (aborted or unavailable)")
            }
        } catch (t: Throwable) {
            Log.e(TAG, "enhance failed for pos=$task", t)
        } finally {
            synchronized(pendingLock) {
                runningPosition = -1
                // 本任务若因翻页被 abort，新窗口可能已在 abort 后重建入队；
                // 若恰好队列为空也无需特殊处理
                (pendingLock as java.lang.Object).notifyAll()
            }
        }
    }

    private fun deliverResult(task: Int, bitmap: Bitmap) {
        // 投递到主线程；仅替换仍属于当前阅读窗口 [P, P+2] 的页面，
        // 旧窗口任务的结果只保留在缓存中供之后命中。VH 尚不存在时
        // deliverFn 内部判空跳过，等该页 onBind 时从缓存直接显示
        mainHandler.post {
            if (task in currentPosition..currentPosition + WINDOW_SIZE - 1) {
                deliverFn(task, bitmap)
            }
        }
    }
}
