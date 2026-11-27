// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTextBlock>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include "common/scm_rev.h"
#include "frontend_common/update_installer.h"
#include "tester_build_dialog.h"
#include "tester_build_launcher.h"
#include "tester_build_runner.h"

namespace {
constexpr int MaxLogLines = 5000;
constexpr int FailureTailLines = 10;
constexpr int LogHeight = 260;
constexpr int CallerPollMilliseconds = 200;
// Long enough to answer a "close while a game runs?" question.
constexpr int CallerPolls = 60'000 / CallerPollMilliseconds;
constexpr std::string_view MainRepository = "Eglantine-Community/Eglantine";

// Remembered between builds, beside the builds themselves.
std::unique_ptr<QSettings> BuilderSettings() {
    const auto root = TesterLauncher::Root();
    if (root.empty()) {
        return nullptr;
    }
    return std::make_unique<QSettings>(
        QString::fromStdString(TesterBuild::PathToUtf8(root / "builder.ini")),
        QSettings::IniFormat);
}

constexpr auto AutoLaunchKey = "auto_launch";

// The stage shown in the step row for a TesterBuildRunner::Stage.
int StageIndex(int stage) {
    switch (stage) {
    case TesterBuildRunner::Downloading:
    case TesterBuildRunner::Fetching:
        return 1;
    case TesterBuildRunner::Building:
        return 2;
    case TesterBuildRunner::Installing:
        return 3;
    default:
        return 0;
    }
}

QString Rgba(QColor color, qreal alpha) {
    color.setAlphaF(alpha);
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alphaF());
}

QPlainTextEdit* MonospaceBox(QWidget* parent) {
    auto* box = new QPlainTextEdit(parent);
    box->setReadOnly(true);
    box->setLineWrapMode(QPlainTextEdit::NoWrap);
    box->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    return box;
}
} // namespace

