#pragma once

#include <QFont>
#include <QPointF>
#include <QString>

class QPainter;

namespace pf::ui::keycaps {

/// Key bindings drawn as keycaps, as the help modal and the chord hint show
/// them.
///
/// The text is a rendered binding — "Ctrl+C", or "g h" for a two-key
/// sequence — and each whitespace-separated press becomes one cap. Several
/// bindings for one action are joined with HelpModal's middle-dot separator
/// and drawn as caps with a faint dot between them.
/// The face caps are set in: the monospace face, two pixels under `base`.
QFont font(const QFont &base);

/// Height of one cap.
constexpr qreal kHeight = 20;

/// The width `text` takes when drawn.
qreal width(const QString &text, const QFont &base);

/// Draws `text` with its left edge at `origin.x()`, centred on `origin.y()`,
/// and returns the width used.
qreal paint(QPainter *painter, const QPointF &origin, const QString &text, const QFont &base);

} // namespace pf::ui::keycaps
