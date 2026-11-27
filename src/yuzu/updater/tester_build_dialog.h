// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <memory>
#include <QDialog>
#include <QElapsedTimer>
#include <QStringList>
#include <QTimer>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QToolButton;
class TesterBuildRunner;

namespace TesterLauncher {
class CallerProcess;
}

// The window of the tester builder: a thin view over TesterBuildRunner's signals.
class TesterBuildDialog : public QDialog {
    Q_OBJECT

public:
    // `caller_pid` is the Eglantine that started the builder, closed before the new build starts.
    explicit TesterBuildDialog(TesterBuildRunner& runner, qint64 caller_pid = 0,
                               QWidget* parent = nullptr);
    ~TesterBuildDialog() override;

    int ExitCode() const {
        return m_exit_code;
    }

    void reject() override;

private:
    void OnFinished(int result, const QString& message);
    void OnConsent(const QStringList& items, const QString& text, const QString& command,
                   const QString& accept_label);
    void FlushLog();
    // "Building <branch>" and the like, from the stage and the result once finished.
    void UpdateTitle();
    void UpdateStages();
    void SetLogVisible(bool visible);
    void StartBuiltEmulator();
    void LaunchBuiltEmulator();
    // Starts the builder again with the same arguments and closes this one.
    void TryAgain();

    TesterBuildRunner& m_runner;
    QLabel* m_title;
    QLabel* m_subtitle;
    std::array<QLabel*, 4> m_stages{};
    QWidget* m_progress_area;
    QProgressBar* m_progress;
    QLabel* m_remaining;
    QLabel* m_step;
    QLabel* m_message;
    QPlainTextEdit* m_failure_tail;
    QElapsedTimer m_step_timer;
    QToolButton* m_toggle_log;
    QPlainTextEdit* m_log;

    QWidget* m_consent;
    QLabel* m_consent_text;
    QPlainTextEdit* m_consent_items;
    QPlainTextEdit* m_consent_command;
    QPushButton* m_copy;
    QPushButton* m_accept;

    QCheckBox* m_auto_launch;
    QPushButton* m_copy_log;
    QPushButton* m_open_logs;
    QPushButton* m_retry;
    QPushButton* m_start;
    QPushButton* m_cancel;

    std::unique_ptr<TesterLauncher::CallerProcess> m_caller;
    QTimer m_caller_wait;
    int m_caller_polls{};

    QString m_name;
    int m_stage{-1};
    // A TesterBuildRunner::Result once finished.
    int m_result{-1};

    QStringList m_pending;
    QTimer m_flush;
    bool m_running{true};
    int m_exit_code{1};
};
