// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include "frontend_common/tester_build.h"
#include "frontend_common/update_installer.h"
#include "yuzu/configuration/experimental_builds_panel.h"
#include "yuzu/updater/tester_build_launcher.h"

namespace fs = std::filesystem;

namespace {

constexpr int VisibleRows = 4;

QString PathText(const fs::path& path) {
    return QDir::toNativeSeparators(QString::fromStdString(TesterBuild::PathToUtf8(path)));
}

fs::path PathOf(const QString& text) {
    return TesterBuild::PathFromUtf8(QDir::cleanPath(QDir::fromNativeSeparators(text)).toStdString());
}

QString Title() {
    return QObject::tr("Experimental Builds");
}

} // namespace

ExperimentalBuildsPanel::ExperimentalBuildsPanel(QWidget* parent) : QWidget(parent) {
    m_folder = new QLineEdit;
    m_browse = new QPushButton(tr("Browse..."));
    auto* folder_row = new QHBoxLayout;
    folder_row->addWidget(new QLabel(tr("Folder")));
    folder_row->addWidget(m_folder, 1);
    folder_row->addWidget(m_browse);

    m_builds = new QTreeWidget;
    m_builds->setHeaderLabels({tr("Branch"), tr("Commit"), tr("Built"), tr("Status")});
    m_builds->setRootIsDecorated(false);
    m_builds->setSelectionMode(QAbstractItemView::SingleSelection);
    m_builds->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_builds->setToolTip(tr("Select a build to launch or delete it."));
    m_builds->header()->setStretchLastSection(false);
    m_builds->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 4; ++column) {
        m_builds->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    const int row_height = m_builds->fontMetrics().height() + 8;
    m_builds->setFixedHeight(m_builds->header()->sizeHint().height() + row_height * VisibleRows +
                             m_builds->frameWidth() * 2);
    m_empty = new QLabel(tr("No experimental builds are installed. Build one from Help > Version "
                            "Manager."));
    m_empty->setWordWrap(true);
    m_empty->setEnabled(false);

    // What can be done with the selected build.
    m_launch = new QPushButton(tr("Launch"));
    m_delete_one = new QPushButton(tr("Delete"));
    m_selection_buttons = new QWidget;
    auto* selection_row = new QHBoxLayout(m_selection_buttons);
    selection_row->setContentsMargins(0, 0, 0, 0);
    selection_row->addWidget(m_launch);
    selection_row->addWidget(m_delete_one);
    selection_row->addStretch();

    m_open = new QPushButton(tr("Open Version Manager..."));
    m_open->setToolTip(tr("Saves the settings and closes this window."));
    m_delete = new QPushButton(tr("Delete experimental builds"));
    auto* action_row = new QHBoxLayout;
    action_row->addWidget(m_open);
    action_row->addWidget(m_delete);
    action_row->addStretch();

    auto* builds_group = new QGroupBox(tr("Experimental Builds"));
    auto* builds_layout = new QVBoxLayout(builds_group);
    builds_layout->addLayout(folder_row);
    builds_layout->addWidget(m_builds);
    builds_layout->addWidget(m_empty);
    builds_layout->addWidget(m_selection_buttons);

    auto* actions_group = new QGroupBox(tr("Actions"));
    (new QVBoxLayout(actions_group))->addLayout(action_row);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(builds_group);
    layout->addWidget(actions_group);

    if (TesterLauncher::IsTesterBuild()) {
        const auto reason = tr("This is an experimental build and cannot move or delete itself. "
                               "Start the release build to do that.");
        for (QWidget* widget : {static_cast<QWidget*>(m_folder), static_cast<QWidget*>(m_browse),
                                static_cast<QWidget*>(m_delete)}) {
            widget->setEnabled(false);
            widget->setToolTip(reason);
        }
    } else if (!qEnvironmentVariableIsEmpty("EGLANTINE_TESTER_ROOT")) {
        const auto reason = tr("Set by the EGLANTINE_TESTER_ROOT environment variable.");
        for (QWidget* widget : {static_cast<QWidget*>(m_folder), static_cast<QWidget*>(m_browse)}) {
            widget->setEnabled(false);
            widget->setToolTip(reason);
        }
    }

    connect(m_browse, &QPushButton::clicked, this, &ExperimentalBuildsPanel::Browse);
    connect(m_folder, &QLineEdit::editingFinished, this,
            [this] { ChangeFolder(m_folder->text()); });
    connect(m_open, &QPushButton::clicked, this,
            &ExperimentalBuildsPanel::OpenVersionManagerRequested);
    connect(m_delete, &QPushButton::clicked, this, &ExperimentalBuildsPanel::DeleteBuilds);
    connect(m_builds, &QTreeWidget::itemSelectionChanged, this,
            &ExperimentalBuildsPanel::UpdateSelectionButtons);
    connect(m_builds, &QTreeWidget::itemDoubleClicked, this,
            &ExperimentalBuildsPanel::LaunchSelected);
    connect(m_launch, &QPushButton::clicked, this, &ExperimentalBuildsPanel::LaunchSelected);
    connect(m_delete_one, &QPushButton::clicked, this, &ExperimentalBuildsPanel::DeleteSelected);
    connect(&m_size_watcher, &QFutureWatcher<quint64>::finished, this, [this] {
        const auto size = m_size_watcher.result();
        m_delete->setText(size == 0 ? tr("Delete experimental builds")
                                    : tr("Delete experimental builds (%1)")
                                          .arg(QLocale().formattedDataSize(size)));
    });
    Refresh();
}

