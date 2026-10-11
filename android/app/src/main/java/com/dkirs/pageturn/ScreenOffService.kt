package com.dkirs.pageturn

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.graphics.drawable.Icon
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.media.VolumeProvider
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import android.os.SystemClock
import android.service.quicksettings.TileService

/**
 * Служба «Режима чтения». Пока она работает, [ReadingMode.active] = true.
 *
 * Для работы при погашенном экране Android отдаёт кнопки громкости активной медиасессии
 * с удалённым VolumeProvider; чтобы сессия считалась активной, играет почти бесшумный поток
 * (с едва слышным шумом — чистые нули некоторые прошивки распознают как «тишину» и душат).
 * CPU и Wi-Fi держим блокировками. Всё важное пишется в [EventLog].
 */
@Suppress("DEPRECATION")
class ScreenOffService : Service() {

    companion object {
        const val ACTION_STOP = "com.dkirs.pageturn.STOP"
        private const val CHANNEL = "pageturn"
        private const val NOTIF_ID = 1
        private const val MIN_INTERVAL_MS = 350L
        private const val CHECK_EVERY_MS = 15_000L
        private const val PING_EVERY_MS = 30_000L
    }

    private var session: MediaSession? = null
    private var track: AudioTrack? = null
    private var wifiLock: WifiManager.WifiLock? = null
    private var cpuLock: PowerManager.WakeLock? = null
    @Volatile private var alive = false
    private var lastTs = 0L
    private var tick = 0
    private val handler = Handler(Looper.getMainLooper())

    private var screenLock: PowerManager.WakeLock? = null
    private var sensorManager: SensorManager? = null
    private var proximityNear: Boolean? = null

    private var audioMgr: AudioManager? = null
    private var savedVolume = -1
    @Volatile private var lockedVolume = -1

