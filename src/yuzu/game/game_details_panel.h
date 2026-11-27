// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QCache>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QString>

#include "common/common_types.h"
#include "yuzu/game/game_details_info.h"
#include "yuzu/game/home_side_panel.h"

class AddOnList;
class GameArtwork;
class QCheckBox;
class QGridLayout;
class QHBoxLayout;
class QMenu;
class QPushButton;
class QToolButton;
class QVBoxLayout;

/// Banner across the top with the game icon overlapping its lower edge, title and developer
/// beside the icon. While the game's artwork can be changed, buttons to change the banner and
/// the icon stay visible over the artwork.
class GameDetailsHeader : public QWidget {
    Q_OBJECT

public:
    explicit GameDetailsHeader(QWidget* parent = nullptr);

    void SetGame(const GameDetailsInfo& info, const QImage& hero);
    void SetIcon(const QPixmap& icon);
    void SetEditable(bool editable);
    void SetBusy(bool busy);

    QToolButton* BannerEditButton() const {
        return banner_edit;
    }
    QToolButton* IconEditButton() const {
        return icon_edit;
    }

    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;

signals:
    void CloseRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    /// Shows the artwork buttons whenever the game's artwork can be changed.
    void UpdateEditButtons();
    int HeroHeight(int width) const;
    QRect IconRect() const;
    void LayoutLabels();
    void RefreshGlyphs();
    void RefreshDeveloperPalette();

    QImage hero;
    QImage fallback_hero;
    QPixmap icon;
    QLabel* title = nullptr;
    QLabel* developer = nullptr;
    QToolButton* close_button = nullptr;
    QToolButton* banner_edit = nullptr;
    QToolButton* icon_edit = nullptr;
    bool editable = false;
    bool busy = false;
    bool glyph_refresh_queued = false;
};

/// Single line label that elides a path to whatever width it gets and opens it on click.
class PathLinkLabel : public QLabel {
    Q_OBJECT

public:
    explicit PathLinkLabel(QWidget* parent = nullptr);

    void SetPath(const QString& path);

    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void Elide();

    QString path;
};

/// Sidebar opened by clicking a game on the home screen: artwork, info and actions.
class GameDetailsPanel : public HomeSidePanel {
    Q_OBJECT

public:
    explicit GameDetailsPanel(QWidget* parent = nullptr);
    ~GameDetailsPanel() override;

    void SetGame(const GameDetailsInfo& info);
    /// Ignored unless it belongs to the game currently shown.
    void SetExtras(const GameDetailsExtras& extras);
    void SetFavorite(bool favorite);
    void SetIcon(const QPixmap& icon);
    const GameDetailsInfo& Game() const {
        return game;
    }

signals:
    void PlayRequested();
    void ConfigureRequested();
    void FavoriteToggled();
    void MoreRequested(const QPoint& global_pos);
    void IconChanged(u64 program_id);
    /// The shown game's banner file was saved or removed.
    void HeroChanged(u64 program_id);
    void OpenLocationRequested();
    void PrecompileShadersRequested();
    /// The full list of add-ons that should be disabled for the shown game.
    void DisabledAddOnsChanged(const QStringList& disabled);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void OnPaletteChanged() override;
    void OnLanguageChanged() override;

private:
    void RetranslateUI();
    void RefreshGlyphs();
    void RefreshChips();
    void RefreshInfo();
    void RefreshAddOns();
    void RefreshShaders();
    void RefreshArtworkMenus();
    void ReloadHero();
    QImage LoadHero(u64 program_id);

    GameDetailsInfo game;
    GameDetailsExtras extras;
    bool extras_loaded = false;

    GameDetailsHeader* header = nullptr;
    QLayout* chip_row = nullptr;

    QLabel* add_ons_heading = nullptr;
    AddOnList* add_on_list = nullptr;

    QLabel* shaders_heading = nullptr;
    QWidget* shaders_box = nullptr;
    QLabel* pipeline_cache_key = nullptr;
    QLabel* pipeline_cache_value = nullptr;
    QLabel* precompiled_key = nullptr;
    QLabel* precompiled_value = nullptr;
    QPushButton* precompile_button = nullptr;

    QToolButton* technical_toggle = nullptr;
    QWidget* technical_box = nullptr;
    QGridLayout* info_grid = nullptr;
    PathLinkLabel* location_label = nullptr;

    QPushButton* play_button = nullptr;
    QToolButton* configure_button = nullptr;
    QToolButton* favorite_button = nullptr;
    QToolButton* more_button = nullptr;

    QMenu* banner_menu = nullptr;
    QMenu* icon_menu = nullptr;

    GameArtwork* artwork = nullptr;

    /// Decoded banners by program ID, cost in KiB.
    QCache<u64, QImage> hero_cache{48 * 1024};
};
