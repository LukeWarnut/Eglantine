// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.dialogs

import android.app.Activity
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.network.DefaultDisplayNames

/**
 * Asks a player whose nickname is still one the emulator ships as a default to change it before
 * joining or hosting a room.
 */
object DefaultNicknameDialog {
    fun confirm(
        activity: Activity,
        nickname: String,
        onChangeNickname: () -> Unit,
        onContinue: () -> Unit
    ) {
        val canonicalName = DefaultDisplayNames.canonical(nickname)
        if (canonicalName == null) {
            onContinue()
            return
        }

        MaterialAlertDialogBuilder(activity)
            .setTitle(R.string.multiplayer_default_nickname_title)
            .setMessage(
                activity.getString(R.string.multiplayer_default_nickname_message, canonicalName)
            )
            .setPositiveButton(R.string.multiplayer_change_nickname) { _, _ -> onChangeNickname() }
            .setNeutralButton(R.string.multiplayer_continue_anyway) { _, _ -> onContinue() }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}