ExperimentalBuildsPanel::~ExperimentalBuildsPanel() {
    m_size_watcher.waitForFinished();
}

void ExperimentalBuildsPanel::Refresh() {
    const auto root = TesterLauncher::Root();
    m_folder->setText(PathText(root));

    const auto state = TesterBuild::LoadState(TesterLauncher::StateFile());
    m_installs = root.empty() ? std::vector<TesterBuild::InstallInfo>{}
                              : TesterBuild::ListInstalls(TesterBuild::MakeLayout(root), state);
    const auto& installs = m_installs;
    m_builds->clear();
    for (const auto& install : installs) {
        auto* item = new QTreeWidgetItem(m_builds);
        item->setText(0, install.source ? QString::fromStdString(install.source->DisplayName())
                                        : tr("Unknown branch"));
        item->setText(1, QString::fromStdString(install.sha.substr(0, 10)));
        item->setToolTip(1, QString::fromStdString(install.sha));
        if (install.built_at != 0) {
            item->setText(2, QLocale().toString(QDateTime::fromSecsSinceEpoch(install.built_at),
                                                QLocale::ShortFormat));
        }
        const bool current = !state.current_install.empty() &&
                             TesterBuild::PathFromUtf8(state.current_install) == install.dir;
        if (TesterLauncher::IsRunningInstall(install)) {
            item->setText(3, tr("Running"));
        } else if (current && state.start_tester) {
            item->setText(3, tr("Starts with Eglantine"));
        }
        if (current && state.start_tester) {
            QFont font = item->font(0);
            font.setBold(true);
            for (int column = 0; column < 4; ++column) {
                item->setFont(column, font);
            }
        }
    }
    m_builds->setVisible(!installs.empty());
    m_empty->setVisible(installs.empty());
    m_selection_buttons->setVisible(!installs.empty());
    UpdateSelectionButtons();

    if (!root.empty()) {
        m_size_watcher.setFuture(
            QtConcurrent::run([root] { return quint64{TesterBuild::DiskUsage(root)}; }));
    }
}

void ExperimentalBuildsPanel::Browse() {
    const auto folder = QFileDialog::getExistingDirectory(this, tr("Folder for experimental builds"),
                                                          m_folder->text());
    if (!folder.isEmpty()) {
        ChangeFolder(folder);
    }
}

