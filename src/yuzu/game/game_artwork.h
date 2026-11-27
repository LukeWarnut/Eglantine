// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include "common/common_types.h"

class IconSearch;
class QMenu;
class QWidget;

/// The artwork actions every game view offers: find a banner or icon on SteamGridDB, choose an
/// image file, or go back to the original. Fills menus on demand and saves what the user picks.
class GameArtwork : public QObject {
    Q_OBJECT

public:
    /// `dialog_parent` owns the file dialogs, message boxes and the SteamGridDB picker.
    explicit GameArtwork(QWidget* dialog_parent);

    /// Rebuilds `menu` with the actions for a game's banner (`hero`) or icon.
    void FillMenu(QMenu* menu, bool hero, u64 program_id, const QString& title);
    bool IsBusy() const;

signals:
    void IconChanged(u64 program_id);
    void HeroChanged(u64 program_id);
    /// A SteamGridDB search started or finished.
    void BusyChanged(bool busy);

private:
    void Find(bool hero, u64 program_id, const QString& title);
    void ChooseFile(bool hero, u64 program_id);
    void Reset(bool hero, u64 program_id);
    void SaveHero(u64 program_id, const QByteArray& image);
    void SaveIcon(u64 program_id, const QByteArray& png);

    QWidget* dialog_parent;
    IconSearch* icon_search;
    u64 search_program_id = 0;
};