TesterBuildDialog::TesterBuildDialog(TesterBuildRunner& runner, qint64 caller_pid, QWidget* parent)
    : QDialog(parent), m_runner(runner),
      m_caller(caller_pid > 0 ? std::make_unique<TesterLauncher::CallerProcess>(caller_pid)
                              : nullptr) {
    setWindowTitle(tr("Experimental build"));

    // Header: what is being built, and from which commit once that is known.
    m_title = new QLabel(tr("Experimental build"));
    QFont title_font = m_title->font();
    title_font.setPointSizeF(title_font.pointSizeF() * 1.35);
    title_font.setBold(true);
    m_title->setFont(title_font);
    // The layout sizes the window to its content, so the width is held here.
    m_title->setMinimumWidth(520);
    m_subtitle = new QLabel;
    m_subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    const QString repository = QString::fromUtf8(Common::g_build_auto_update_repo);
    if (repository != QLatin1String(MainRepository.data(), MainRepository.size())) {
        m_subtitle->setText(tr("From %1").arg(repository));
    } else {
        m_subtitle->hide();
    }

    // Prepare > Get source > Build > Install
    auto* stage_row = new QHBoxLayout;
    stage_row->setSpacing(8);
    const std::array<QString, 4> stage_names{tr("Prepare"), tr("Get source"), tr("Build"),
                                             tr("Install")};
    for (std::size_t i = 0; i < m_stages.size(); ++i) {
        if (i != 0) {
            auto* separator = new QLabel(QStringLiteral("›"));
            separator->setEnabled(false);
            stage_row->addWidget(separator);
        }
        m_stages[i] = new QLabel(stage_names[i]);
        stage_row->addWidget(m_stages[i]);
    }
    stage_row->addStretch();

    // Progress: the bar with the time left beside it, the current step under it.
    m_progress_area = new QWidget;
    m_progress = new QProgressBar;
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_remaining = new QLabel(tr("Working..."));
    m_step = new QLabel(tr("Starting..."));
    m_step->setWordWrap(true);
    auto* bar_row = new QHBoxLayout;
    bar_row->addWidget(m_progress, 1);
    bar_row->addWidget(m_remaining);
    auto* progress_layout = new QVBoxLayout(m_progress_area);
    progress_layout->setContentsMargins(0, 0, 0, 0);
    progress_layout->setSpacing(4);
    progress_layout->addLayout(bar_row);
    progress_layout->addWidget(m_step);
    m_step_timer.start();

    m_message = new QLabel;
    m_message->setWordWrap(true);
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_message->hide();
    m_failure_tail = MonospaceBox(this);
    m_failure_tail->setFixedHeight(m_failure_tail->fontMetrics().lineSpacing() *
                                       FailureTailLines +
                                   12);
    m_failure_tail->hide();

    m_toggle_log = new QToolButton;
    m_toggle_log->setText(tr("Details"));
    m_toggle_log->setCheckable(true);
    m_toggle_log->setAutoRaise(true);
    m_toggle_log->setArrowType(Qt::RightArrow);
    m_toggle_log->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_log = MonospaceBox(this);
    m_log->setMaximumBlockCount(MaxLogLines);
    m_log->setMinimumHeight(LogHeight);
    m_log->hide();

    m_consent = new QWidget;
    m_consent_text = new QLabel;
    m_consent_text->setWordWrap(true);
    m_consent_items = new QPlainTextEdit;
    m_consent_items->setReadOnly(true);
    m_consent_items->setMaximumHeight(90);
    m_consent_command = MonospaceBox(this);
    m_consent_command->setMaximumHeight(60);
    auto* consent_layout = new QVBoxLayout(m_consent);
    consent_layout->setContentsMargins(0, 0, 0, 0);
    consent_layout->addWidget(m_consent_text);
    consent_layout->addWidget(m_consent_items);
    consent_layout->addWidget(m_consent_command);
    m_consent->hide();

    m_copy = new QPushButton(tr("Copy"));
    m_accept = new QPushButton;
    m_copy_log = new QPushButton(tr("Copy Log"));
    m_open_logs = new QPushButton(tr("Open Log Folder"));
    m_retry = new QPushButton(tr("Try Again"));
    m_start = new QPushButton(tr("Launch"));
    m_cancel = new QPushButton(tr("Cancel"));
    for (auto* button : {m_copy, m_accept, m_copy_log, m_open_logs, m_retry, m_start}) {
        button->hide();
    }
    m_auto_launch = new QCheckBox(tr("Automatically launch the build when done"));
    if (const auto settings = BuilderSettings()) {
        m_auto_launch->setChecked(settings->value(QLatin1String(AutoLaunchKey), false).toBool());
    }

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_auto_launch);
    buttons->addWidget(m_copy_log);
    buttons->addWidget(m_open_logs);
    buttons->addStretch();
    buttons->addWidget(m_copy);
    buttons->addWidget(m_accept);
    buttons->addWidget(m_retry);
    buttons->addWidget(m_start);
    buttons->addWidget(m_cancel);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->addWidget(m_title);
    layout->addWidget(m_subtitle);
    layout->addSpacing(2);
    layout->addLayout(stage_row);
    layout->addWidget(m_progress_area);
    layout->addWidget(m_message);
    layout->addWidget(m_failure_tail);
    layout->addWidget(m_consent);
    layout->addWidget(m_toggle_log, 0, Qt::AlignLeft);
    layout->addWidget(m_log, 1);
    layout->addLayout(buttons);

    connect(m_toggle_log, &QToolButton::toggled, this, &TesterBuildDialog::SetLogVisible);
    connect(m_auto_launch, &QCheckBox::toggled, this, [](bool on) {
        if (const auto settings = BuilderSettings()) {
            settings->setValue(QLatin1String(AutoLaunchKey), on);
        }
    });
    connect(m_cancel, &QPushButton::clicked, this, &TesterBuildDialog::reject);
    connect(m_copy, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(m_consent_command->toPlainText()); });
    connect(m_accept, &QPushButton::clicked, this, [this] {
        m_consent->hide();
        m_copy->hide();
        m_accept->hide();
        m_runner.Respond(true);
    });
    connect(m_copy_log, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(m_log->toPlainText()); });
    connect(m_open_logs, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_runner.LogDirectory()));
    });
    connect(m_retry, &QPushButton::clicked, this, &TesterBuildDialog::TryAgain);
    connect(m_start, &QPushButton::clicked, this, &TesterBuildDialog::StartBuiltEmulator);

    connect(&m_runner, &TesterBuildRunner::StageChanged, this, [this](int stage) {
        m_stage = stage;
        UpdateTitle();
        UpdateStages();
    });
    connect(&m_runner, &TesterBuildRunner::SourceResolved, this, [this](const QString& name) {
        m_name = name;
        UpdateTitle();
    });
    connect(&m_runner, &TesterBuildRunner::CommitResolved, this,
            [this](const QString& sha, const QString& subject) {
                QString text = sha.left(10);
                if (!subject.isEmpty()) {
                    text += QStringLiteral("  ·  ") + subject;
                }
                m_subtitle->setText(text);
                m_subtitle->setToolTip(sha);
                m_subtitle->show();
            });
    connect(&m_runner, &TesterBuildRunner::StepStarted, this, [this](const QString& label) {
        m_step->setText(label);
        m_remaining->setText(tr("Working..."));
        m_step_timer.restart();
    });
    connect(&m_runner, &TesterBuildRunner::LogLine, this,
            [this](const QString& line) { m_pending << line; });
    connect(&m_runner, &TesterBuildRunner::Progress, this, [this](int done, int total) {
        m_progress->setRange(0, total);
        m_progress->setValue(done);
        if (total == 0) {
            m_remaining->setText(tr("Working..."));
            return;
        }
        const QString percent = QStringLiteral("%1%").arg(done * 100 / std::max(total, 1));
        const QString left =
            TesterBuildRunner::RemainingText(done, total, m_step_timer.elapsed() / 1000);
        m_remaining->setText(left.isEmpty() ? percent
                                            : QStringLiteral("%1  ·  %2").arg(percent, left));
    });
    connect(&m_runner, &TesterBuildRunner::ConsentNeeded, this, &TesterBuildDialog::OnConsent);
    connect(&m_runner, &TesterBuildRunner::Finished, this, &TesterBuildDialog::OnFinished);

    m_caller_wait.setInterval(CallerPollMilliseconds);
    connect(&m_caller_wait, &QTimer::timeout, this, [this] {
        if (!m_caller->Running()) {
            m_caller_wait.stop();
            LaunchBuiltEmulator();
        } else if (++m_caller_polls >= CallerPolls) {
            m_caller_wait.stop();
            m_message->setText(tr("Eglantine is still open. Close it, then press Launch."));
            m_start->setEnabled(true);
            m_cancel->setEnabled(true);
        }
    });

    m_flush.setInterval(150);
    connect(&m_flush, &QTimer::timeout, this, &TesterBuildDialog::FlushLog);
    m_flush.start();

    UpdateStages();
    adjustSize();
}

