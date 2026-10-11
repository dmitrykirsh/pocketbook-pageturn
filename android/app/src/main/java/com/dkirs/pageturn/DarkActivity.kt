package com.dkirs.pageturn

import android.app.Activity
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.TextView

/**
 * «Тёмный экран»: чёрная страница с минимальной яркостью, которая не даёт устройству уснуть.
 * Android доставляет клавиши только «бодрствующему» устройству, поэтому так кнопки громкости
 * работают, а экран при этом выглядит выключенным (на AMOLED чёрные пиксели не светятся).
 * Касания игнорируются; выйти — удержать палец 2 секунды (или Назад).
 */
class DarkActivity : Activity() {

    private val handler = Handler(Looper.getMainLooper())
    private lateinit var hint: TextView

    private val exitRunnable = Runnable { finish() }
    private val hideHint = Runnable { hint.visibility = View.INVISIBLE }

    /** Режим чтения закончился (автоотключение / «Остановить») — закрываем и тёмный экран. */
    private val watcher = object : Runnable {
        override fun run() {
            if (!ReadingMode.active) {
                finish()
                return
            }
            handler.postDelayed(this, 2000)
        }
    }

    @Suppress("DEPRECATION")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        if (Build.VERSION.SDK_INT >= 27) {
            setShowWhenLocked(true)
            setTurnScreenOn(true)
        } else {
            window.addFlags(
                WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED or
                    WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON
            )
        }
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.attributes = window.attributes.apply {
            screenBrightness = WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_OFF // минимум
        }

        val root = FrameLayout(this).apply { setBackgroundColor(Color.BLACK) }
        hint = TextView(this).apply {
            text = getString(R.string.dark_hint)
            setTextColor(0xFF3A3A3A.toInt())
            gravity = Gravity.CENTER
            textSize = 14f
        }
        root.addView(
            hint,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT
            )
        )
        setContentView(root)
        hideSystemBars()

        if (!ReadingMode.active) ReadingMode.start(this)
        EventLog.add(this, "🌑 Тёмный экран открыт")
        handler.postDelayed(hideHint, 4000)
        handler.postDelayed(watcher, 4000) // даём службе время запуститься
    }

    @Suppress("DEPRECATION")
    private fun hideSystemBars() {
        if (Build.VERSION.SDK_INT >= 30) {
            window.insetsController?.let {
                it.hide(WindowInsets.Type.systemBars())
                it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION)
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                handler.removeCallbacks(hideHint)
                hint.visibility = View.VISIBLE
                handler.postDelayed(exitRunnable, 2000)
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                handler.removeCallbacks(exitRunnable)
                handler.postDelayed(hideHint, 1500)
            }
        }
        return true
    }

    /** Если служба спец. возможностей выключена, кнопки ловим здесь, пока этот экран в фокусе. */
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        val (action, name) = when (keyCode) {
            KeyEvent.KEYCODE_VOLUME_UP -> Prefs.volUp(this) to "Громкость +"
            KeyEvent.KEYCODE_VOLUME_DOWN -> Prefs.volDown(this) to "Громкость −"
            else -> return super.onKeyDown(keyCode, event)
        }
        val cmd = Prefs.commandFor(action)
        if (cmd != null && event.repeatCount == 0) {
            ReadingMode.touch()
            EventLog.add(this, "⌨ [тёмный экран] $name → $cmd")
            UdpSender.send(this, cmd)
        }
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN) return true
        return super.onKeyUp(keyCode, event)
    }

    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        EventLog.add(this, "🌑 Тёмный экран закрыт")
        super.onDestroy()
    }
}
