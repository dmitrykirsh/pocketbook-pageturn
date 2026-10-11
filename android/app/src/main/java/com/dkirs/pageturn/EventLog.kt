package com.dkirs.pageturn

import android.content.Context
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Небольшой журнал событий в файле (переживает смерть процесса).
 * Нужен для диагностики: что происходит, когда экран телефона погашен.
 */
object EventLog {
    private const val MAX_BYTES = 40_000L
    private const val KEEP_LINES = 200
    private val fmt = SimpleDateFormat("HH:mm:ss", Locale.US)

    private fun file(c: Context) = File(c.applicationContext.filesDir, "events.log")

    @Synchronized
    fun add(c: Context, msg: String) {
        try {
            val f = file(c)
            f.appendText("${fmt.format(Date())}  $msg\n")
            if (f.length() > MAX_BYTES) {
                f.writeText(f.readLines().takeLast(KEEP_LINES).joinToString("\n", postfix = "\n"))
            }
        } catch (_: Exception) {
        }
    }

    @Synchronized
    fun tail(c: Context, n: Int): String = try {
        file(c).readLines().takeLast(n).joinToString("\n")
    } catch (_: Exception) {
        ""
    }

    @Synchronized
    fun clear(c: Context) {
        try {
            file(c).writeText("")
        } catch (_: Exception) {
        }
    }
}
