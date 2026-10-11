package com.dkirs.pageturn

import android.content.Context
import android.os.SystemClock
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.NetworkInterface
import java.net.SocketTimeoutException

/**
 * Автопоиск книги: шлём широковещательный запрос "PTDISCOVER" на все сетевые интерфейсы
 * (домашний Wi-Fi и точка доступа самого телефона) и собираем ответы "PTHERE:<порт>:<имя>".
 */
object Discovery {
    data class Book(val host: String, val port: Int, val name: String)

    /** Блокирующий вызов (до [timeoutMs]); запускать не из главного потока. */
    fun find(ctx: Context, timeoutMs: Int = 2500): List<Book> {
        val found = LinkedHashMap<String, Book>()
        val port = Prefs.port(ctx)
        val payload = "PTDISCOVER".toByteArray(Charsets.US_ASCII)
        try {
            DatagramSocket(null).use { s ->
                s.reuseAddress = true
                s.broadcast = true
                s.soTimeout = 250
                s.bind(InetSocketAddress(0))
                val targets = broadcastAddresses()
                val end = SystemClock.elapsedRealtime() + timeoutMs
                var nextSend = 0L
                val buf = ByteArray(256)
                while (SystemClock.elapsedRealtime() < end) {
                    val now = SystemClock.elapsedRealtime()
                    if (now >= nextSend) {
                        for (t in targets) {
                            try {
                                s.send(DatagramPacket(payload, payload.size, t, port))
                            } catch (_: Exception) {
                            }
                        }
                        nextSend = now + 700
                    }
                    try {
                        val p = DatagramPacket(buf, buf.size)
                        s.receive(p)
                        val text = String(p.data, 0, p.length, Charsets.UTF_8)
                        if (text.startsWith("PTHERE:")) {
                            val parts = text.split(':', limit = 3)
                            val bookPort = parts.getOrNull(1)?.toIntOrNull() ?: port
                            val name = parts.getOrNull(2)?.trim().orEmpty().ifEmpty { "PocketBook" }
                            val host = p.address.hostAddress ?: continue
                            found[host] = Book(host, bookPort, name)
                        }
                    } catch (_: SocketTimeoutException) {
                    }
                }
            }
        } catch (e: Exception) {
            EventLog.add(ctx, "discovery: ERROR ${e.message}")
        }
        EventLog.add(ctx, "discovery: found ${found.size}")
        return found.values.toList()
    }

    /**
     * Проверка подключения: шлём "<PIN>:ping" на адрес книги и ждём "PTOK".
     * Подтверждает и адрес, и PIN. Блокирующий вызов — не из главного потока.
     */
    fun verify(host: String, port: Int, pin: String, timeoutMs: Int = 2200): Boolean {
        return try {
            DatagramSocket().use { s ->
                s.soTimeout = 300
                val payload = "$pin:ping".toByteArray(Charsets.US_ASCII)
                val addr = InetAddress.getByName(host)
                val end = SystemClock.elapsedRealtime() + timeoutMs
                var nextSend = 0L
                var sent = 0
                val buf = ByteArray(32)
                while (SystemClock.elapsedRealtime() < end) {
                    val now = SystemClock.elapsedRealtime()
                    if (now >= nextSend && sent < 3) {
                        s.send(DatagramPacket(payload, payload.size, addr, port))
                        sent++
                        nextSend = now + 700
                    }
                    try {
                        val p = DatagramPacket(buf, buf.size)
                        s.receive(p)
                        if (String(p.data, 0, p.length, Charsets.US_ASCII).startsWith("PTOK")) return true
                    } catch (_: SocketTimeoutException) {
                    }
                }
                false
            }
        } catch (_: Exception) {
            false
        }
    }

    private fun broadcastAddresses(): List<InetAddress> {
        val list = LinkedHashSet<InetAddress>()
        try {
            list.add(InetAddress.getByName("255.255.255.255"))
            val ifs = NetworkInterface.getNetworkInterfaces()
            while (ifs != null && ifs.hasMoreElements()) {
                val ni = ifs.nextElement()
                if (!ni.isUp || ni.isLoopback) continue
                for (ia in ni.interfaceAddresses) ia.broadcast?.let { list.add(it) }
            }
        } catch (_: Exception) {
        }
        return list.toList()
    }
}
