// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QStringList>
#include <QWidget>

#include "yuzu/game/game_details_info.h"

class QAbstractButton;
class QButtonGroup;
class QRadioButton;
class QVBoxLayout;

/// A game's updates, DLC and mods. At most one update runs, so updates are a radio group with
/// the base game as the "no update" choice; DLC and mods are independent checkboxes. Every
/// change emits the full list of disabled entries.
class AddOnList : public QWidget {
    Q_OBJECT

public:
    explicit AddOnList(QWidget* parent = nullptr);

    /// Shows a loading note until `loaded`.
    void SetAddOns(const std::vector<GameAddOn>& add_ons, bool loaded);
    /// The add-ons with the user's latest choices applied.
    const std::vector<GameAddOn>& AddOns() const {
        return add_ons;
    }

signals:
    void DisabledChanged(const QStringList& disabled);

private:
    void AddNote(const QString& text);
    void AddRow(QAbstractButton* button, const QString& version);
    void OnChanged();

    std::vector<GameAddOn> add_ons;
    /// One per add-on, in the same order.
    std::vector<QAbstractButton*> buttons;
    QRadioButton* base_game = nullptr;
    QButtonGroup* update_group = nullptr;
    QVBoxLayout* layout = nullptr;
};
