package com.dkirs.pageturn

import android.accessibilityservice.AccessibilityService
import android.view.KeyEvent
import android.view.accessibility.AccessibilityEvent

/**
 * Перехватывает кнопки громкости (в том числе на экране блокировки, пока экран включён),
 * но ТОЛЬКО пока включён «Режим чтения». Иначе пропускает нажатия — громкость обычная.
 *
 * Важно: при погашенном экране Android не передаёт клавиши службам спец. возможностей —
 * там работает только медиасессия из [ScreenOffService].
 */
class PageTurnAccessibilityService : AccessibilityService() {

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {}

    override fun onInterrupt() {}

    override fun onKeyEvent(event: KeyEvent): Boolean {
        if (!ReadingMode.active) return false

        val (action, name) = when (event.keyCode) {
            KeyEvent.KEYCODE_VOLUME_UP -> Prefs.volUp(this) to "Громкость +"
            KeyEvent.KEYCODE_VOLUME_DOWN -> Prefs.volDown(this) to "Громкость −"
            else -> return false
        }
        val cmd = Prefs.commandFor(action) ?: return false

        // Реагируем только на первое нажатие, автоповтор при удержании игнорируем.
        if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
            ReadingMode.touch()
            EventLog.add(this, "⌨ [спец. возможности] $name → $cmd")
            UdpSender.send(this, cmd)
        }
        return true // гасим и DOWN, и UP, чтобы не менялась громкость
    }
}
