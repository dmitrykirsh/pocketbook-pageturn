package com.dkirs.pageturn

import android.content.Context
import android.content.SharedPreferences

object Prefs {
    const val NONE = 0
    const val NEXT = 1
    const val PREV = 2

    const val THEME_SYSTEM = 0
    const val THEME_LIGHT = 1
    const val THEME_DARK = 2

    private fun sp(c: Context): SharedPreferences =
        c.applicationContext.getSharedPreferences("pageturn", Context.MODE_PRIVATE)

    fun host(c: Context): String = sp(c).getString("host", "") ?: ""
    fun port(c: Context): Int = sp(c).getInt("port", 57321)

    /** PIN-код книги (в хранилище ключ по-прежнему "secret"). */
    fun pin(c: Context): String = sp(c).getString("secret", "") ?: ""

    /** Действие для «Громкость +» (по умолчанию — назад). */
    fun volUp(c: Context): Int = sp(c).getInt("volUp", PREV)

    /** Действие для «Громкость −» (по умолчанию — вперёд). */
    fun volDown(c: Context): Int = sp(c).getInt("volDown", NEXT)

    /** Через сколько минут бездействия режим чтения выключается сам; 0 — никогда. */
    fun autoStopMin(c: Context): Int = sp(c).getInt("autoStopMin", 30)

    fun theme(c: Context): Int = sp(c).getInt("theme", THEME_SYSTEM)

    /** Играть почти бесшумный аудиопоток (держит медиасессию «играющей»). */
    fun audioStream(c: Context): Boolean = sp(c).getBoolean("audioStream", true)

    /** Не давать экрану погаснуть (тусклый экран) — запасной вариант для погашенного экрана. */
    fun keepScreen(c: Context): Boolean = sp(c).getBoolean("keepScreen", false)

    fun setConnection(c: Context, host: String, port: Int, pin: String) {
        sp(c).edit().putString("host", host.trim()).putInt("port", port)
            .putString("secret", pin.trim()).apply()
    }

    fun setVolUp(c: Context, action: Int) = sp(c).edit().putInt("volUp", action).apply()
    fun setVolDown(c: Context, action: Int) = sp(c).edit().putInt("volDown", action).apply()
    fun setAutoStopMin(c: Context, min: Int) = sp(c).edit().putInt("autoStopMin", min).apply()
    fun setTheme(c: Context, theme: Int) = sp(c).edit().putInt("theme", theme).apply()
    fun setAudioStream(c: Context, on: Boolean) = sp(c).edit().putBoolean("audioStream", on).apply()
    fun setKeepScreen(c: Context, on: Boolean) = sp(c).edit().putBoolean("keepScreen", on).apply()

    /** Старый длинный секрет (из шестнадцатеричных символов) не годится как PIN — сбрасываем. */
    fun migrateLegacySecret(c: Context) {
        val s = pin(c)
        if (s.isNotEmpty() && !s.all { it.isDigit() }) sp(c).edit().putString("secret", "").apply()
    }

    fun commandFor(action: Int): String? = when (action) {
        NEXT -> "next"
        PREV -> "prev"
        else -> null
    }
}
