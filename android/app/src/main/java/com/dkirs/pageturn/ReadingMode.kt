package com.dkirs.pageturn

import android.content.Context
import android.content.Intent
import android.os.SystemClock

/**
 * «Режим чтения»: пока он включён, кнопки громкости перехватываются и листают книгу.
 * Когда выключен (или процесс приложения погиб) — громкость работает как обычно.
 */
object ReadingMode {
    @Volatile
    var active = false

    @Volatile
    private var lastActivity = 0L

    /** Отметить активность (сбрасывает таймер автоотключения). */
    fun touch() {
        lastActivity = SystemClock.elapsedRealtime()
    }

    /** Сколько мс осталось до автоотключения; null — если таймер выключен или режим не активен. */
    fun remainingMs(ctx: Context): Long? {
        val min = Prefs.autoStopMin(ctx)
        if (!active || min <= 0) return null
        val left = min * 60_000L - (SystemClock.elapsedRealtime() - lastActivity)
        return left.coerceAtLeast(0L)
    }

    fun start(ctx: Context) {
        touch()
        ctx.applicationContext.startForegroundService(Intent(ctx, ScreenOffService::class.java))
    }

    fun stop(ctx: Context) {
        ctx.applicationContext.stopService(Intent(ctx, ScreenOffService::class.java))
    }
}
