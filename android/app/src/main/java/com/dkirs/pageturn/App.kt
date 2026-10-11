package com.dkirs.pageturn

import android.app.Application
import android.content.Context
import androidx.appcompat.app.AppCompatDelegate

class App : Application() {
    override fun onCreate() {
        super.onCreate()
        Prefs.migrateLegacySecret(this)
        applyTheme(this)
    }

    companion object {
        /** Применяет сохранённый выбор темы: системная / светлая / тёмная. */
        fun applyTheme(c: Context) {
            AppCompatDelegate.setDefaultNightMode(
                when (Prefs.theme(c)) {
                    Prefs.THEME_LIGHT -> AppCompatDelegate.MODE_NIGHT_NO
                    Prefs.THEME_DARK -> AppCompatDelegate.MODE_NIGHT_YES
                    else -> AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM
                }
            )
        }
    }
}