    /**
     * Запасной путь для погашенного экрана: когда «играет музыка», прошивка при нажатии громкости
     * сама меняет громкость медиа (мимо медиасессии). Ловим это изменение, листаем страницу и
     * возвращаем громкость на «базовую» отметку.
     */
    private val volumeReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) {
            val type = i.getIntExtra("android.media.EXTRA_VOLUME_STREAM_TYPE", -1)
            if (type != AudioManager.STREAM_MUSIC) return
            val v = i.getIntExtra("android.media.EXTRA_VOLUME_STREAM_VALUE", -1)
            val base = lockedVolume
            if (v < 0 || base < 0 || v == base) return // наш собственный возврат / нет изменения
            val dir = if (v > base) 1 else -1
            EventLog.add(c, "🔊 громкость медиа: $v (база $base) → ${if (dir > 0) "+" else "−"}")
            try {
                audioMgr?.setStreamVolume(AudioManager.STREAM_MUSIC, base, 0)
            } catch (_: Exception) {
            }
            handle(dir, "наблюдатель громкости")
        }
    }

    private val screenReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) {
            val text = when (i.action) {
                Intent.ACTION_SCREEN_OFF -> "◼ Экран выключен"
                Intent.ACTION_SCREEN_ON -> "◻ Экран включён"
                Intent.ACTION_USER_PRESENT -> "◻ Телефон разблокирован"
                PowerManager.ACTION_DEVICE_IDLE_MODE_CHANGED ->
                    "💤 Doze (глубокий сон Android): " +
                        if (c.getSystemService(PowerManager::class.java).isDeviceIdleMode) "ВКЛ" else "выкл"
                PowerManager.ACTION_POWER_SAVE_MODE_CHANGED ->
                    "🔋 Режим экономии: " +
                        if (c.getSystemService(PowerManager::class.java).isPowerSaveMode) "ВКЛ" else "выкл"
                else -> return
            }
            EventLog.add(c, text)
        }
    }

    /** Лог датчика приближения: когда телефон лежит экраном вниз, он «закрыт». */
    private val proximityListener = object : SensorEventListener {
        override fun onSensorChanged(e: SensorEvent) {
            val range = e.sensor.maximumRange
            val near = e.values[0] < range.coerceAtMost(5f)
            if (near != proximityNear) {
                proximityNear = near
                EventLog.add(this@ScreenOffService, "👁 датчик приближения: " + if (near) "ЗАКРЫТ (экраном вниз/в кармане)" else "открыт")
            }
        }

        override fun onAccuracyChanged(s: Sensor?, a: Int) {}
    }

    /** Каждые 30 с шлём книге «ping»: она держит Wi-Fi и не засыпает, пока телефон на связи. */
    private val pinger = object : Runnable {
        override fun run() {
            UdpSender.send(this@ScreenOffService, "ping", quiet = true)
            handler.postDelayed(this, PING_EVERY_MS)
        }
    }

    private val checker = object : Runnable {
        override fun run() {
            val left = ReadingMode.remainingMs(this@ScreenOffService)
            if (left != null && left <= 0L) {
                EventLog.add(this@ScreenOffService, "Автоотключение по бездействию")
                stopSelf()
                return
            }
            tick++
            health(logIt = tick % 4 == 0) // раз в минуту пишем «пульс»
            handler.postDelayed(this, CHECK_EVERY_MS)
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            EventLog.add(this, "Остановлено кнопкой в уведомлении")
            stopSelf()
            return START_NOT_STICKY
        }
        return START_STICKY
    }

    override fun onCreate() {
        super.onCreate()
        EventLog.add(
            this,
            "▶ Режим чтения включён (Android ${Build.VERSION.SDK_INT}, ${Build.MANUFACTURER}; " +
                "аудиопоток=${Prefs.audioStream(this)}, не гасить экран=${Prefs.keepScreen(this)})"
        )
        startAsForeground()
        startSession()
        if (Prefs.audioStream(this)) startSilence() else EventLog.add(this, "аудиопоток отключён настройкой")

        val wm = applicationContext.getSystemService(WifiManager::class.java)
        wifiLock = wm.createWifiLock(
            if (Build.VERSION.SDK_INT >= 29) WifiManager.WIFI_MODE_FULL_LOW_LATENCY
            else WifiManager.WIFI_MODE_FULL_HIGH_PERF,
            "pageturn:wifi"
        ).apply { acquire() }

        val pm = getSystemService(PowerManager::class.java)
        cpuLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "pageturn:cpu").apply { acquire() }

        if (Prefs.keepScreen(this)) {
            screenLock = pm.newWakeLock(
                PowerManager.SCREEN_DIM_WAKE_LOCK or PowerManager.ON_AFTER_RELEASE, "pageturn:screen"
            ).apply { acquire() }
            EventLog.add(this, "экран: удерживаю включённым (тусклый)")
        }

        sensorManager = getSystemService(SensorManager::class.java)?.also { sm ->
            sm.getDefaultSensor(Sensor.TYPE_PROXIMITY)?.let {
                sm.registerListener(proximityListener, it, SensorManager.SENSOR_DELAY_NORMAL)
            }
        }

        val filter = IntentFilter().apply {
            addAction(Intent.ACTION_SCREEN_OFF)
            addAction(Intent.ACTION_SCREEN_ON)
            addAction(Intent.ACTION_USER_PRESENT)
            addAction(PowerManager.ACTION_DEVICE_IDLE_MODE_CHANGED)
            addAction(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED)
        }
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(screenReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(screenReceiver, filter)
        }

        // Базовая громкость медиа: середина шкалы, чтобы было куда двигаться вверх и вниз.
        val am = getSystemService(AudioManager::class.java)
        audioMgr = am
        try {
            savedVolume = am.getStreamVolume(AudioManager.STREAM_MUSIC)
            val max = am.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
            lockedVolume = (max / 2).coerceAtLeast(1)
            am.setStreamVolume(AudioManager.STREAM_MUSIC, lockedVolume, 0)
            EventLog.add(this, "громкость медиа: была $savedVolume, база $lockedVolume из $max")
        } catch (e: Exception) {
            lockedVolume = -1
            EventLog.add(this, "⚠ не удалось выставить базовую громкость: ${e.message}")
        }
        val volFilter = IntentFilter("android.media.VOLUME_CHANGED_ACTION")
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(volumeReceiver, volFilter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(volumeReceiver, volFilter)
        }

        ReadingMode.active = true
        ReadingMode.touch()
        handler.postDelayed(checker, CHECK_EVERY_MS)
        handler.postDelayed(pinger, 1_000)
        refreshTile()
    }

    private fun startAsForeground() {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL, getString(R.string.notif_channel), NotificationManager.IMPORTANCE_LOW)
        )
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE
        )
        val stop = PendingIntent.getService(
            this, 1,
            Intent(this, ScreenOffService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE
        )
        val dark = PendingIntent.getActivity(
            this, 2, Intent(this, DarkActivity::class.java), PendingIntent.FLAG_IMMUTABLE
        )
        val min = Prefs.autoStopMin(this)
        val text = if (min > 0) getString(R.string.notif_text_auto, min) else getString(R.string.notif_text)
        val n = Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_notif)
            .setContentTitle(getString(R.string.notif_title))
            .setContentText(text)
            .setContentIntent(open)
            .setOngoing(true)
            .addAction(
                Notification.Action.Builder(
                    Icon.createWithResource(this, R.drawable.ic_notif), getString(R.string.notif_dark), dark
                ).build()
            )
            .addAction(
                Notification.Action.Builder(
                    Icon.createWithResource(this, R.drawable.ic_notif), getString(R.string.notif_stop), stop
                ).build()
            )
            .build()
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
        } else {
            startForeground(NOTIF_ID, n)
        }
    }

    private fun playingState(): PlaybackState =
        PlaybackState.Builder()
            .setActions(PlaybackState.ACTION_PLAY or PlaybackState.ACTION_PAUSE)
            .setState(PlaybackState.STATE_PLAYING, PlaybackState.PLAYBACK_POSITION_UNKNOWN, 1f)
            .build()

    private fun startSession() {
        val s = MediaSession(this, "PageTurn")
        s.setFlags(
            MediaSession.FLAG_HANDLES_MEDIA_BUTTONS or MediaSession.FLAG_HANDLES_TRANSPORT_CONTROLS
        )
        s.setPlaybackState(playingState())
        s.setPlaybackToRemote(object : VolumeProvider(VOLUME_CONTROL_RELATIVE, 100, 50) {
            override fun onAdjustVolume(direction: Int) {
                handle(direction, "медиасессия")
            }
        })
        s.isActive = true
        session = s
        EventLog.add(this, "медиасессия создана, active=${s.isActive}")
    }

    private fun handle(direction: Int, source: String) {
        if (direction == 0) return // отпускание кнопки (ADJUST_SAME)
        val now = SystemClock.elapsedRealtime()
        if (now - lastTs < MIN_INTERVAL_MS) return
        val (action, name) = if (direction > 0) Prefs.volUp(this) to "Громкость +"
        else Prefs.volDown(this) to "Громкость −"
        val cmd = Prefs.commandFor(action) ?: return
        lastTs = now
        ReadingMode.touch()
        EventLog.add(this, "⌨ [$source] $name → $cmd")
        UdpSender.send(this, cmd)
    }

    private fun startSilence() {
        val sampleRate = 8000
        val min = AudioTrack.getMinBufferSize(
            sampleRate, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT
        )
        val t = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setSampleRate(sampleRate)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .build()
            )
            .setBufferSizeInBytes(min * 2)
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()
        track = t
        alive = true
        t.play()
        val app = applicationContext
        Thread {
            val buf = ShortArray(2048)
            var seed = 12345
            while (alive) {
                // амплитуда ±3 из 32768 (≈ −80 дБ): не слышно, но и не «чистая тишина»
                for (i in buf.indices) {
                    seed = seed * 1103515245 + 12345
                    buf[i] = (((seed ushr 16) and 7) - 3).toShort()
                }
                if (t.write(buf, 0, buf.size) < 0) {
                    if (!alive) break
                    EventLog.add(app, "⚠ запись в аудиопоток не удалась, пробую перезапустить")
                    try { Thread.sleep(1000) } catch (_: InterruptedException) {}
                    try { t.play() } catch (_: Exception) {}
                }
            }
        }.start()
        EventLog.add(this, "аудиопоток запущен")
    }

    /** Проверка и самовосстановление: поток, сессия; опционально запись «пульса». */
    private fun health(logIt: Boolean) {
        val t = track
        val playing = t?.playState == AudioTrack.PLAYSTATE_PLAYING
        if (t != null && !playing) {
            try { t.play() } catch (_: Exception) {}
            EventLog.add(this, "⚠ аудиопоток был остановлен системой — перезапускаю")
        }
        session?.let {
            it.isActive = true
            it.setPlaybackState(playingState())
        }
        if (logIt) {
            val am = getSystemService(AudioManager::class.java)
            EventLog.add(
                this,
                "пульс: аудио=${if (t == null) "выкл" else if (playing) "играет" else "СТОП"}, " +
                    "сессия=${session?.isActive}, музыка=${am.isMusicActive}, " +
                    "cpu-lock=${cpuLock?.isHeld}"
            )
        }
    }

    private fun refreshTile() {
        try {
            TileService.requestListeningState(
                this, ComponentName(this, ReadingTileService::class.java)
            )
        } catch (_: Exception) {
        }
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        EventLog.add(this, "приложение смахнули из недавних — режим чтения продолжается")
        super.onTaskRemoved(rootIntent)
    }

    override fun onDestroy() {
        EventLog.add(this, "■ Режим чтения выключен")
        handler.removeCallbacks(checker)
        handler.removeCallbacks(pinger)
        try { sensorManager?.unregisterListener(proximityListener) } catch (_: Exception) {}
        screenLock?.let { if (it.isHeld) it.release() }
        screenLock = null
        alive = false
        ReadingMode.active = false
        try { unregisterReceiver(screenReceiver) } catch (_: Exception) {}
        try { unregisterReceiver(volumeReceiver) } catch (_: Exception) {}
        lockedVolume = -1
        if (savedVolume >= 0) {
            try {
                audioMgr?.setStreamVolume(AudioManager.STREAM_MUSIC, savedVolume, 0)
            } catch (_: Exception) {
            }
        }
        try { track?.stop() } catch (_: Exception) {}
        track?.release()
        track = null
        session?.isActive = false
        session?.release()
        session = null
        wifiLock?.let { if (it.isHeld) it.release() }
        wifiLock = null
        cpuLock?.let { if (it.isHeld) it.release() }
        cpuLock = null
        refreshTile()
        super.onDestroy()
    }
}