TesterBuildDialog::~TesterBuildDialog() = default;

void TesterBuildDialog::UpdateTitle() {
    QString title;
    if (m_name.isEmpty()) {
        title = tr("Experimental build");
    } else {
        switch (m_result) {
        case TesterBuildRunner::Success:
        case TesterBuildRunner::UpToDate:
            title = tr("%1 is ready");
            break;
        case TesterBuildRunner::Failed:
            title = tr("Building %1 failed");
            break;
        case TesterBuildRunner::Canceled:
            title = tr("Building %1 canceled");
            break;
        default:
            switch (m_stage) {
            case TesterBuildRunner::Downloading:
                title = tr("Downloading %1");
                break;
            case TesterBuildRunner::Fetching:
                title = tr("Fetching %1");
                break;
            case TesterBuildRunner::Building:
                title = tr("Building %1");
                break;
            case TesterBuildRunner::Installing:
                title = tr("Installing %1");
                break;
            default:
                title = tr("Preparing to build %1");
                break;
            }
        }
        title = title.arg(m_name);
    }
    setWindowTitle(title);
    m_title->setText(title);
}

void TesterBuildDialog::UpdateStages() {
    const bool done = m_result == TesterBuildRunner::Success ||
                      m_result == TesterBuildRunner::UpToDate;
    const bool stopped = m_result == TesterBuildRunner::Failed ||
                         m_result == TesterBuildRunner::Canceled;
    const int current = StageIndex(m_stage);
    const QColor text = palette().color(QPalette::Text);
    const QString highlight = palette().color(QPalette::Highlight).name();
    const std::array<QString, 4> names{tr("Prepare"), tr("Get source"), tr("Build"),
                                       tr("Install")};
    for (int i = 0; i < static_cast<int>(m_stages.size()); ++i) {
        auto* label = m_stages[static_cast<std::size_t>(i)];
        const QString& name = names[static_cast<std::size_t>(i)];
        if (done || i < current) {
            label->setText(QStringLiteral("✓ ") + name);
            label->setStyleSheet(QStringLiteral("color: %1;").arg(Rgba(text, 0.6)));
        } else if (i == current && stopped) {
            label->setText(QStringLiteral("✕ ") + name);
            label->setStyleSheet(QStringLiteral("color: #d32f2f; font-weight: bold;"));
        } else if (i == current) {
            label->setText(name);
            label->setStyleSheet(
                QStringLiteral("color: %1; font-weight: bold;").arg(highlight));
        } else {
            label->setText(name);
            label->setStyleSheet(QStringLiteral("color: %1;").arg(Rgba(text, 0.45)));
        }
    }
}

