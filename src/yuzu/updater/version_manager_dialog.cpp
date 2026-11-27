// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include "common/scm_rev.h"
#include <QtConcurrent/QtConcurrentRun>

#include "yuzu/updater/tester_build_launcher.h"
#include "yuzu/updater/version_manager_dialog.h"

namespace fs = std::filesystem;

namespace {

constexpr int CommitCount = 50;
constexpr int DescriptionLines = 3;

QString Q(const std::string& text) {
    return QString::fromStdString(text);
}

QString PathText(const fs::path& path) {
    return Q(TesterBuild::PathToUtf8(path));
}

bool Matches(const TesterBuild::InstallInfo& install, const std::string& sha) {
    return !install.sha.empty() && sha.starts_with(install.sha);
}

QLabel* Badge(const QString& text, const char* name, QWidget* parent) {
    auto* badge = new QLabel(text, parent);
    badge->setObjectName(QLatin1String(name));
    QFont font = badge->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 0.85);
    badge->setFont(font);
    return badge;
}

struct RowBadges {
    bool latest{};
    bool newest_fetched{};
    bool current{};
    bool running{};
    bool built{};
};

// Subject, badges, hash and date on the first line; the start of the description under it.
QWidget* CommitRow(const Common::Net::Commit& commit, const RowBadges& badges, QWidget* parent) {
    auto* row = new QWidget(parent);
    row->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* layout = new QVBoxLayout(row);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(2);

    const QString message = Q(commit.message).trimmed();
    const qsizetype newline = message.indexOf(QLatin1Char('\n'));
    const QString subject = newline < 0 ? message : message.left(newline);
    const QString description = newline < 0 ? QString{} : message.mid(newline + 1).trimmed();

    auto* top = new QHBoxLayout;
    top->setSpacing(6);
    auto* subject_label = new QLabel(row);
    subject_label->setObjectName(QStringLiteral("subject"));
    QFont subject_font = subject_label->font();
    subject_font.setBold(true);
    subject_label->setFont(subject_font);
    subject_label->setText(subject.isEmpty() ? VersionManagerDialog::tr("(no message)")
                                             : subject);
    subject_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    top->addWidget(subject_label, 1);

    if (badges.latest) {
        top->addWidget(Badge(VersionManagerDialog::tr("Latest"), "latest", row));
    } else if (badges.newest_fetched) {
        top->addWidget(Badge(VersionManagerDialog::tr("Newest fetched"), "newest", row));
    }
    if (badges.current) {
        top->addWidget(Badge(VersionManagerDialog::tr("Current build"), "current", row));
    } else if (badges.built) {
        top->addWidget(Badge(VersionManagerDialog::tr("Built"), "built", row));
    }
    if (badges.running) {
        top->addWidget(Badge(VersionManagerDialog::tr("Running"), "running", row));
    }

    QString meta = Q(commit.sha.substr(0, 10));
    if (commit.time != 0) {
        meta += QStringLiteral("  ·  ") +
                QLocale().toString(
                    QDateTime::fromSecsSinceEpoch(static_cast<qint64>(commit.time)),
                    QLocale::ShortFormat);
    }
    auto* meta_label = new QLabel(meta, row);
    meta_label->setObjectName(QStringLiteral("meta"));
    top->addWidget(meta_label);
    layout->addLayout(top);

    QStringList lines;
    if (!commit.author.empty()) {
        lines << VersionManagerDialog::tr("by %1").arg(Q(commit.author));
    }
    for (const auto& line : description.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (lines.size() > DescriptionLines) {
            lines.back() += QStringLiteral(" …");
            break;
        }
        lines << line.trimmed();
    }
    if (!lines.isEmpty()) {
        auto* details = new QLabel(lines.join(QLatin1Char('\n')), row);
        details->setObjectName(QStringLiteral("details"));
        details->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(details);
    }
    row->setToolTip(message);
    return row;
}

