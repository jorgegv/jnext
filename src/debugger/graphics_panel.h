#pragma once

#include <QImage>
#include <QWidget>

#include <vector>

class QComboBox;
class QLabel;
class QRadioButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;

namespace jnext { namespace dbg { class Debugger; } }

/// An atlas of equal square cells drawn at an integer scale over a checkerboard (the Video panel's
/// convention for transparency). image() is the UNSCALED atlas, ARGB, alpha 0 where the decoder
/// says a pixel is transparent. A click selects a cell.
class GfxAtlas : public QWidget {
    Q_OBJECT
public:
    GfxAtlas(int scale, QWidget* parent = nullptr);

    /// `cell_px` pixels per cell side; `count` cells valid (the rest of the last row stays empty).
    void set_image(const QImage& img, int cell_px, int count);
    const QImage& image() const { return image_; }
    int selected() const { return selected_; }
    void set_selected(int idx);
    int cell_count() const { return count_; }

    QSize sizeHint() const override { return image_.isNull() ? QSize(1, 1) : image_.size() * scale_; }

signals:
    void cell_clicked(int idx);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    QImage image_;
    int scale_;
    int cell_px_ = 16;
    int count_ = 0;
    int selected_ = -1;
};

/// Debugger "Graphics" tab (GH #371): the sprite patterns, the tile definitions and the eight
/// palette banks, read through the facade only. What it shows is the machine at the pause
/// instant (live registers and RAM), not a per-scanline replay.
class GraphicsPanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the debugger backend every read goes through; null shows nothing.
    explicit GraphicsPanel(const jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// Redraw the current sub-tab, only when this panel is visible (its tab is the current one).
    void refresh();

    /// How many sub-tab renders have happened (a test seam: a hidden panel renders none).
    int render_count() const { return render_count_; }

    // The width stays at or under the Video panel's, so opening the window does not widen it.
    QSize sizeHint() const override { return QSize(420, 500); }

protected:
    void showEvent(QShowEvent*) override;

private:
    void create_ui();
    void render_current();
    void render_patterns();
    void render_tiles();
    void render_palettes();
    void update_pattern_info();
    void update_tile_info();

    const jnext::dbg::Debugger* dbg_;
    int render_count_ = 0;
    QTabWidget* sub_tabs_ = nullptr;

    // Patterns
    QRadioButton* fmt8_ = nullptr;
    QRadioButton* fmt4_ = nullptr;
    QComboBox* pat_palette_ = nullptr;
    QSpinBox* pat_offset_ = nullptr;
    GfxAtlas* pat_atlas_ = nullptr;
    QLabel* pat_info_ = nullptr;

    // Tiles
    QComboBox* tile_palette_ = nullptr;
    QSpinBox* tile_offset_ = nullptr;
    GfxAtlas* tile_atlas_ = nullptr;
    QLabel* tile_source_ = nullptr;
    QLabel* tile_info_ = nullptr;
    bool tile_textmode_ = false;
    bool tile_bank7_ = false;
    uint8_t tile_base_ = 0;

    // Palettes
    QComboBox* pal_bank_ = nullptr;
    QTableWidget* pal_table_ = nullptr;
};