void TesterBuildDialog::SetLogVisible(bool visible) {
    m_toggle_log->setArrowType(visible ? Qt::DownArrow : Qt::RightArrow);
    m_log->setVisible(visible);
    if (visible) {
        resize(width(), height() + LogHeight);
    } else {
        // Shrink back once the layout has dropped the log.
        QTimer::singleShot(0, this, [this] { resize(width(), minimumSizeHint().height()); });
    }
}

void TesterBuildDialog::reject() {
    if (m_running) {
        m_cancel->setEnabled(false);
        m_step->setText(tr("Canceling..."));
        m_runner.Cancel();
        return;
    }
    QDialog::reject();
}

void TesterBuildDialog::FlushLog() {
    if (m_pending.isEmpty()) {
        return;
    }
    m_log->appendPlainText(m_pending.join(QLatin1Char('\n')));
    m_pending.clear();
}

void TesterBuildDialog::OnConsent(const QStringList& items, const QString& text,
                                  const QString& command, const QString& accept_label) {
    m_consent_text->setText(text);
    m_consent_items->setPlainText(items.join(QLatin1Char('\n')));
    m_consent_command->setPlainText(command);
    m_consent_command->setVisible(!command.isEmpty());
    m_copy->setVisible(!command.isEmpty());
    m_accept->setText(accept_label);
    m_accept->setVisible(!accept_label.isEmpty());
    m_consent->show();
    m_remaining->clear();
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    if (accept_label.isEmpty()) {
        // Nothing to wait for: the runner stops and says why.
        m_runner.Respond(false);
    }
}

