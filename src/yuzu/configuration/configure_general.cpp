// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <functional>
#include <utility>
#include <vector>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QListWidget>
#include <QMessageBox>
#include <QVBoxLayout>
#include "common/scm_rev.h"
#include "common/settings.h"
#include "core/core.h"
#include "qt_common/config/uisettings.h"
#include "ui_configure_general.h"
#include "yuzu/configuration/configuration_shared.h"
#include "yuzu/configuration/configure_general.h"
#include "yuzu/configuration/shared_widget.h"
#ifdef ENABLE_UPDATE_CHECKER
#include "frontend_common/update_checker.h"
#include "yuzu/configuration/experimental_builds_panel.h"
#include "yuzu/configuration/update_settings_panel.h"
#endif

ConfigureGeneral::ConfigureGeneral(const Core::System& system_,
                                   std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group_,
                                   const ConfigurationShared::Builder& builder, QWidget* parent)
    : Tab(group_, parent), ui{std::make_unique<Ui::ConfigureGeneral>()}, system{system_} {
    ui->setupUi(this);

    Setup(builder);

    SetConfiguration();

    connect(ui->button_reset_defaults, &QPushButton::clicked, this,
            &ConfigureGeneral::ResetDefaults);

    connect(ui->add_external_dir_button, &QPushButton::pressed, this,
            &ConfigureGeneral::AddExternalContentDirectory);
    connect(ui->remove_external_dir_button, &QPushButton::pressed, this,
            &ConfigureGeneral::RemoveSelectedExternalContentDirectory);
    connect(ui->external_content_list, &QListWidget::itemSelectionChanged, this, [this] {
        ui->remove_external_dir_button->setEnabled(
            !ui->external_content_list->selectedItems().isEmpty());
    });

    if (!Settings::IsConfiguringGlobal()) {
        ui->button_reset_defaults->setVisible(false);
    }
}

ConfigureGeneral::~ConfigureGeneral() = default;

void ConfigureGeneral::SetConfiguration() {
    UpdateExternalContentList();
}

void ConfigureGeneral::Setup(const ConfigurationShared::Builder& builder) {
    QLayout& general_layout = *ui->general_widget->layout();
    std::map<u32, QWidget*> general_hold{};
    std::map<u32, QWidget*> linux_hold{};
    std::vector<QWidget*> update_hold{};

    std::vector<Settings::BasicSetting*> settings;

    auto push = [&settings](auto& list) {
        for (auto setting : list) {
            settings.push_back(setting);
        }
    };

    push(UISettings::values.linkage.by_category[Settings::Category::UiGeneral]);
#ifdef ENABLE_UPDATE_CHECKER
    // Release candidates now belong to Stable, and "Auto" is no longer offered: show such values
    // as the options that stand for them.
    auto& channel = UISettings::values.update_channel;
    if (channel.GetValue() == Settings::UpdateChannel::Unset ||
        channel.GetValue() == Settings::UpdateChannel::ReleaseCandidate) {
        channel.SetValue(UpdateChecker::BuildChannel() == UpdateChecker::Channel::Daily
                             ? Settings::UpdateChannel::Daily
                             : Settings::UpdateChannel::Stable);
    }
    if (UISettings::values.update_variant.GetValue() == Settings::UpdateVariant::Auto) {
        UISettings::values.update_variant.SetValue(UpdateChecker::ThisBuildVariant());
    }
    ConfigurationShared::Widget* channel_widget{};
    ConfigurationShared::Widget* variant_widget{};
#endif
    for (const auto setting : settings) {
        const bool is_update_channel = setting->Id() == UISettings::values.update_channel.Id();
        const bool is_update_variant = setting->Id() == UISettings::values.update_variant.Id();
        const bool is_update_setting = is_update_channel || is_update_variant ||
                                       setting->Id() == UISettings::values.check_for_updates.Id();
#ifdef ENABLE_UPDATE_CHECKER
        if (is_update_variant && Common::g_build_release_platform[0] == '\0') {
            continue;
        }
#else
        if (is_update_setting) {
            continue;
        }
#endif

        auto* widget = builder.BuildWidget(setting, apply_funcs);

        if (widget == nullptr) {
            continue;
        }
        if (!widget->Valid()) {
            widget->deleteLater();
            continue;
        }

#ifdef ENABLE_UPDATE_CHECKER
        if (is_update_channel) {
            channel_widget = widget;
        } else if (is_update_variant) {
            variant_widget = widget;
        }
        if (is_update_channel) {
            widget->setToolTip(widget->toolTip() + QStringLiteral("\n\n") +
                               tr("Installed version: %1")
                                   .arg(QString::fromStdString(Common::BuildDisplayName())));
        }
#endif

        if (is_update_setting) {
            update_hold.push_back(widget);
            continue;
        }

        switch (setting->GetCategory()) {
        case Settings::Category::UiGeneral:
            general_hold.emplace(setting->Id(), widget);
            break;
        default:
            widget->deleteLater();
        }
    }

    for (const auto& [id, widget] : general_hold) {
        general_layout.addWidget(widget);
    }

    for (auto* widget : update_hold) {
        ui->updates_widget->layout()->addWidget(widget);
    }
#ifdef ENABLE_UPDATE_CHECKER
    // Checks what the settings show, before they are applied.
    const auto choice = [channel_widget, variant_widget] {
        const auto selected = [](ConfigurationShared::Widget* widget, auto fallback) {
            using Enum = decltype(fallback);
            return widget && widget->combobox && widget->combobox->currentIndex() >= 0
                       ? static_cast<Enum>(widget->combobox->currentData().toUInt())
                       : fallback;
        };
        return UpdateSettingsPanel::Choice{
            selected(channel_widget, UISettings::values.update_channel.GetValue()),
            selected(variant_widget, UISettings::values.update_variant.GetValue())};
    };
    ui->updates_widget->layout()->addWidget(new UpdateSettingsPanel(choice, this));
#else
    if (update_hold.empty()) {
        ui->UpdatesGroupBox->setVisible(false);
        return;
    }
#endif

    // Updates get their own tab under General.
    updates_page = std::make_unique<QWidget>();
    auto* const updates_layout = new QVBoxLayout(updates_page.get());
    updates_layout->addWidget(ui->UpdatesGroupBox);
#ifdef ENABLE_UPDATE_CHECKER
    auto* experimental = new ExperimentalBuildsPanel;
    connect(experimental, &ExperimentalBuildsPanel::OpenVersionManagerRequested, this,
            &ConfigureGeneral::OpenVersionManagerRequested);
    connect(experimental, &ExperimentalBuildsPanel::LaunchRequested, this,
            &ConfigureGeneral::LaunchExperimentalBuildRequested);
    updates_layout->addWidget(experimental);
#endif
    updates_layout->addStretch();
    updates_page->setAccessibleName(tr("Updates"));
}

