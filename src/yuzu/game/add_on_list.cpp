// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <type_traits>

#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QStyle>
#include <QVBoxLayout>

#include "yuzu/game/add_on_list.h"

namespace {

constexpr int VersionWidth = 140;

void SetMuted(QWidget* widget) {
    QPalette pal = widget->palette();
    QColor color = pal.color(QPalette::WindowText);
    color.setAlpha(150);
    pal.setColor(QPalette::WindowText, color);
    widget->setPalette(pal);
}

/// A check box or radio button whose label elides to the width it gets: by default neither
/// shrinks below its full label, and add-on names can be very long.
template <typename Base>
class ElidedButton : public Base {
public:
    ElidedButton(const QString& text, QWidget* parent) : Base{parent}, full_text{text} {
        this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        this->setToolTip(text);
        this->setText(text);
        this->setCursor(Qt::PointingHandCursor);
    }

    QSize sizeHint() const override {
        return {this->fontMetrics().horizontalAdvance(full_text) + LabelOffset(),
                Base::sizeHint().height()};
    }
    QSize minimumSizeHint() const override {
        return {LabelOffset() + this->fontMetrics().horizontalAdvance(QStringLiteral("M...")),
                Base::minimumSizeHint().height()};
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        Base::resizeEvent(event);
        this->setText(this->fontMetrics().elidedText(full_text, Qt::ElideRight,
                                                     std::max(0, this->width() - LabelOffset())));
    }

private:
    int LabelOffset() const {
        constexpr bool radio = std::is_same_v<Base, QRadioButton>;
        QStyle* style = this->style();
        return style->pixelMetric(radio ? QStyle::PM_ExclusiveIndicatorWidth
                                        : QStyle::PM_IndicatorWidth,
                                  nullptr, this) +
               style->pixelMetric(radio ? QStyle::PM_RadioButtonLabelSpacing
                                        : QStyle::PM_CheckBoxLabelSpacing,
                                  nullptr, this) +
               4;
    }

    QString full_text;
};

} // namespace

AddOnList::AddOnList(QWidget* parent) : QWidget{parent} {
    layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    SetAddOns({}, false);
}

void AddOnList::AddNote(const QString& text) {
    auto* note = new QLabel(text, this);
    note->setWordWrap(true);
    SetMuted(note);
    layout->addWidget(note);
}

void AddOnList::AddRow(QAbstractButton* button, const QString& version) {
    auto* row = new QWidget(this);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(8);
    button->setParent(row);
    row_layout->addWidget(button, 1);
    if (!version.isEmpty()) {
        auto* label = new QLabel(row);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setText(label->fontMetrics().elidedText(version, Qt::ElideRight, VersionWidth));
        label->setToolTip(version);
        SetMuted(label);
        row_layout->addWidget(label);
    }
    layout->addWidget(row);
}

void AddOnList::SetAddOns(const std::vector<GameAddOn>& add_ons_, bool loaded) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    delete update_group;
    update_group = nullptr;
    base_game = nullptr;
    buttons.clear();
    add_ons = add_ons_;

    if (!loaded) {
        AddNote(tr("Loading..."));
        return;
    }
    if (add_ons.empty()) {
        AddNote(tr("No updates, DLC or mods installed."));
        return;
    }

    buttons.resize(add_ons.size(), nullptr);
    const bool has_update = std::any_of(add_ons.begin(), add_ons.end(),
                                        [](const GameAddOn& add_on) { return add_on.is_update; });
    if (has_update) {
        update_group = new QButtonGroup(this);
        base_game = new ElidedButton<QRadioButton>(tr("Base game, no update"), this);
        update_group->addButton(base_game);
        AddRow(base_game, {});
        bool any_enabled = false;
        for (size_t i = 0; i < add_ons.size(); ++i) {
            if (!add_ons[i].is_update) {
                continue;
            }
            auto* radio = new ElidedButton<QRadioButton>(add_ons[i].name, this);
            radio->setChecked(add_ons[i].enabled);
            any_enabled |= add_ons[i].enabled;
            update_group->addButton(radio);
            buttons[i] = radio;
            AddRow(radio, add_ons[i].version);
        }
        base_game->setChecked(!any_enabled);
        connect(update_group, &QButtonGroup::buttonToggled, this,
                [this](QAbstractButton*, bool checked) {
                    // Each switch toggles two buttons; the one turning on reports the result.
                    if (checked) {
                        OnChanged();
                    }
                });
    }
    for (size_t i = 0; i < add_ons.size(); ++i) {
        if (add_ons[i].is_update) {
            continue;
        }
        auto* check = new ElidedButton<QCheckBox>(add_ons[i].name, this);
        check->setChecked(add_ons[i].enabled);
        connect(check, &QCheckBox::toggled, this, &AddOnList::OnChanged);
        buttons[i] = check;
        AddRow(check, add_ons[i].version);
    }
}

void AddOnList::OnChanged() {
    QStringList disabled;
    for (size_t i = 0; i < add_ons.size(); ++i) {
        add_ons[i].enabled = buttons[i] && buttons[i]->isChecked();
        if (!add_ons[i].enabled) {
            disabled << add_ons[i].key;
        }
    }
    emit DisabledChanged(disabled);
}