void TesterBuildDialog::OnFinished(int result, const QString& message) {
    m_running = false;
    m_flush.stop();
    FlushLog();
    m_exit_code = TesterBuildRunner::ExitCode(static_cast<TesterBuildRunner::Result>(result));
    m_result = result;
    UpdateTitle();
    UpdateStages();

    const bool failed = result == TesterBuildRunner::Failed;
    const bool ready = !m_runner.InstalledExePath().isEmpty() &&
                       (result == TesterBuildRunner::Success ||
                        result == TesterBuildRunner::UpToDate);

    // Only the outcome is left to show.
    m_progress_area->hide();
    m_auto_launch->hide();
    m_consent->hide();
    m_copy->hide();
    m_accept->hide();
    m_message->setText(message);
    m_message->show();
    m_cancel->setText(tr("Close"));
    m_cancel->setEnabled(true);

    if (failed) {
        QStringList tail;
        for (auto block = m_log->document()->lastBlock();
             block.isValid() && tail.size() < FailureTailLines; block = block.previous()) {
            tail.prepend(block.text());
        }
        m_failure_tail->setPlainText(tail.join(QLatin1Char('\n')));
        m_failure_tail->setVisible(!tail.isEmpty());
        m_copy_log->show();
        m_open_logs->show();
        m_retry->show();
        m_retry->setDefault(true);
    }
    m_start->setVisible(ready);
    if (ready) {
        m_start->setDefault(true);
        m_start->setFocus();
    }
    adjustSize();

    if (ready && m_auto_launch->isChecked()) {
        StartBuiltEmulator();
    }
}

void TesterBuildDialog::TryAgain() {
    QStringList args = QCoreApplication::arguments().mid(1);
    // The caller's pid is only handed on while that process still runs.
    if (const auto at = args.indexOf(QStringLiteral("--caller"));
        at >= 0 && (!m_caller || !m_caller->Running())) {
        args.remove(at, std::min<qsizetype>(2, args.size() - at));
    }
    if (!TesterLauncher::StartDetached(TesterLauncher::SelfExe(), args)) {
        m_message->setText(tr("Could not start the builder again."));
        return;
    }
    QDialog::reject();
}

void TesterBuildDialog::StartBuiltEmulator() {
    // The running Eglantine closes first, so the new one reads the settings it saves on close.
    if (!m_caller || !m_caller->Running()) {
        LaunchBuiltEmulator();
        return;
    }
    m_message->setText(tr("Closing the running Eglantine..."));
    m_start->setEnabled(false);
    m_cancel->setEnabled(false);
    m_caller_polls = 0;
    m_caller->RequestClose();
    m_caller_wait.start();
}

void TesterBuildDialog::LaunchBuiltEmulator() {
    const QString exe = m_runner.InstalledExePath();
    {
        // The release starts this build from now on, until the release is launched on purpose.
        auto state = TesterBuild::LoadState(TesterLauncher::StateFile());
        state.start_tester = true;
        std::error_code ec;
        TesterBuild::SaveState(TesterLauncher::StateFile(), state, ec);
    }
    QProcess process;
    process.setProgram(exe);
    // A portable user folder is found from the working directory on Linux.
#ifdef _WIN32
    process.setWorkingDirectory(QFileInfo{exe}.absolutePath());
#else
    process.setWorkingDirectory(QDir::currentPath());
    auto environment = QProcessEnvironment::systemEnvironment();
    const auto app_dir = environment.value(QStringLiteral("APPDIR"));
    if (!app_dir.isEmpty()) {
        for (const auto& key : environment.keys()) {
            const auto value = environment.value(key);
            if (!value.contains(app_dir)) {
                continue;
            }
            const auto stripped = UpdateInstaller::StripPathListEntries(value.toStdString(),
                                                                        app_dir.toStdString());
            if (stripped.empty()) {
                environment.remove(key);
            } else {
                environment.insert(key, QString::fromStdString(stripped));
            }
        }
    }
    for (const auto& key : {"APPDIR", "APPIMAGE", "ARGV0", "OWD"}) {
        environment.remove(QString::fromLatin1(key));
    }
    process.setProcessEnvironment(environment);
#endif
    // The build outlives the builder; its log file has what it would print.
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    if (!process.startDetached()) {
        m_message->setText(tr("Could not start %1.").arg(exe));
        m_start->setEnabled(true);
        m_cancel->setEnabled(true);
        return;
    }
    accept();
}
