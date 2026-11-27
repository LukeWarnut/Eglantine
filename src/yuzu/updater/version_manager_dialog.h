// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>
#include <QDialog>
#include <QFutureWatcher>
#include "common/net/net.h"
#include "frontend_common/tester_build.h"

class QLabel;
class QListWidget;
class QPushButton;

// Help > Version Manager: the branches and pull requests with tester builds installed, their
// commits, and building, deleting or switching to a build of one.
class VersionManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit VersionManagerDialog(QWidget* parent = nullptr);
    ~VersionManagerDialog() override;

private:
    struct CommitList {
        std::string key;
        std::vector<Common::Net::Commit> commits;
        // From the local checkout because GitHub could not be asked; the newest commit is then
        // only the newest fetched.
        bool local{};
        // GitHub's answer when it failed, 0 without a response.
        int status{};
    };

    struct Branch {
        // Empty for installs whose source is unknown, and for the release.
        std::optional<TesterBuild::Source> source;
        std::vector<TesterBuild::InstallInfo> installs;
        bool release{};

        // Stable across reloads, to select the same entry again.
        std::string Key() const;
    };

    void Reload();
    // `keep` is the Key() of the entry to select again; empty selects the build that starts.
    void FillBranches(const std::string& keep);
    // Whether `install` is the build the release starts.
    bool IsStarting(const TesterBuild::InstallInfo& install) const;
    bool ReleaseIsRunning() const;
    void LaunchRelease();
    void OnBranchSelected();
    void ShowCommits(const CommitList& list);
    void UpdateButtons();

    // Asks for a branch or pull request to list, from a search of the repository's branches.
    void AddBranch();
    void ShowBranchMenu(const QPoint& pos);
    void SelectSource(const TesterBuild::Source& source);
    void SaveSources();

    void BuildSelected();
    void DeleteSelected();
    void LaunchSelected();

    const Branch* SelectedBranch() const;
    std::optional<Common::Net::Commit> SelectedCommit() const;
    // The install of the selected commit, if there is one.
    const TesterBuild::InstallInfo* SelectedInstall() const;
    bool IsRunning(const TesterBuild::InstallInfo& install) const;
    bool SaveState();

    TesterBuild::Layout m_layout;
    TesterBuild::State m_state;
    std::vector<Branch> m_branches;
    // The release exe; empty when it is not known.
    QString m_release_exe;
    // Added with "Add Branch".
    std::vector<TesterBuild::Source> m_saved;
    // Every branch of the repository, once read.
    QStringList m_branch_names;
    // Commit lists already fetched, by Source::ToString().
    std::map<std::string, CommitList> m_cache;
    // The commits shown, in list order.
    std::vector<Common::Net::Commit> m_shown;
    // Whether m_shown[0] is the newest commit GitHub knows of.
    bool m_first_is_latest{};

    QListWidget* m_branch_list;
    QPushButton* m_add_branch;
    QListWidget* m_commit_list;
    QLabel* m_status;
    QPushButton* m_build;
    QPushButton* m_delete;
    QPushButton* m_launch;
    QFutureWatcher<CommitList> m_watcher;
};
