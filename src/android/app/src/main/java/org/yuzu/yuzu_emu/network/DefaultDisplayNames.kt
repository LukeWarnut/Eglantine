// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.network

/**
 * Names that ship as an emulator's own default, never as a user's choice.
 *
 * Port of the desktop lobby guard (NetworkMessage::GetDefaultDisplayName) so a room is never
 * joined with a stock name: the nickname is what every other player sees.
 */
object DefaultDisplayNames {
    private val defaultNames = listOf(
        "Eglantine",
        "Eden",
        "Lichii",
        "Suyu",
        "Citron",
        "Yuzu",
        "Torzu",
        "Sudachii"
    )

    /**
     * Returns the canonical default name when [name] is one of them, matching trimmed and
     * case-insensitively, or null when it is a name the user picked themselves.
     */
    fun canonical(name: String?): String? {
        val trimmed = name?.trim().orEmpty()
        if (trimmed.isEmpty()) {
            return null
        }
        return defaultNames.firstOrNull { it.equals(trimmed, ignoreCase = true) }
    }

    fun isDefault(name: String?): Boolean = canonical(name) != null
}