void ExperimentalBuildsPanel::ChangeFolder(const QString& folder) {
    const auto old_root = TesterLauncher::Root();
    if (folder.trimmed().isEmpty()) {
        m_folder->setText(PathText(old_root));
        return;
    }
    auto new_root = PathOf(folder.trimmed());
    std::error_code ec;
    if (!old_root.empty() && fs::weakly_canonical(new_root, ec) == fs::weakly_canonical(old_root, ec)) {
        m_folder->setText(PathText(old_root));
        return;
    }
    // A folder with other things in it gets a folder of its own inside it.
    if (fs::is_directory(new_root, ec) && !fs::is_empty(new_root, ec) &&
        !TesterBuild::HasContent(TesterBuild::MakeLayout(new_root))) {
        new_root /= "eglantine-experimental";
    }

    const auto old_layout = TesterBuild::MakeLayout(old_root);
    const auto new_layout = TesterBuild::MakeLayout(new_root);
    auto state = TesterBuild::LoadState(TesterLauncher::StateFile());

    if (!old_root.empty() && TesterBuild::HasContent(old_layout)) {
        const UpdateInstaller::Lock lock{old_root / "builder.lock"};
        if (!lock.Held()) {
            QMessageBox::warning(this, Title(),
                                 tr("An experimental build is being built. Wait until it is done, "
                                    "then change the folder."));
            m_folder->setText(PathText(old_root));
            return;
        }

        QMessageBox box{QMessageBox::Question, Title(),
                        tr("%1 already holds experimental builds. What should happen to them?")
                            .arg(PathText(old_root)),
                        QMessageBox::NoButton, this};
        box.setInformativeText(tr("Move takes them to the new folder; the next build compiles "
                                  "from scratch. Delete removes them for good."));
        auto* move = box.addButton(tr("Move"), QMessageBox::AcceptRole);
        auto* remove = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(move);
        box.exec();

        if (box.clickedButton() == move) {
            QProgressDialog progress{tr("Moving the experimental builds..."), {}, 0, 0, this};
            progress.setWindowTitle(Title());
            progress.setWindowModality(Qt::WindowModal);
            progress.setMinimumDuration(0);
            QFutureWatcher<std::error_code> watcher;
            connect(&watcher, &QFutureWatcher<std::error_code>::finished, &progress,
                    &QProgressDialog::reset);
            watcher.setFuture(QtConcurrent::run([&] {
                std::error_code move_ec;
                TesterBuild::MoveRoot(old_layout, new_layout, state, move_ec);
                return move_ec;
            }));
            progress.exec();
            watcher.waitForFinished();
            if (const auto move_ec = watcher.result()) {
                QMessageBox::warning(
                    this, Title(),
                    move_ec == std::errc::file_exists
                        ? tr("%1 already has files of the same names. Choose an empty folder.")
                              .arg(PathText(new_root))
                        : tr("Could not move the experimental builds: %1\n\nSome of them may "
                             "already be in %2.")
                              .arg(QString::fromStdString(move_ec.message()), PathText(new_root)));
                if (move_ec == std::errc::file_exists) {
                    m_folder->setText(PathText(old_root));
                    return;
                }
            }
        } else if (box.clickedButton() == remove) {
            TesterBuild::RemoveAll(old_layout, ec);
            state.built_sha.clear();
            state.current_install.clear();
            state.previous_install.clear();
            state.built_at = 0;
            state.start_tester = false;
        } else {
            m_folder->setText(PathText(old_root));
            return;
        }
    }

    std::error_code save_ec;
    TesterBuild::SaveState(TesterLauncher::StateFile(), state, save_ec);
    // The default folder is saved as no choice, so it follows the default if that moves.
    const bool is_default = new_root == TesterBuild::DefaultRoot();
    if (!TesterBuild::SaveRootSetting(TesterLauncher::RootSettingFile(),
                                      is_default ? fs::path{} : new_root, save_ec)) {
        QMessageBox::warning(this, Title(),
                             tr("Could not save the folder: %1")
                                 .arg(QString::fromStdString(save_ec.message())));
    }
    Refresh();
}

