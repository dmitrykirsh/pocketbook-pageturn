package com.dkirs.pageturn

import android.Manifest
import android.content.ClipData
import android.content.ClipboardManager
import android.content.ComponentName
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.net.Uri
import android.widget.ArrayAdapter
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.journeyapps.barcodescanner.ScanContract
import com.journeyapps.barcodescanner.ScanOptions
import com.google.android.material.button.MaterialButton
import com.google.android.material.button.MaterialButtonToggleGroup
import com.google.android.material.card.MaterialCardView
import com.google.android.material.color.MaterialColors
import com.google.android.material.materialswitch.MaterialSwitch
import com.google.android.material.textfield.MaterialAutoCompleteTextView
import com.google.android.material.textfield.TextInputEditText

class MainActivity : AppCompatActivity() {

    private lateinit var cardReading: MaterialCardView
    private lateinit var tvReadingStatus: TextView
    private lateinit var switchReading: MaterialSwitch
    private lateinit var etHost: TextInputEditText
    private lateinit var etPort: TextInputEditText
    private lateinit var etPin: TextInputEditText
    private lateinit var tvTestStatus: TextView
    private lateinit var btnFind: MaterialButton
    private lateinit var ddUp: MaterialAutoCompleteTextView
    private lateinit var ddDown: MaterialAutoCompleteTextView
    private lateinit var ddAuto: MaterialAutoCompleteTextView
    private lateinit var tvA11y: TextView
    private lateinit var toggleTheme: MaterialButtonToggleGroup
    private lateinit var tvLog: TextView
    private var tickCount = 0

    private val handler = Handler(Looper.getMainLooper())
    private var programmatic = false

    private val actionLabels by lazy {
        listOf(getString(R.string.act_next), getString(R.string.act_prev), getString(R.string.act_none))
    }
    private val actionValues = listOf(Prefs.NEXT, Prefs.PREV, Prefs.NONE)
    private val autoLabels by lazy {
        listOf(
            getString(R.string.auto_15), getString(R.string.auto_30), getString(R.string.auto_60),
            getString(R.string.auto_120), getString(R.string.auto_never)
        )
    }
    private val autoValues = listOf(15, 30, 60, 120, 0)

    private var volUp = Prefs.PREV
    private var volDown = Prefs.NEXT

    private val scanLauncher = registerForActivityResult(ScanContract()) { result ->
        result.contents?.let { handleLink(it) }
    }

