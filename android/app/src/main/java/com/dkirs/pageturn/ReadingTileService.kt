package com.dkirs.pageturn

import android.service.quicksettings.Tile
import android.service.quicksettings.TileService

/** Плитка в шторке быстрых настроек: включить/выключить «Режим чтения». */
class ReadingTileService : TileService() {

    override fun onStartListening() {
        render(ReadingMode.active)
    }

    override fun onClick() {
        val turnOn = !ReadingMode.active
        try {
            if (turnOn) ReadingMode.start(this) else ReadingMode.stop(this)
        } catch (_: Exception) {
            return
        }
        render(turnOn)
    }

    private fun render(on: Boolean) {
        val tile = qsTile ?: return
        tile.state = if (on) Tile.STATE_ACTIVE else Tile.STATE_INACTIVE
        tile.label = getString(R.string.tile_label)
        tile.updateTile()
    }
}
