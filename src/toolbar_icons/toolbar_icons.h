#pragma once

// The toolbar icon set (doc/design/ICONSET-DESIGN.md section 4).
//
// The icons are the monochrome SVGs in src/gui/icons/, compiled in as Qt
// resources under :/icons/. Qt does not colour `currentColor` from a widget
// palette, so each SVG is coloured before it is rendered: `currentColor`
// becomes the palette's WindowText (Normal state) and its Disabled WindowText
// (Disabled state), and QSvgRenderer draws it at 16, 24, 32 and 48 px. No Qt
// icon-engine plugin is involved, so none has to be deployed.
//
// A library of its own because BOTH Qt libraries use it (the Main toolbar in
// jnext_gui, the Debug toolbar in jnext_debugger), and putting it in either
// would create a link edge between them that the ENABLE_QT_UI x
// ENABLE_DEBUGGER build matrix forbids.
//
// Every failure is loud: a missing resource, an SVG QSvgRenderer cannot
// parse, one with no `currentColor` (it could not follow the palette) or one
// that renders nothing throws std::runtime_error naming the icon. A blank
// toolbar button is never the result.

#include <QByteArray>
#include <QColor>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QPalette>
#include <QString>
#include <QStringList>

class QAction;
class QWidget;

namespace toolbar_icons {

/// The pixel sizes every icon carries, smallest first.
const QList<int>& sizes();

/// The compiled-in icon names (resource file names without ".svg"), sorted.
QStringList names();

/// The raw SVG of icon `name`. Throws std::runtime_error if it is not a resource.
QByteArray svg(const QString& name);

/// `svg` with `currentColor` replaced by `color`, rendered at px x px
/// (Format_ARGB32_Premultiplied). Throws std::runtime_error if the SVG has no
/// `currentColor`, does not parse, or renders no pixel. `what` names it in the
/// error.
QImage render(const QByteArray& svg, const QColor& color, int px, const QString& what);

/// Icon `name` for `pal`: Normal pixmaps in the Active WindowText, Disabled
/// pixmaps in the Disabled WindowText, at every size(). If `on_name` is not
/// empty, the On state (a checked action) shows that icon instead.
QIcon icon(const QPalette& pal, const QString& name, const QString& on_name = QString());

/// Give `action` icon `name` (and `on_name` for its checked state) rendered
/// from `palette_source`'s palette, and render it again whenever that widget
/// receives QEvent::PaletteChange. The names are kept on the action as the
/// properties "jnext_icon" and "jnext_icon_on".
void bind(QAction* action, QWidget* palette_source, const QString& name,
          const QString& on_name = QString());

} // namespace toolbar_icons