QString Rgba(QColor color, qreal alpha) {
    color.setAlphaF(alpha);
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alphaF());
}

// The selected row sits on the highlight colour, so everything on it switches to the highlighted
// text colour; the current-build badge, itself highlight coloured, is drawn inverted there.
void StyleRow(QWidget* row, bool selected) {
    const QPalette& palette = row->palette();
    const QColor text = palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    const QColor highlight = palette.color(QPalette::Highlight);
    const QString badge = QStringLiteral("border-radius: 3px; padding: 1px 6px;");
    row->setStyleSheet(
        QStringLiteral("QLabel#subject { color: %1; }"
                       "QLabel#meta, QLabel#details { color: %2; }"
                       "QLabel#latest { %3 background: #2e7d32; color: white; }"
                       "QLabel#newest { %3 border: 1px solid %4; color: %4; }"
                       "QLabel#current { %3 background: %5; color: %6; }"
                       "QLabel#built, QLabel#running { %3 border: 1px solid %7; color: %1; }")
            .arg(text.name(), Rgba(text, selected ? 0.8 : 0.6), badge,
                 selected ? text.name() : QStringLiteral("#2e7d32"),
                 selected ? text.name() : highlight.name(),
                 selected ? highlight.name() : palette.color(QPalette::HighlightedText).name(),
                 Rgba(text, 0.5)));
}

} // namespace