QWidget* ConfigureGeneral::UpdatesPage() const {
    return updates_page.get();
}

// Called to set the callback when resetting settings to defaults
void ConfigureGeneral::SetResetCallback(std::function<void()> callback) {
    reset_callback = std::move(callback);
}

void ConfigureGeneral::ResetDefaults() {
    QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Eglantine"),
        tr("This reset all settings and remove all per-game configurations. This will not delete "
           "game directories, profiles, or input profiles. Proceed?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::No) {
        return;
    }
    UISettings::values.reset_to_defaults = true;
    UISettings::values.is_game_list_reload_pending.exchange(true);
    reset_callback();
}


void ConfigureGeneral::ApplyConfiguration() {
    bool powered_on = system.IsPoweredOn();
    const auto previous_channel = UISettings::values.update_channel.GetValue();
    for (const auto& func : apply_funcs) {
        func(powered_on);
    }
    if (UISettings::values.update_channel.GetValue() != previous_channel) {
        UISettings::values.skipped_update.SetValue({});
    }

    std::vector<std::string> new_dirs;
    new_dirs.reserve(ui->external_content_list->count());
    for (int i = 0; i < ui->external_content_list->count(); ++i) {
        new_dirs.push_back(ui->external_content_list->item(i)->text().toStdString());
    }

    if (new_dirs != Settings::values.external_content_dirs) {
        Settings::values.external_content_dirs = std::move(new_dirs);
        emit ExternalContentDirsChanged();
    }
}

void ConfigureGeneral::UpdateExternalContentList() {
    ui->external_content_list->clear();
    for (const auto& dir : Settings::values.external_content_dirs) {
        ui->external_content_list->addItem(QString::fromStdString(dir));
    }
}

void ConfigureGeneral::AddExternalContentDirectory() {
    const QString dir_path = QFileDialog::getExistingDirectory(
        this, tr("Select External Content Directory..."), QString());

    if (dir_path.isEmpty()) {
        return;
    }

    QString normalized_path = QDir::toNativeSeparators(dir_path);
    if (normalized_path.back() != QDir::separator()) {
        normalized_path.append(QDir::separator());
    }

    for (int i = 0; i < ui->external_content_list->count(); ++i) {
        if (ui->external_content_list->item(i)->text() == normalized_path) {
            QMessageBox::information(this, tr("Directory Already Added"),
                                     tr("This directory is already in the list."));
            return;
        }
    }

    ui->external_content_list->addItem(normalized_path);
}

void ConfigureGeneral::RemoveSelectedExternalContentDirectory() {
    auto selected = ui->external_content_list->selectedItems();
    if (!selected.isEmpty()) {
        qDeleteAll(ui->external_content_list->selectedItems());
    }
}

void ConfigureGeneral::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureGeneral::RetranslateUI() {
    ui->retranslateUi(this);
    if (updates_page) {
        updates_page->setAccessibleName(tr("Updates"));
    }
}
