#pragma once

#include <QWidget>
#include <QLabel>
#include <QImage>

class Emulator;
class QTabWidget;
class QRadioButton;

// ---------------------------------------------------------------------------
// VideoLayerView — displays a single video layer (320×256) for the debugger.
// ---------------------------------------------------------------------------

class VideoLayerView : public QWidget {
    Q_OBJECT
public:
    enum class Layer {
        ULA_PRIMARY,    ///< ULA standard screen (bank 5, pages 10-11)
        ULA_SHADOW,     ///< ULA 128K shadow screen (bank 7, page 14) — port 0x7FFD bit 3
        LAYER2_ACTIVE,  ///< Layer 2 active bank
        LAYER2_SHADOW,  ///< Layer 2 shadow bank
        SPRITES,        ///< Sprite layer (transparent background)
        TILEMAP,        ///< Tilemap layer (transparent background)
    };

    VideoLayerView(Layer layer, const char* title,
                   Emulator* emulator, QWidget* parent = nullptr);

    /// Switch which layer/screen is displayed and force a re-render.
    void setLayer(Layer layer);

    /// Re-render the layer for the given scanline position.
    /// @param vc  Current vertical counter (0-255).  Pass -1 when running
    ///            (shows dim placeholder).
    void refresh(int vc);

    /// Force re-render on the next refresh() call (e.g. after a tab switch).
    void invalidate() { last_vc_ = -2; }

    /// Layout constants shared by all VideoLayerView instances.
    static constexpr int NATIVE_W = 320;
    static constexpr int NATIVE_H = 256;
    static constexpr int DISPLAY_SCALE = 2;   ///< same scale as emulator viewport default
    static constexpr int TITLE_H  = 14;
    static constexpr int MARGIN   = 12;       ///< margin on all sides around the image

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void render_to_image(int vc);

    Layer       layer_;
    QString     title_;
    Emulator*   emulator_;
    QImage      image_;     ///< NATIVE_W × NATIVE_H, ARGB32
    int         last_vc_ = -2;
};

// ---------------------------------------------------------------------------
// VideoPanel — debugger panel showing raster position, layer state,
//              priority, ULA palette, and per-layer sub-panel views.
// ---------------------------------------------------------------------------

class VideoPanel : public QWidget {
    Q_OBJECT
public:
    explicit VideoPanel(Emulator* emulator, QWidget* parent = nullptr);

    /// Update display with current video state.
    void refresh();

    QSize sizeHint() const override { return QSize(600, 600); }

private:
    void create_ui();

    Emulator* emulator_;

    // Raster position
    QLabel* hc_label_       = nullptr;
    QLabel* vc_label_       = nullptr;

    // Layer flags (0=ULA, 1=Layer2, 2=Tilemap, 3=Sprites)
    QLabel* layer_flags_[4] = {};

    // Layer priority flags (0=SLU .. 5=ULS)
    QLabel* prio_flags_[6]  = {};

    // Palette swatch
    QWidget* palette_widget_ = nullptr;

    // Sub-panel layer views (one per tab)
    QTabWidget*      layer_tabs_    = nullptr;
    VideoLayerView*  ula_view_      = nullptr;  ///< ULA tab (primary or shadow)
    VideoLayerView*  l2_view_       = nullptr;  ///< Layer2 tab (active or shadow)
    VideoLayerView*  sprites_view_  = nullptr;
    VideoLayerView*  tilemap_view_  = nullptr;

    // Screen-select radio buttons
    QRadioButton*    ula_rb_primary_  = nullptr;
    QRadioButton*    ula_rb_shadow_   = nullptr;
    QRadioButton*    l2_rb_active_    = nullptr;
    QRadioButton*    l2_rb_shadow_    = nullptr;
};
