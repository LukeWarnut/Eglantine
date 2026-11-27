// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QAction>
#include <QBuffer>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QMenu>
#include <QMessageBox>

#include "qt_common/config/uisettings.h"
#include "qt_common/util/game_art.h"
#include "yuzu/game/game_artwork.h"
#include "yuzu/util/icon_search.h"

namespace {

constexpr int HeroMaxWidth = 1280;
constexpr int StoredIconSize = 256;

QByteArray EncodeHeroImage(QImage image) {
    if (image.isNull()) {
        return {};
    }
    image = image.convertToFormat(QImage::Format_RGB888);
    if (image.width() > HeroMaxWidth) {
        image = image.scaledToWidth(HeroMaxWidth, Qt::SmoothTransformation);
    }
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 90);
    return out;
}

QByteArray EncodeIconImage(const QImage& image) {
    if (image.isNull()) {
        return {};
    }
    const int side = std::min(image.width(), image.height());
    const QImage square =
        image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side)
            .scaled(StoredIconSize, StoredIconSize, Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation);
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    square.save(&buffer, "PNG");
    return out;
}

} // namespace

GameArtwork::GameArtwork(QWidget* dialog_parent_)
    : QObject{dialog_parent_}, dialog_parent{dialog_parent_},
      icon_search{new IconSearch(dialog_parent_)} {
    connect(icon_search, &IconSearch::RunningChanged, this, &GameArtwork::BusyChanged);
    connect(icon_search, &IconSearch::IconSelected, this,
            [this](const QByteArray& png, const QString&) { SaveIcon(search_program_id, png); });
    connect(icon_search, &IconSearch::HeroSelected, this,
            [this](const QByteArray& image) { SaveHero(search_program_id, image); });
}

bool GameArtwork::IsBusy() const {
    return icon_search->IsRunning();
}

void GameArtwork::FillMenu(QMenu* menu, bool hero, u64 program_id, const QString& title) {
    menu->clear();
    menu->setToolTipsVisible(true);
    QAction* find = menu->addAction(tr("Find on SteamGridDB..."));
    if (UISettings::values.steamgriddb_api_key.GetValue().empty()) {
        find->setToolTip(tr("Needs a free SteamGridDB API key. You will be asked for it, "
                            "or set it in Settings > General > Web."));
    }
    find->setEnabled(!IsBusy());
    connect(find, &QAction::triggered, this,
            [this, hero, program_id, title] { Find(hero, program_id, title); });
    connect(menu->addAction(tr("Choose File...")), &QAction::triggered, this,
            [this, hero, program_id] { ChooseFile(hero, program_id); });
    const QString path = hero ? QtCommon::GameArt::HeroPath(program_id)
                              : QtCommon::GameArt::IconPath(program_id);
    QAction* reset = menu->addAction(hero ? tr("Remove Banner") : tr("Reset Icon"));
    reset->setEnabled(QFileInfo::exists(path));
    connect(reset, &QAction::triggered, this,
            [this, hero, program_id] { Reset(hero, program_id); });
}

void GameArtwork::Find(bool hero, u64 program_id, const QString& title) {
    if (IsBusy()) {
        return;
    }
    search_program_id = program_id;
    icon_search->StartSteamGridDb(title, program_id, hero ? SgdbArtKind::Hero : SgdbArtKind::Icon);
}

void GameArtwork::ChooseFile(bool hero, u64 program_id) {
    const QString path = QFileDialog::getOpenFileName(
        dialog_parent, hero ? tr("Choose Banner") : tr("Choose Icon"), {},
        tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
    if (path.isEmpty()) {
        return;
    }
    const QImage image(path);
    if (image.isNull()) {
        QMessageBox::warning(
            dialog_parent, tr("Artwork"),
            tr("\"%1\" could not be loaded as an image.").arg(QFileInfo(path).fileName()));
        return;
    }
    if (hero) {
        SaveHero(program_id, EncodeHeroImage(image));
    } else {
        SaveIcon(program_id, EncodeIconImage(image));
    }
}

void GameArtwork::Reset(bool hero, u64 program_id) {
    if (hero) {
        QtCommon::GameArt::RemoveHero(program_id);
        emit HeroChanged(program_id);
    } else {
        QtCommon::GameArt::RemoveIcon(program_id);
        emit IconChanged(program_id);
    }
}

void GameArtwork::SaveHero(u64 program_id, const QByteArray& image) {
    if (!QtCommon::GameArt::SaveHero(program_id, image)) {
        QMessageBox::warning(dialog_parent, tr("Artwork"), tr("The banner could not be saved."));
        return;
    }
    emit HeroChanged(program_id);
}

void GameArtwork::SaveIcon(u64 program_id, const QByteArray& png) {
    if (!QtCommon::GameArt::SaveIcon(program_id, png)) {
        QMessageBox::warning(dialog_parent, tr("Artwork"), tr("The icon could not be saved."));
        return;
    }
    emit IconChanged(program_id);
}
