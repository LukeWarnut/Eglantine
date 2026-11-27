// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent>
#include "common/scm_rev.h"
#include "yuzu/configuration/update_settings_panel.h"
#include "yuzu/updater/update_dialog.h"

UpdateSettingsPanel::UpdateSettingsPanel(std::function<Choice()> choice, QWidget* parent)
    : QWidget(parent), m_choice{std::move(choice)} {
    m_check = new QPushButton(tr("Check for Updates"));
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->hide();

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_check);
    buttons->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(buttons);
    layout->addWidget(m_status);

    connect(m_check, &QPushButton::clicked, this, &UpdateSettingsPanel::CheckForUpdates);
    connect(&m_check_watcher, &QFutureWatcher<UpdateChecker::CheckResult>::finished, this,
            &UpdateSettingsPanel::OnChecked);
}

UpdateSettingsPanel::~UpdateSettingsPanel() {
    m_check_watcher.waitForFinished();
}

void UpdateSettingsPanel::CheckForUpdates() {
    const auto choice = m_choice();
    m_checked_channel = choice.channel;
    m_check->setEnabled(false);
    m_status->setText(tr("Checking..."));
    m_status->show();
    m_check_watcher.setFuture(QtConcurrent::run(
        [channel = UpdateChecker::ChannelOfSetting(choice.channel),
         platform = UpdateChecker::DownloadPlatform(choice.variant)] {
            return UpdateChecker::Check(channel, platform);
        }));
}

void UpdateSettingsPanel::OnChecked() {
    m_check->setEnabled(true);
    const auto result = m_check_watcher.result();
    const auto channel = UpdateChecker::ChannelOfSetting(m_checked_channel);
    switch (result.failure) {
    case UpdateChecker::Failure::None:
        break;
    case UpdateChecker::Failure::NotReleaseBuild:
        m_status->setText(tr("This build is not a release or nightly build, so it cannot update "
                             "itself."));
        return;
    case UpdateChecker::Failure::RateLimited:
        m_status->setText(tr("GitHub is limiting requests from your network right now. Please "
                             "try again later."));
        return;
    case UpdateChecker::Failure::Http:
        m_status->setText(
            tr("GitHub answered with HTTP error %1. Please try again later.").arg(result.status));
        return;
    default:
        m_status->setText(
            tr("Could not reach GitHub. Check your internet connection and try again."));
        return;
    }
    if (!result.update) {
        m_status->setText(tr("You are running the newest %1 build (%2).")
                              .arg(UpdateDialog::ChannelName(channel),
                                   QString::fromStdString(Common::BuildDisplayName())));
        return;
    }
    m_status->setText(tr("%1 is available.").arg(QString::fromStdString(result.update->title)));
    UpdateDialog dialog(*result.update, channel, this);
    dialog.exec();
}
