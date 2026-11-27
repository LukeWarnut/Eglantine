// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <QString>
#include <QStringList>
#include "frontend_common/tester_build.h"

class QWidget;

// The release exe's side of tester builds: it is always the entry point, and hands over to the
// installed tester build when the user follows a branch or pull request.
namespace TesterLauncher {

// tester.json, in the config folder.
std::filesystem::path StateFile();

// The folder experimental builds are kept in: the one chosen in the settings, or the default.
std::filesystem::path Root();
// Where that choice is saved.
std::filesystem::path RootSettingFile();

// Whether `install` is the build this process runs from.
bool IsRunningInstall(const TesterBuild::InstallInfo& install);

// The question to ask before launching `install`.
QString LaunchQuestion(const TesterBuild::InstallInfo& install);

// Makes `install` the build Eglantine starts with, closes `main_window` and starts the build.
// When closing is declined the previous choice is put back and nothing starts.
bool SwitchAndLaunch(const TesterBuild::InstallInfo& install, QWidget* main_window);

// The exe to start for a plain relaunch of this build: $APPIMAGE inside an AppImage.
QString SelfExe();

// Whether this exe lies in the tester install folder.
bool IsTesterBuild();

// The commit this exe was built from when it is a CI build of the followed branch.
std::optional<std::string> FollowedBranchCommit(const TesterBuild::State& state);

// The commit a new one of the followed source is compared against: the installed tester build's,
// else this exe's when it is a CI build of the followed branch.
std::optional<std::string> CurrentCommit(const TesterBuild::State& state);

// The exe that runs the builder: always the release build, as the followed branch may be older than
// the builder. Inside a tester build that is the one the state remembers.
QString BuilderExe(const TesterBuild::State& state);

struct RemoteResult {
    // Empty when the check failed; `status` is then the HTTP status, or 0 without a response.
    std::optional<TesterBuild::RemoteInfo> info;
    int status{};
};

// Blocking: callers run it on a worker thread.
RemoteResult FetchRemote(const TesterBuild::Source& source);

// Starts a program that outlives this process. On Linux the AppImage environment is removed first,
// since the program is not part of this AppImage.
bool StartDetached(const QString& program, const QStringList& args,
                   const QString& working_dir = {});

// `<exe> --tester-build`, which shows the builder window. `close_caller` lets the builder close
// this process when the user starts the new build. A non-empty `commit` builds that commit of the
// source instead of its newest.
bool StartBuilder(const QString& exe, bool force, const QString& source = {},
                  bool close_caller = true, const QString& commit = {});

// The Eglantine that started the builder. It is held from the builder's start, so its pid cannot
// be reused by another process before it is asked to close.
class CallerProcess {
public:
    CallerProcess() = default;
    explicit CallerProcess(qint64 pid);
    ~CallerProcess();
    CallerProcess(const CallerProcess&) = delete;
    CallerProcess& operator=(const CallerProcess&) = delete;

    bool Running() const;
    // Asks it to close like its window's close button would, so it saves its settings.
    void RequestClose() const;

private:
#ifdef _WIN32
    void* m_handle{};
#else
    int m_fd{-1};
#endif
};

// Called at the start of main(), once it is known this is not a helper process. Returns the exit
// code when the launcher handled this start (it never falls through into the emulator), and
// nothing when the emulator should start normally.
std::optional<int> Run(int argc, char** argv);

} // namespace TesterLauncher