VersionManagerDialog::VersionManagerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Version Manager"));

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 20, 20, 16);

    auto* title = new QLabel(tr("Version Manager"), this);
    QFont title_font = title->font();
    title_font.setPointSizeF(title_font.pointSizeF() * 1.35);
    title_font.setBold(true);
    title->setFont(title_font);
    layout->addWidget(title);

    auto* intro = new QLabel(tr("Experimental builds of branches and pull requests, installed beside "
                                "this release. Pick a commit to build it, or switch to a build "
                                "you already have."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* lists = new QHBoxLayout;
    lists->setSpacing(12);
    auto* sidebar = new QVBoxLayout;
    sidebar->setSpacing(6);
    m_branch_list = new QListWidget(this);
    m_branch_list->setFixedWidth(220);
    m_branch_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_branch_list->setTextElideMode(Qt::ElideMiddle);
    m_branch_list->setContextMenuPolicy(Qt::CustomContextMenu);
    sidebar->addWidget(m_branch_list, 1);
    m_add_branch = new QPushButton(tr("Add Branch..."), this);
    m_add_branch->setToolTip(tr("List another branch or pull request here."));
    sidebar->addWidget(m_add_branch);
    lists->addLayout(sidebar);

    m_commit_list = new QListWidget(this);
    m_commit_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_commit_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_commit_list->setSpacing(1);
    lists->addWidget(m_commit_list, 1);
    layout->addLayout(lists, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto* buttons = new QDialogButtonBox(this);
    m_build = buttons->addButton(tr("Build"), QDialogButtonBox::ActionRole);
    m_delete = buttons->addButton(tr("Delete Instance"), QDialogButtonBox::ActionRole);
    m_launch = buttons->addButton(tr("Launch"), QDialogButtonBox::ActionRole);
    auto* close_button = buttons->addButton(QDialogButtonBox::Close);
    connect(close_button, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_build, &QPushButton::clicked, this, &VersionManagerDialog::BuildSelected);
    connect(m_delete, &QPushButton::clicked, this, &VersionManagerDialog::DeleteSelected);
    connect(m_launch, &QPushButton::clicked, this, &VersionManagerDialog::LaunchSelected);
    layout->addWidget(buttons);

    connect(m_branch_list, &QListWidget::currentRowChanged, this,
            &VersionManagerDialog::OnBranchSelected);
    connect(m_branch_list, &QListWidget::customContextMenuRequested, this,
            &VersionManagerDialog::ShowBranchMenu);
    connect(m_add_branch, &QPushButton::clicked, this, &VersionManagerDialog::AddBranch);
    connect(m_commit_list, &QListWidget::currentRowChanged, this, [this] {
        for (int i = 0; i < m_commit_list->count(); ++i) {
            if (auto* row = m_commit_list->itemWidget(m_commit_list->item(i))) {
                StyleRow(row, i == m_commit_list->currentRow());
            }
        }
        UpdateButtons();
    });
    connect(&m_watcher, &QFutureWatcher<CommitList>::finished, this, [this] {
        auto list = m_watcher.result();
        const auto key = list.key;
        m_cache[key] = std::move(list);
        if (const auto* branch = SelectedBranch();
            branch && branch->source && branch->source->ToString() == key) {
            ShowCommits(m_cache[key]);
        }
    });

    QSize size{920, 620};
    if (const QScreen* screen = parent ? parent->screen() : this->screen()) {
        size = size.boundedTo(screen->availableGeometry().size() * 0.85);
    }
    resize(size);

    const auto root = TesterLauncher::Root();
    if (root.empty()) {
        m_status->setText(tr("Experimental builds are not available: no folder to keep them in."));
        m_add_branch->setEnabled(false);
        UpdateButtons();
        return;
    }
    m_layout = TesterBuild::MakeLayout(root);
    m_saved = TesterBuild::LoadSavedSources(TesterBuild::SavedSourcesPath(m_layout));
    Reload();
}

VersionManagerDialog::~VersionManagerDialog() {
    m_watcher.waitForFinished();
}

std::string VersionManagerDialog::Branch::Key() const {
    if (release) {
        return "release";
    }
    return source ? source->ToString() : "unknown";
}

bool VersionManagerDialog::IsStarting(const TesterBuild::InstallInfo& install) const {
    return m_state.start_tester && !m_state.current_install.empty() &&
           TesterBuild::PathFromUtf8(m_state.current_install) == install.dir;
}

bool VersionManagerDialog::ReleaseIsRunning() const {
    return !TesterLauncher::IsTesterBuild();
}

void VersionManagerDialog::Reload() {
    const auto* selected = SelectedBranch();
    const std::string keep = selected ? selected->Key() : std::string{};

    m_state = TesterBuild::LoadState(TesterLauncher::StateFile());
    const auto installs = TesterBuild::ListInstalls(m_layout, m_state);
    m_release_exe = !m_state.release_exe.empty() ? Q(m_state.release_exe)
                    : ReleaseIsRunning()         ? TesterLauncher::SelfExe()
                                                 : QString{};

    m_branches.clear();
    const auto branch_of = [this](const std::optional<TesterBuild::Source>& source) -> Branch& {
        for (auto& branch : m_branches) {
            if (branch.source == source) {
                return branch;
            }
        }
        return m_branches.emplace_back(Branch{source, {}});
    };
    // The current build's branch first, then the followed one, then the rest by newest build.
    for (const auto& install : installs) {
        if (!m_state.current_install.empty() &&
            TesterBuild::PathFromUtf8(m_state.current_install) == install.dir) {
            branch_of(install.source).installs.push_back(install);
        }
    }
    if (m_state.source) {
        branch_of(m_state.source);
    }
    for (const auto& install : installs) {
        auto& branch = branch_of(install.source);
        if (std::ranges::none_of(branch.installs, [&](const auto& known) {
                return known.dir == install.dir;
            })) {
            branch.installs.push_back(install);
        }
    }
    for (const auto& source : m_saved) {
        branch_of(source);
    }
    // Unknown sources last.
    std::ranges::stable_partition(m_branches, [](const Branch& branch) {
        return branch.source.has_value();
    });
    if (!m_release_exe.isEmpty()) {
        m_branches.insert(m_branches.begin(), Branch{.release = true});
    }
    FillBranches(keep);
}

void VersionManagerDialog::FillBranches(const std::string& keep) {
    const int previous_row = m_branch_list->currentRow();

    {
        const QSignalBlocker blocker(m_branch_list);
        m_branch_list->clear();
        for (const auto& branch : m_branches) {
            if (branch.release) {
                auto* item = new QListWidgetItem(tr("Release"), m_branch_list);
                if (!m_state.start_tester) {
                    QFont font = item->font();
                    font.setBold(true);
                    item->setFont(font);
                }
                item->setToolTip(m_state.start_tester
                                     ? tr("The release build.")
                                     : tr("The release build; Eglantine starts with it."));
                continue;
            }
            QString text = branch.source ? Q(branch.source->DisplayName()) : tr("Unknown source");
            if (!branch.installs.empty()) {
                text += QStringLiteral("  (%1)").arg(branch.installs.size());
            }
            auto* item = new QListWidgetItem(text, m_branch_list);
            QStringList tips;
            if (branch.source && branch.source == m_state.source) {
                tips << tr("Followed for updates.");
            }
            if (branch.source && std::ranges::find(m_saved, *branch.source) != m_saved.end()) {
                tips << tr("Added by hand; right-click to remove it from the list.");
            }
            if (std::ranges::any_of(branch.installs,
                                    [this](const auto& i) { return IsStarting(i); })) {
                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
                tips << tr("Holds the build Eglantine starts with.");
            }
            tips << tr("%n build(s) installed.", nullptr, static_cast<int>(branch.installs.size()));
            item->setToolTip(tips.join(QLatin1Char('\n')));
        }
    }

    if (m_branches.empty()) {
        m_commit_list->clear();
        m_shown.clear();
        m_status->setText(tr("No experimental builds yet. Add a branch or pull request with "
                             "Add Branch... and build it."));
        UpdateButtons();
        return;
    }

    int row = 0;
    for (size_t i = 0; i < m_branches.size(); ++i) {
        const auto& branch = m_branches[i];
        const bool match =
            !keep.empty() ? branch.Key() == keep
                          : (branch.release ? !m_state.start_tester
                                            : std::ranges::any_of(branch.installs, [this](
                                                                                       const auto& i) {
                                                  return IsStarting(i);
                                              }));
        if (match) {
            row = static_cast<int>(i);
            break;
        }
    }
    if (row == previous_row) {
        // Same row, new contents: currentRowChanged would not fire.
        m_branch_list->setCurrentRow(row);
        OnBranchSelected();
    } else {
        m_branch_list->setCurrentRow(row);
    }
}

void VersionManagerDialog::OnBranchSelected() {
    const auto* branch = SelectedBranch();
    if (!branch) {
        return;
    }
    if (branch->release) {
        CommitList list;
        const QString what =
            ReleaseIsRunning() ? Q(Common::BuildDisplayName()) : m_release_exe;
        list.commits.push_back({.message = QString{tr("Release build") + QLatin1Char('\n') + what}
                                               .toStdString()});
        ShowCommits(list);
        return;
    }
    if (!branch->source) {
        CommitList list;
        for (const auto& install : branch->installs) {
            list.commits.push_back({.sha = install.sha,
                                    .message = tr("Build of an unknown branch").toStdString(),
                                    .time = static_cast<u64>(install.built_at)});
        }
        list.local = true;
        ShowCommits(list);
        return;
    }

    const auto key = branch->source->ToString();
    if (const auto it = m_cache.find(key); it != m_cache.end()) {
        ShowCommits(it->second);
        return;
    }

    m_commit_list->clear();
    m_shown.clear();
    m_status->setText(tr("Reading the commits of %1...").arg(Q(branch->source->DisplayName())));
    UpdateButtons();

    // The checkout's refs/tester/head is the last source fetched; for another one, the history of
    // its newest install.
    std::string local_ref;
    if (branch->source == m_state.source) {
        local_ref = "refs/tester/head";
    } else if (!branch->installs.empty()) {
        local_ref = branch->installs.front().sha;
    }
    m_watcher.setFuture(QtConcurrent::run(
        [source = *branch->source, layout = m_layout, local_ref, key]() -> CommitList {
            CommitList list{.key = key};
            const auto remote = source.kind == TesterBuild::Source::Kind::Branch
                                    ? Common::Net::GetBranchCommits(source.branch, CommitCount)
                                    : Common::Net::GetPullRequestCommits(source.pr);
            if (remote.ok && !remote.commits.empty()) {
                list.commits = remote.commits;
                return list;
            }
            list.local = true;
            list.status = remote.status;

            std::error_code ec;
            const auto git = TesterBuild::FindProgram(
                "git", qEnvironmentVariable("PATH").toStdString());
            if (git.empty() || local_ref.empty() || !fs::exists(layout.src / ".git", ec)) {
                return list;
            }
            const auto step = TesterBuild::LogStep(layout, git, local_ref, CommitCount);
            QProcess process;
            QStringList args;
            for (const auto& arg : step.args) {
                args << Q(arg);
            }
            process.start(PathText(step.program), args);
            if (process.waitForFinished(15000) && process.exitStatus() == QProcess::NormalExit &&
                process.exitCode() == 0) {
                list.commits = TesterBuild::ParseLog(process.readAllStandardOutput().toStdString());
            }
            return list;
        }));
}

void VersionManagerDialog::ShowCommits(const CommitList& list) {
    const auto* branch = SelectedBranch();
    if (!branch) {
        return;
    }

    m_shown = list.commits;
    // Installs older than the commits listed still get a row.
    for (const auto& install : branch->installs) {
        if (std::ranges::none_of(m_shown, [&](const auto& c) { return Matches(install, c.sha); })) {
            m_shown.push_back({.sha = install.sha,
                               .message = tr("(older than the commits listed)").toStdString(),
                               .time = static_cast<u64>(install.built_at)});
        }
    }
    m_first_is_latest = !list.local && !list.commits.empty();

    int current_row = -1;

    const QSignalBlocker blocker(m_commit_list);
    m_commit_list->clear();
    for (size_t i = 0; i < m_shown.size(); ++i) {
        const auto& commit = m_shown[i];
        RowBadges badges;
        badges.latest = m_first_is_latest && i == 0;
        badges.newest_fetched = list.local && branch->source && !list.commits.empty() && i == 0;
        for (const auto& install : branch->installs) {
            if (!Matches(install, commit.sha)) {
                continue;
            }
            badges.built = true;
            badges.current = IsStarting(install);
            badges.running = IsRunning(install);
        }
        if (branch->release) {
            badges.current = !m_state.start_tester;
            badges.running = ReleaseIsRunning();
        }

        auto* item = new QListWidgetItem(m_commit_list);
        auto* row = CommitRow(commit, badges, m_commit_list);
        StyleRow(row, false);
        item->setSizeHint(row->sizeHint());
        if (badges.current) {
            QColor tint = palette().color(QPalette::Highlight);
            tint.setAlpha(45);
            item->setBackground(tint);
            current_row = static_cast<int>(i);
        }
        m_commit_list->setItemWidget(item, row);
    }

    QStringList status;
    if (list.local && branch->source) {
        status << (list.status != 0
                       ? tr("GitHub could not be asked (HTTP %1); showing the commits of the last "
                            "fetch, so newer ones may be missing.")
                             .arg(list.status)
                       : tr("GitHub could not be reached; showing the commits of the last fetch, "
                            "so newer ones may be missing."));
    }
    if (m_shown.empty()) {
        status << tr("No commits to show.");
    } else if (m_first_is_latest) {
        status << tr("The newest commit of %1 is %2 (selected).")
                      .arg(Q(branch->source->DisplayName()), Q(m_shown.front().sha.substr(0, 10)));
    }
    if (current_row >= 0) {
        status << tr("The highlighted build is the one Eglantine starts with.");
    }
    m_status->setText(status.join(QLatin1Char(' ')));

    if (!m_shown.empty()) {
        m_commit_list->setCurrentRow(0);
        StyleRow(m_commit_list->itemWidget(m_commit_list->item(0)), true);
        m_commit_list->scrollToTop();
    }
    UpdateButtons();
}

void VersionManagerDialog::AddBranch() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add Branch"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);
    layout->setContentsMargins(20, 20, 20, 16);

    auto* title = new QLabel(tr("Add a branch or pull request"), &dialog);
    QFont title_font = title->font();
    title_font.setPointSizeF(title_font.pointSizeF() * 1.2);
    title_font.setBold(true);
    title->setFont(title_font);
    layout->addWidget(title);

    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText(tr("Search branches, or type a pull request number"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);

    auto* matches = new QListWidget(&dialog);
    matches->setTextElideMode(Qt::ElideMiddle);
    layout->addWidget(matches, 1);

    auto* status = new QLabel(&dialog);
    status->setWordWrap(true);
    layout->addWidget(status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Add"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    bool loading = m_branch_names.isEmpty();
    QString load_error;
    const auto fill = [&] {
        constexpr int MaxShown = 500;
        matches->clear();
        const QString text = search->text().trimmed();
        const auto add = [&](const QString& label, const TesterBuild::Source& source) {
            auto* item = new QListWidgetItem(label, matches);
            item->setData(Qt::UserRole, Q(source.ToString()));
        };

        std::optional<TesterBuild::Source> typed;
        if (!text.isEmpty()) {
            typed = TesterBuild::ParseSource(text.toStdString(), Common::g_build_auto_update_repo);
        }
        if (typed && typed->kind == TesterBuild::Source::Kind::PullRequest) {
            add(tr("Pull request #%1").arg(typed->pr), *typed);
        }

        const auto tokens = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        int found = 0;
        for (const auto& name : m_branch_names) {
            if (!std::ranges::all_of(tokens, [&](const QString& token) {
                    return name.contains(token, Qt::CaseInsensitive);
                })) {
                continue;
            }
            if (++found <= MaxShown) {
                add(name, TesterBuild::Source{TesterBuild::Source::Kind::Branch,
                                              name.toStdString(), 0});
            }
        }

        if (loading) {
            status->setText(tr("Reading the branches..."));
        } else if (!load_error.isEmpty()) {
            status->setText(load_error);
        } else if (found > MaxShown) {
            status->setText(tr("%1 branches match; showing the first %2. Type more to narrow it.")
                                .arg(found)
                                .arg(MaxShown));
        } else {
            status->setText(tr("%n branch(es) match.", nullptr, found));
        }
        matches->setCurrentRow(matches->count() > 0 ? 0 : -1);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(matches->currentItem() != nullptr);
    };
    connect(search, &QLineEdit::textChanged, &dialog, fill);
    connect(matches, &QListWidget::currentItemChanged, &dialog, [&] {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(matches->currentItem() != nullptr);
    });
    connect(matches, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    connect(search, &QLineEdit::returnPressed, &dialog, [&] {
        if (matches->currentItem()) {
            dialog.accept();
        }
    });

    QFutureWatcher<Common::Net::BranchList> watcher;
    if (loading) {
        connect(&watcher, &QFutureWatcher<Common::Net::BranchList>::finished, &dialog, [&] {
            const auto result = watcher.result();
            loading = false;
            for (const auto& name : result.names) {
                m_branch_names << Q(name);
            }
            m_branch_names.sort(Qt::CaseInsensitive);
            if (!result.ok) {
                load_error = result.status != 0
                                 ? tr("Could not read the branches (HTTP %1). Please try "
                                      "again later.")
                                       .arg(result.status)
                                 : tr("Could not reach GitHub. Check your internet connection "
                                      "and try again.");
            }
            fill();
        });
        watcher.setFuture(QtConcurrent::run([] { return Common::Net::GetAllBranches(); }));
    }
    fill();
    dialog.resize(480, 520);
    search->setFocus();

    const bool accepted = dialog.exec() == QDialog::Accepted;
    watcher.disconnect();
    if (!accepted || !matches->currentItem()) {
        return;
    }
    const auto source = TesterBuild::ParseSource(
        matches->currentItem()->data(Qt::UserRole).toString().toStdString(), {});
    if (!source) {
        return;
    }
    const bool listed = std::ranges::any_of(
        m_branches, [&](const Branch& branch) { return branch.source == source; });
    if (!listed) {
        m_saved.push_back(*source);
        SaveSources();
        Reload();
    }
    SelectSource(*source);
}

void VersionManagerDialog::ShowBranchMenu(const QPoint& pos) {
    const int row = m_branch_list->row(m_branch_list->itemAt(pos));
    if (row < 0 || row >= static_cast<int>(m_branches.size()) || !m_branches[row].source) {
        return;
    }
    const auto source = *m_branches[row].source;
    const auto it = std::ranges::find(m_saved, source);
    if (it == m_saved.end()) {
        return;
    }
    QMenu menu(this);
    const auto* remove = menu.addAction(tr("Remove from List"));
    if (menu.exec(m_branch_list->viewport()->mapToGlobal(pos)) != remove) {
        return;
    }
    m_saved.erase(it);
    m_cache.erase(source.ToString());
    SaveSources();
    Reload();
}

void VersionManagerDialog::SelectSource(const TesterBuild::Source& source) {
    for (size_t i = 0; i < m_branches.size(); ++i) {
        if (m_branches[i].source == source) {
            m_branch_list->setCurrentRow(static_cast<int>(i));
            return;
        }
    }
}

void VersionManagerDialog::SaveSources() {
    std::error_code ec;
    if (!TesterBuild::SaveSavedSources(TesterBuild::SavedSourcesPath(m_layout), m_saved, ec)) {
        QMessageBox::warning(this, tr("Version Manager"),
                             tr("Could not save the branch list: %1").arg(Q(ec.message())));
    }
}

void VersionManagerDialog::UpdateButtons() {
    const auto* branch = SelectedBranch();
    const auto commit = SelectedCommit();
    if (branch && branch->release) {
        const bool running = ReleaseIsRunning();
        m_build->setEnabled(false);
        m_build->setText(tr("Build"));
        m_delete->setEnabled(false);
        m_delete->setToolTip({});
        m_launch->setEnabled(!running);
        m_launch->setToolTip(running ? tr("This build is running.")
                                     : tr("Switch back to the release build and restart "
                                          "Eglantine with it."));
        return;
    }
    const auto* install = SelectedInstall();
    const bool running = install && IsRunning(*install);

    m_build->setEnabled(branch && branch->source && commit &&
                        TesterBuild::ParseRevParse(commit->sha).has_value());
    m_build->setText(install ? tr("Rebuild") : tr("Build"));
    m_build->setToolTip(tr("Build this commit and switch to it. Eglantine closes once the build "
                           "starts."));
    m_delete->setEnabled(install && !running);
    m_delete->setToolTip(running ? tr("This build is running.") : QString{});
    m_launch->setEnabled(install && !running);
    m_launch->setToolTip(running ? tr("This build is running.")
                                 : tr("Switch to this build and restart Eglantine with it."));
}

const VersionManagerDialog::Branch* VersionManagerDialog::SelectedBranch() const {
    const int row = m_branch_list->currentRow();
    return row >= 0 && row < static_cast<int>(m_branches.size()) ? &m_branches[row] : nullptr;
}

std::optional<Common::Net::Commit> VersionManagerDialog::SelectedCommit() const {
    const int row = m_commit_list->currentRow();
    if (row < 0 || row >= static_cast<int>(m_shown.size())) {
        return std::nullopt;
    }
    return m_shown[row];
}

const TesterBuild::InstallInfo* VersionManagerDialog::SelectedInstall() const {
    const auto* branch = SelectedBranch();
    const auto commit = SelectedCommit();
    if (!branch || !commit) {
        return nullptr;
    }
    for (const auto& install : branch->installs) {
        if (Matches(install, commit->sha)) {
            return &install;
        }
    }
    return nullptr;
}

bool VersionManagerDialog::IsRunning(const TesterBuild::InstallInfo& install) const {
    return TesterLauncher::IsRunningInstall(install);
}

bool VersionManagerDialog::SaveState() {
    std::error_code ec;
    if (TesterBuild::SaveState(TesterLauncher::StateFile(), m_state, ec)) {
        return true;
    }
    QMessageBox::warning(this, tr("Version Manager"),
                         tr("Could not save the experimental build state: %1")
                             .arg(Q(ec.message())));
    return false;
}

void VersionManagerDialog::BuildSelected() {
    const auto* branch = SelectedBranch();
    const auto commit = SelectedCommit();
    if (!branch || !branch->source || !commit) {
        return;
    }
    // The newest commit goes without --commit, so a CI build of it can be used.
    const bool newest = m_first_is_latest && m_commit_list->currentRow() == 0;
    const auto exe = TesterLauncher::BuilderExe(m_state);
    if (!TesterLauncher::StartBuilder(exe, SelectedInstall() != nullptr,
                                      Q(branch->source->ToString()), true,
                                      newest ? QString{} : Q(commit->sha))) {
        QMessageBox::warning(this, tr("Version Manager"), tr("Could not start %1.").arg(exe));
        return;
    }
    accept();
}

void VersionManagerDialog::DeleteSelected() {
    const auto* install = SelectedInstall();
    if (!install || IsRunning(*install)) {
        return;
    }
    const auto* branch = SelectedBranch();
    const QString name = branch && branch->source ? Q(branch->source->DisplayName())
                                                  : tr("an unknown branch");
    if (QMessageBox::question(this, tr("Delete Instance"),
                              tr("Delete the build of %1 at %2?\n\n%3")
                                  .arg(name, Q(install->sha.substr(0, 10)),
                                       PathText(install->dir)),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    std::error_code ec;
    const auto info = *install;
    if (!TesterBuild::RemoveInstall(m_state, info, ec)) {
        QMessageBox::warning(this, tr("Delete Instance"),
                             tr("Could not delete %1: %2").arg(PathText(info.dir), Q(ec.message())));
        Reload();
        return;
    }
    SaveState();
    Reload();
}

void VersionManagerDialog::LaunchRelease() {
    if (ReleaseIsRunning() || m_release_exe.isEmpty()) {
        return;
    }
    if (QMessageBox::question(this, tr("Launch"),
                              tr("Eglantine closes and starts again as the release build."),
                              QMessageBox::Ok | QMessageBox::Cancel,
                              QMessageBox::Ok) != QMessageBox::Ok) {
        return;
    }
    QWidget* const main_window = parentWidget() ? parentWidget()->window() : nullptr;
    accept();
    if (main_window && !main_window->close()) {
        return;
    }
    // --release makes the release the build to start from now on.
    if (!TesterLauncher::StartDetached(m_release_exe, {QStringLiteral("--release")})) {
        QMessageBox::warning(nullptr, tr("Launch"), tr("Could not start %1.").arg(m_release_exe));
    }
}

void VersionManagerDialog::LaunchSelected() {
    if (const auto* branch = SelectedBranch(); branch && branch->release) {
        LaunchRelease();
        return;
    }
    const auto* install = SelectedInstall();
    if (!install || IsRunning(*install)) {
        return;
    }
    const auto info = *install;
    if (QMessageBox::question(this, tr("Launch"), TesterLauncher::LaunchQuestion(info),
                              QMessageBox::Ok | QMessageBox::Cancel,
                              QMessageBox::Ok) != QMessageBox::Ok) {
        return;
    }
    QWidget* const main_window = parentWidget() ? parentWidget()->window() : nullptr;
    accept();
    TesterLauncher::SwitchAndLaunch(info, main_window);
}
