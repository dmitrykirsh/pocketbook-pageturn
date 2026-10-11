package com.dkirs.pageturn

import android.content.Context
import android.os.SystemClock
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.util.concurrent.Executors

/** Отправка команды "<PIN>:<cmd>" по UDP на сервер PageTurn на книге. */
object UdpSender {
    private val exec = Executors.newSingleThreadExecutor()

    /**
     * [quiet] — не писать успешную отправку в журнал (для регулярных «ping»).
     * [onResult] вызывается из фонового потока; сообщение уже переведено на язык системы.
     */
    fun send(ctx: Context, cmd: String, quiet: Boolean = false, onResult: ((String) -> Unit)? = null) {
        val app = ctx.applicationContext
        val host = Prefs.host(app)
        val port = Prefs.port(app)
        val pin = Prefs.pin(app)
        if (host.isBlank()) {
            EventLog.add(app, "→ $cmd: no book address")
            onResult?.invoke(app.getString(R.string.no_host))
            return
        }
        exec.execute {
            val t0 = SystemClock.elapsedRealtime()
            try {
                val payload = (if (pin.isEmpty()) cmd else "$pin:$cmd")
                    .toByteArray(Charsets.US_ASCII)
                DatagramSocket().use { s ->
                    s.send(DatagramPacket(payload, payload.size, InetAddress.getByName(host), port))
                }
                if (!quiet) EventLog.add(app, "→ $cmd: sent (${SystemClock.elapsedRealtime() - t0} ms)")
                onResult?.invoke(app.getString(R.string.sent_fmt, cmd))
            } catch (e: Exception) {
                EventLog.add(app, "→ $cmd: ERROR ${e.message}")
                onResult?.invoke(app.getString(R.string.send_error_fmt, e.message ?: ""))
            }
        }
    }
}