void ExperimentalBuildsPanel::DeleteBuilds() {
    const auto root = TesterLauncher::Root();
    if (root.empty()) {
        return;
    }
    if (QMessageBox::question(this, Title(),
                              tr("Delete all experimental builds and their downloaded tools? The "
                                 "release build is not affected."),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes) {
        return;
    }
    std::error_code ec;
    const bool removed = TesterBuild::RemoveAll(TesterBuild::MakeLayout(root), ec);

    auto state = TesterBuild::LoadState(TesterLauncher::StateFile());
    state.built_sha.clear();
    state.current_install.clear();
    state.previous_install.clear();
    state.built_at = 0;
    state.start_tester = false;
    std::error_code save_ec;
    TesterBuild::SaveState(TesterLauncher::StateFile(), state, save_ec);

    if (!removed) {
        QMessageBox::warning(this, Title(),
                             tr("Some files could not be deleted: %1")
                                 .arg(QString::fromStdString(ec.message())));
    }
    Refresh();
}

const TesterBuild::InstallInfo* ExperimentalBuildsPanel::SelectedInstall() const {
    const auto selected = m_builds->selectedItems();
    if (selected.isEmpty()) {
        return nullptr;
    }
    const int row = m_builds->indexOfTopLevelItem(selected.front());
    return row >= 0 && row < static_cast<int>(m_installs.size()) ? &m_installs[row] : nullptr;
}

void ExperimentalBuildsPanel::UpdateSelectionButtons() {
    const auto* install = SelectedInstall();
    const bool running = install && TesterLauncher::IsRunningInstall(*install);
    const QString why = !install   ? tr("Select a build in the list first.")
                        : running ? tr("This build is running.")
                                  : QString{};
    m_launch->setEnabled(install && !running);
    m_launch->setToolTip(why.isEmpty() ? tr("Saves the settings, closes Eglantine and starts it "
                                            "again as this build.")
                                       : why);
    m_delete_one->setEnabled(install && !running);
    m_delete_one->setToolTip(why);
}

void ExperimentalBuildsPanel::LaunchSelected() {
    const auto* install = SelectedInstall();
    if (!install || TesterLauncher::IsRunningInstall(*install)) {
        return;
    }
    if (QMessageBox::question(this, tr("Launch"), TesterLauncher::LaunchQuestion(*install),
                              QMessageBox::Ok | QMessageBox::Cancel,
                              QMessageBox::Ok) != QMessageBox::Ok) {
        return;
    }
    emit LaunchRequested(QString::fromStdString(TesterBuild::PathToUtf8(install->dir)));
}

void ExperimentalBuildsPanel::DeleteSelected() {
    const auto* install = SelectedInstall();
    if (!install || TesterLauncher::IsRunningInstall(*install)) {
        return;
    }
    const auto info = *install;
    const QString name = info.source ? QString::fromStdString(info.source->DisplayName())
                                     : tr("an unknown branch");
    if (QMessageBox::question(this, tr("Delete"),
                              tr("Delete the build of %1 at %2?")
                                  .arg(name, QString::fromStdString(info.sha.substr(0, 10))),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    auto state = TesterBuild::LoadState(TesterLauncher::StateFile());
    std::error_code ec;
    if (!TesterBuild::RemoveInstall(state, info, ec)) {
        QMessageBox::warning(this, tr("Delete"),
                             tr("Could not delete %1: %2")
                                 .arg(PathText(info.dir), QString::fromStdString(ec.message())));
    } else {
        TesterBuild::SaveState(TesterLauncher::StateFile(), state, ec);
    }
    Refresh();
}
