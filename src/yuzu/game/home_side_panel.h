// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFrame>
#include <QIcon>

class QLabel;
class QScrollArea;

namespace HomeGlyphs {
QIcon Close(const QColor& color, qreal dpr, int size = 20);
QIcon Brush(const QColor& color, qreal dpr, int size = 20);
} // namespace HomeGlyphs

/// Base for the sidebars that slide in beside the home screen. GameList animates the maximum
/// width between 0 and PreferredWidth(), which the user sets by dragging the left edge.
class HomeSidePanel : public QFrame {
    Q_OBJECT

public:
    static constexpr int PanelWidth = 340;
    static constexpr int MinPanelWidth = 280;
    static constexpr int MaxPanelWidth = 800;

    explicit HomeSidePanel(QWidget* parent = nullptr);

    QSize sizeHint() const override;

    int PreferredWidth() const {
        return preferred_width;
    }
    void SetPreferredWidth(int width);

signals:
    void CloseRequested();
    /// The left edge is being dragged; PreferredWidth() already holds `width`.
    void WidthDragged(int width);
    /// The drag ended at PreferredWidth().
    void WidthChosen(int width);

protected:
    /// The scrolled widget derived panels build their layout on.
    QWidget* Content() const {
        return content;
    }

    /// Pins a widget below the scrolled content, always visible.
    void SetFooter(QWidget* footer);

    static QLabel* MakeHeading(QWidget* parent, qreal scale = 1.15);

    /// Run once after the palette or style changed, outside Qt's style change handling.
    virtual void OnPaletteChanged() {}
    virtual void OnLanguageChanged() {}

    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    friend class HomeSidePanelGrip;

    int ClampWidth(int width) const;

    QWidget* grip = nullptr;
    int preferred_width = PanelWidth;
    QScrollArea* scroll = nullptr;
    QWidget* content = nullptr;
    QWidget* footer = nullptr;
    bool palette_refresh_queued = false;
    bool language_refresh_queued = false;
};
