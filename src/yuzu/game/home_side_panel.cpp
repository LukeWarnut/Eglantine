// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QVBoxLayout>

#include "yuzu/game/home_side_panel.h"

namespace HomeGlyphs {

namespace {
QPixmap MakeCanvas(qreal dpr, int size) {
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    return pixmap;
}
} // namespace

QIcon Close(const QColor& color, qreal dpr, int size) {
    QPixmap pixmap = MakeCanvas(dpr, size);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 20.0, size / 20.0);
    p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(5.5, 5.5), QPointF(14.5, 14.5));
    p.drawLine(QPointF(14.5, 5.5), QPointF(5.5, 14.5));
    return QIcon(pixmap);
}

QIcon Brush(const QColor& color, qreal dpr, int size) {
    QPixmap pixmap = MakeCanvas(dpr, size);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 20.0, size / 20.0);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    // Drawn upright around the origin, then tilted so the handle points to the top right.
    p.translate(10.5, 9.5);
    p.rotate(45);
    p.drawRoundedRect(QRectF(-1.5, -10.5, 3.0, 8.0), 1.5, 1.5);
    p.drawRoundedRect(QRectF(-2.6, -2.0, 5.2, 3.2), 0.8, 0.8);

    QPainterPath bristles;
    bristles.moveTo(-2.8, 1.9);
    bristles.lineTo(2.8, 1.9);
    bristles.cubicTo(3.2, 5.0, 1.6, 7.6, -0.6, 9.4);
    bristles.cubicTo(-0.2, 7.2, -3.4, 5.6, -2.8, 1.9);
    p.drawPath(bristles);
    return QIcon(pixmap);
}

} // namespace HomeGlyphs

/// The strip along the panel's left edge that resizes it.
class HomeSidePanelGrip : public QWidget {
public:
    static constexpr int Width = 6;

    explicit HomeSidePanelGrip(HomeSidePanel* panel_) : QWidget{panel_}, panel{panel_} {
        setCursor(Qt::SizeHorCursor);
        setMouseTracking(true);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        dragging = true;
        press_x = event->globalPosition().x();
        press_width = panel->PreferredWidth();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!dragging) {
            return;
        }
        // The panel sits on the right, so dragging left widens it.
        const int width =
            panel->ClampWidth(press_width + qRound(press_x - event->globalPosition().x()));
        if (width != panel->PreferredWidth()) {
            panel->preferred_width = width;
            panel->updateGeometry();
            emit panel->WidthDragged(width);
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (!dragging || event->button() != Qt::LeftButton) {
            return;
        }
        dragging = false;
        update();
        emit panel->WidthChosen(panel->PreferredWidth());
    }

    void enterEvent(QEnterEvent*) override {
        hovered = true;
        update();
    }

    void leaveEvent(QEvent*) override {
        hovered = false;
        update();
    }

    void paintEvent(QPaintEvent*) override {
        if (hovered || dragging) {
            QPainter p(this);
            p.fillRect(QRect(0, 0, 2, height()), palette().color(QPalette::Highlight));
        }
    }

private:
    HomeSidePanel* panel;
    qreal press_x = 0.0;
    int press_width = 0;
    bool dragging = false;
    bool hovered = false;
};

HomeSidePanel::HomeSidePanel(QWidget* parent) : QFrame{parent} {
    setFrameShape(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    setMinimumWidth(0);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Window);
    setFocusPolicy(Qt::StrongFocus);

    auto* outer = new QVBoxLayout(this);
    // Leaves room for the border painted in paintEvent.
    outer->setContentsMargins(1, 0, 0, 0);
    outer->setSpacing(0);

    scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);

    content = new QWidget(scroll);
    scroll->setWidget(content);

    grip = new HomeSidePanelGrip(this);
    grip->raise();
}

QSize HomeSidePanel::sizeHint() const {
    return {preferred_width, QFrame::sizeHint().height()};
}

int HomeSidePanel::ClampWidth(int width) const {
    // Leave the games some room beside a docked panel.
    int limit = MaxPanelWidth;
    if (parentWidget()) {
        limit = std::min(limit, parentWidget()->width() - 240);
    }
    return std::clamp(width, MinPanelWidth, std::max(MinPanelWidth, limit));
}

void HomeSidePanel::SetPreferredWidth(int width) {
    preferred_width = std::clamp(width, MinPanelWidth, MaxPanelWidth);
    updateGeometry();
}

void HomeSidePanel::resizeEvent(QResizeEvent* event) {
    QFrame::resizeEvent(event);
    grip->setGeometry(0, 0, HomeSidePanelGrip::Width, height());
    grip->raise();
}

void HomeSidePanel::SetFooter(QWidget* footer_) {
    footer = footer_;
    layout()->addWidget(footer);
}

QLabel* HomeSidePanel::MakeHeading(QWidget* parent, qreal scale) {
    auto* label = new QLabel(parent);
    QFont font = label->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * scale);
    label->setFont(font);
    return label;
}

void HomeSidePanel::paintEvent(QPaintEvent* event) {
    QFrame::paintEvent(event);
    // Mid is black on some dark palettes, so derive the border from the text colour.
    QColor color = palette().color(QPalette::WindowText);
    color.setAlpha(70);
    QPainter p(this);
    p.fillRect(QRect(0, 0, 1, height()), color);
    if (footer && footer->isVisible()) {
        p.fillRect(QRect(1, footer->geometry().top(), width() - 1, 1), color);
    }
}

void HomeSidePanel::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        emit CloseRequested();
        return;
    }
    QFrame::keyPressEvent(event);
}

void HomeSidePanel::changeEvent(QEvent* event) {
    // Queued so derived panels are fully constructed, and because setting child style sheets
    // while Qt is still applying a style change crashes it.
    const auto queue = [this](bool& flag, void (HomeSidePanel::*hook)()) {
        if (flag) {
            return;
        }
        flag = true;
        QMetaObject::invokeMethod(
            this,
            [this, &flag, hook] {
                flag = false;
                (this->*hook)();
            },
            Qt::QueuedConnection);
    };
    switch (event->type()) {
    case QEvent::LanguageChange:
        queue(language_refresh_queued, &HomeSidePanel::OnLanguageChanged);
        break;
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
        queue(palette_refresh_queued, &HomeSidePanel::OnPaletteChanged);
        update();
        break;
    default:
        break;
    }
    QFrame::changeEvent(event);
}