    private val ticker = object : Runnable {
        override fun run() {
            refresh()
            if (tickCount++ % 2 == 0) refreshLog()
            handler.postDelayed(this, 1000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        cardReading = findViewById(R.id.cardReading)
        tvReadingStatus = findViewById(R.id.tvReadingStatus)
        switchReading = findViewById(R.id.switchReading)
        etHost = findViewById(R.id.etHost)
        etPort = findViewById(R.id.etPort)
        etPin = findViewById(R.id.etPin)
        tvTestStatus = findViewById(R.id.tvTestStatus)
        btnFind = findViewById(R.id.btnFind)
        ddUp = findViewById(R.id.ddUp)
        ddDown = findViewById(R.id.ddDown)
        ddAuto = findViewById(R.id.ddAuto)
        tvA11y = findViewById(R.id.tvA11y)
        toggleTheme = findViewById(R.id.toggleTheme)
        tvLog = findViewById(R.id.tvLog)

        findViewById<MaterialButton>(R.id.btnLogClear).setOnClickListener {
            EventLog.clear(this)
            refreshLog()
        }
        findViewById<MaterialButton>(R.id.btnLogCopy).setOnClickListener {
            val cm = getSystemService(ClipboardManager::class.java)
            cm.setPrimaryClip(ClipData.newPlainText("PageTurn log", EventLog.tail(this, 200)))
            tvTestStatus.text = getString(R.string.log_copied)
        }

        etHost.setText(Prefs.host(this))
        etPort.setText(Prefs.port(this).toString())
        etPin.setText(Prefs.pin(this))
        if (savedInstanceState == null) intent?.data?.let { handleLink(it.toString()) }   // запуск по ссылке pageturn://

        volUp = Prefs.volUp(this)
        volDown = Prefs.volDown(this)
        setupActionDropdown(ddUp, { volUp }) { volUp = it; Prefs.setVolUp(this, it) }
        setupActionDropdown(ddDown, { volDown }) { volDown = it; Prefs.setVolDown(this, it) }

        findViewById<MaterialButton>(R.id.btnSwap).setOnClickListener {
            val t = volUp; volUp = volDown; volDown = t
            Prefs.setVolUp(this, volUp); Prefs.setVolDown(this, volDown)
            ddUp.setText(actionLabels[actionValues.indexOf(volUp)], false)
            ddDown.setText(actionLabels[actionValues.indexOf(volDown)], false)
        }

        val autoIdx = autoValues.indexOf(Prefs.autoStopMin(this)).coerceAtLeast(0)
        ddAuto.setAdapter(ArrayAdapter(this, android.R.layout.simple_list_item_1, autoLabels))
        ddAuto.setText(autoLabels[autoIdx], false)
        ddAuto.setOnItemClickListener { _, _, pos, _ -> Prefs.setAutoStopMin(this, autoValues[pos]) }

        btnFind.setOnClickListener { findBook() }
        findViewById<MaterialButton>(R.id.btnScan).setOnClickListener {
            saveConnection()
            scanLauncher.launch(
                ScanOptions()
                    .setDesiredBarcodeFormats(ScanOptions.QR_CODE)
                    .setPrompt(getString(R.string.scan_prompt))
                    .setBeepEnabled(false)
                    .setOrientationLocked(false)
            )
        }
        findViewById<MaterialButton>(R.id.btnDark).setOnClickListener {
            saveConnection()
            startActivity(Intent(this, DarkActivity::class.java))
        }
        findViewById<MaterialButton>(R.id.btnTestPrev).setOnClickListener { test("prev") }
        findViewById<MaterialButton>(R.id.btnTestNext).setOnClickListener { test("next") }
        findViewById<MaterialButton>(R.id.btnA11y).setOnClickListener {
            startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
        }

        val switchAudio = findViewById<MaterialSwitch>(R.id.switchAudio)
        val switchKeepScreen = findViewById<MaterialSwitch>(R.id.switchKeepScreen)
        switchAudio.isChecked = Prefs.audioStream(this)
        switchKeepScreen.isChecked = Prefs.keepScreen(this)
        switchAudio.setOnCheckedChangeListener { _, on ->
            Prefs.setAudioStream(this, on)
            restartIfActive()
        }
        switchKeepScreen.setOnCheckedChangeListener { _, on ->
            Prefs.setKeepScreen(this, on)
            restartIfActive()
        }

        switchReading.setOnCheckedChangeListener { _, on ->
            if (programmatic) return@setOnCheckedChangeListener
            saveConnection()
            if (on) startReading() else ReadingMode.stop(this)
            handler.postDelayed({ refresh() }, 300)
        }

        programmatic = true
        toggleTheme.check(
            when (Prefs.theme(this)) {
                Prefs.THEME_LIGHT -> R.id.btnThemeLight
                Prefs.THEME_DARK -> R.id.btnThemeDark
                else -> R.id.btnThemeSystem
            }
        )
        programmatic = false
        toggleTheme.addOnButtonCheckedListener { _, id, checked ->
            if (!checked || programmatic) return@addOnButtonCheckedListener
            Prefs.setTheme(
                this, when (id) {
                    R.id.btnThemeLight -> Prefs.THEME_LIGHT
                    R.id.btnThemeDark -> Prefs.THEME_DARK
                    else -> Prefs.THEME_SYSTEM
                }
            )
            saveConnection()
            App.applyTheme(this) // пересоздаст экран с новой темой
        }
    }

    private fun setupActionDropdown(
        view: MaterialAutoCompleteTextView,
        current: () -> Int,
        onPick: (Int) -> Unit
    ) {
        view.setAdapter(ArrayAdapter(this, android.R.layout.simple_list_item_1, actionLabels))
        view.setText(actionLabels[actionValues.indexOf(current()).coerceAtLeast(0)], false)
        view.setOnItemClickListener { _, _, pos, _ -> onPick(actionValues[pos]) }
    }

    private fun saveConnection() {
        Prefs.setConnection(
            this,
            etHost.text?.toString().orEmpty(),
            etPort.text?.toString()?.toIntOrNull() ?: 57321,
            etPin.text?.toString().orEmpty()
        )
    }

    private fun test(cmd: String) {
        saveConnection()
        tvTestStatus.text = getString(R.string.sending)
        UdpSender.send(this, cmd) { msg -> runOnUiThread { tvTestStatus.text = msg } }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        intent.data?.let { handleLink(it.toString()) }
    }

    /**
     * Ссылка из QR (или системной камеры): pageturn://c?h=<адрес>&p=<порт>&k=<PIN>.
     * Сохраняем настройки и сразу проверяем связь (книга отвечает "PTOK" на верный PIN).
     */
    private fun handleLink(text: String) {
        val uri = try { Uri.parse(text.trim()) } catch (_: Exception) { null }
        val host = uri?.getQueryParameter("h").orEmpty()
        val port = uri?.getQueryParameter("p")?.toIntOrNull() ?: 0
        val pin = uri?.getQueryParameter("k").orEmpty()
        if (uri == null || uri.scheme != "pageturn" || host.isBlank() || port !in 1..65535 ||
            pin.length !in 4..8 || !pin.all { it.isDigit() }
        ) {
            tvTestStatus.text = getString(R.string.scan_bad)
            return
        }
        etHost.setText(host)
        etPort.setText(port.toString())
        etPin.setText(pin)
        saveConnection()
        tvTestStatus.text = getString(R.string.connecting)
        Thread {
            val ok = Discovery.verify(host, port, pin)
            runOnUiThread {
                if (isFinishing || isDestroyed) return@runOnUiThread
                tvTestStatus.text = if (ok) getString(R.string.connected_fmt, host)
                else getString(R.string.connect_failed)
            }
        }.start()
    }

    /** Автопоиск книги в сети (дом. Wi-Fi или точка доступа телефона). */
    private fun findBook() {
        saveConnection()
        tvTestStatus.text = getString(R.string.searching)
        btnFind.isEnabled = false
        Thread {
            val books = Discovery.find(applicationContext)
            runOnUiThread {
                if (isFinishing || isDestroyed) return@runOnUiThread
                btnFind.isEnabled = true
                if (books.isEmpty()) {
                    tvTestStatus.text = getString(R.string.not_found)
                } else {
                    val b = books[0]
                    etHost.setText(b.host)
                    etPort.setText(b.port.toString())
                    saveConnection()
                    tvTestStatus.text = if (books.size == 1)
                        getString(R.string.found_fmt, "${b.name} (${b.host})")
                    else getString(R.string.found_many_fmt, books.size, b.host)
                }
            }
        }.start()
    }

    private fun startReading() {
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
        ReadingMode.start(this)
    }

    private fun a11yEnabled(): Boolean {
        val cn = ComponentName(this, PageTurnAccessibilityService::class.java)
        val enabled = Settings.Secure.getString(
            contentResolver, Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES
        ) ?: return false
        return enabled.split(':').any {
            it.equals(cn.flattenToString(), true) || it.equals(cn.flattenToShortString(), true)
        }
    }

    /** Настройки службы применяются при её запуске — перезапускаем, если режим чтения включён. */
    private fun restartIfActive() {
        if (!ReadingMode.active) return
        ReadingMode.stop(this)
        handler.postDelayed({ startReading() }, 700)
    }

    private fun refreshLog() {
        val t = EventLog.tail(this, 30)
        tvLog.text = if (t.isBlank()) getString(R.string.log_empty) else t
    }

    private fun formatLeft(ms: Long): String {
        val m = ((ms + 59_999) / 60_000).toInt()
        return if (m >= 60) getString(R.string.left_hm_fmt, m / 60, m % 60)
        else getString(R.string.left_min_fmt, m)
    }

    private fun refresh() {
        val active = ReadingMode.active
        programmatic = true
        switchReading.isChecked = active
        programmatic = false

        tvReadingStatus.text = if (active) {
            val left = ReadingMode.remainingMs(this)
            getString(R.string.reading_on) +
                (left?.let { "\n" + getString(R.string.reading_auto_off, formatLeft(it)) } ?: "")
        } else {
            getString(R.string.reading_off)
        }
        cardReading.setCardBackgroundColor(
            MaterialColors.getColor(
                cardReading,
                if (active) com.google.android.material.R.attr.colorPrimaryContainer
                else com.google.android.material.R.attr.colorSurfaceContainerHigh
            )
        )

        tvA11y.text = getString(if (a11yEnabled()) R.string.a11y_on else R.string.a11y_off)
    }

    override fun onResume() {
        super.onResume()
        handler.post(ticker)
    }

    override fun onPause() {
        handler.removeCallbacks(ticker)
        saveConnection()
        super.onPause()
    }
}
