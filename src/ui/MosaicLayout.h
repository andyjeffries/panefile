#pragma once

#include <QList>
#include <QPair>
#include <QRect>

namespace pf::ui {

/// A justified layout: each row is filled with items at their own aspect ratio
/// and scaled so the row is exactly as wide as the view, all at roughly one
/// target height. Pure, so the arithmetic can be tested without a widget.
struct MosaicLayout {
    struct Row {
        int first = 0;
        int count = 0;
        int top = 0;
        int height = 0;
    };

    /// Per item, in view coordinates, with the gap included in its size.
    QList<QRect> rects;
    QList<Row> rows;
    int width = 0;
    int targetHeight = 0;

    /// Aspect ratios outside 1:4..4:1 are clamped; invalid ones count as 1.
    /// Every row but the last adds up to exactly `width`; the last keeps the
    /// target height and lines up on the left.
    static MosaicLayout compute(const QList<qreal> &aspects, int width, int targetHeight, int gap);

    int rowOf(int item) const;

    /// The item in the row `delta` rows away whose centre is nearest `item`'s,
    /// clamped to the first and last row; -1 when there are no items.
    int verticalNeighbour(int item, int delta) const;

    /// The first and last items intersecting the band `top`..`bottom`, or
    /// {-1, -1} when there are none.
    QPair<int, int> itemsInBand(int top, int bottom) const;
};

} // namespace pf::ui
