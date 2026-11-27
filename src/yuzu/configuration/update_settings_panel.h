// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <QFutureWatcher>
#include <QWidget>
#include "common/settings_enums.h"
#include "frontend_common/update_checker.h"

class QLabel;
class QPushButton;

// The button of the Updates settings that checks the chosen release channel now.
class UpdateSettingsPanel : public QWidget {
    Q_OBJECT

public:
    struct Choice {
        Settings::UpdateChannel channel;
        Settings::UpdateVariant variant;
    };

    // `choice` gives the release channel and build type shown in the settings, applied or not.
    explicit UpdateSettingsPanel(std::function<Choice()> choice, QWidget* parent = nullptr);
    ~UpdateSettingsPanel() override;

private:
    void CheckForUpdates();
    void OnChecked();

    std::function<Choice()> m_choice;
    QLabel* m_status;
    QPushButton* m_check;

    QFutureWatcher<UpdateChecker::CheckResult> m_check_watcher;
    Settings::UpdateChannel m_checked_channel{Settings::UpdateChannel::Stable};
};
