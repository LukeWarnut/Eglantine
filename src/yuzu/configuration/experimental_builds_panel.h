// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>
#include <QFutureWatcher>
#include <QWidget>
#include "frontend_common/tester_build.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

// The Experimental Builds and Actions groups of the Updates settings: where the builds of branches
// and pull requests are kept, which are installed, launching or deleting one, and deleting them
// all. Building them is left to the version manager.
class ExperimentalBuildsPanel : public QWidget {
    Q_OBJECT

public:
    explicit ExperimentalBuildsPanel(QWidget* parent = nullptr);
    ~ExperimentalBuildsPanel() override;

signals:
    void OpenVersionManagerRequested();
    // The user confirmed launching the install in `install_dir`; Eglantine has to close for it.
    void LaunchRequested(const QString& install_dir);

private:
    void Browse();
    // Uses `folder` from now on, after moving or deleting what the current folder holds.
    void ChangeFolder(const QString& folder);
    void Refresh();
    void DeleteBuilds();
    const TesterBuild::InstallInfo* SelectedInstall() const;
    void UpdateSelectionButtons();
    void LaunchSelected();
    void DeleteSelected();

    QLineEdit* m_folder;
    QPushButton* m_browse;
    QTreeWidget* m_builds;
    QLabel* m_empty;
    QWidget* m_selection_buttons;
    QPushButton* m_launch;
    QPushButton* m_delete_one;
    QPushButton* m_open;
    QPushButton* m_delete;
    QFutureWatcher<quint64> m_size_watcher;
    // In list order.
    std::vector<TesterBuild::InstallInfo> m_installs;
};
