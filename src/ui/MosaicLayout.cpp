#include "ui/MosaicLayout.h"

#include <algorithm>
#include <cmath>

namespace pf::ui {
namespace {

constexpr qreal kMinAspect = 0.25;
constexpr qreal kMaxAspect = 4.0;

qreal clampedAspect(qreal aspect)
{
    if (!(aspect > 0.0) || !std::isfinite(aspect)) {
        return 1.0;
    }
    return std::clamp(aspect, kMinAspect, kMaxAspect);
}

} // namespace

MosaicLayout MosaicLayout::compute(const QList<qreal> &aspects, int width, int targetHeight,
                                   int gap)
{
    MosaicLayout layout;
    layout.width = width;
    layout.targetHeight = targetHeight;
    const int count = static_cast<int>(aspects.size());
    layout.rects.resize(count);
    if (count == 0 || width <= 0 || targetHeight <= 0) {
        return layout;
    }

    // The row height (gap included) at which `items` fill the width exactly.
    const auto justifiedHeight = [&](int items, qreal aspectSum) {
        return ((width - (items * gap)) / aspectSum) + gap;
    };

    int top = 0;
    int first = 0;
    while (first < count) {
        int end = first;
        qreal sum = 0;
        bool closed = false;
        while (end < count) {
            const qreal next = sum + clampedAspect(aspects.at(end));
            const int items = end - first + 1;
            if (justifiedHeight(items, next) <= targetHeight) {
                // Keep the item that tipped the row only if that lands nearer
                // the target than stopping short of it.
                const bool keep =
                    items == 1 || std::abs(justifiedHeight(items, next) - targetHeight) <=
                                      std::abs(justifiedHeight(items - 1, sum) - targetHeight);
                if (keep) {
                    sum = next;
                    ++end;
                }
                closed = true;
                break;
            }
            sum = next;
            ++end;
        }

        const int items = end - first;
        const bool last = end >= count && !closed;
        int height = targetHeight;
        if (!last) {
            height = std::clamp(static_cast<int>(std::floor(justifiedHeight(items, sum))),
                                targetHeight / 2, targetHeight * 2);
        }

        if (last) {
            int x = 0;
            for (int i = first; i < end; ++i) {
                const int w = std::max(
                    1, static_cast<int>(std::floor(clampedAspect(aspects.at(i)) * (height - gap))) +
                           gap);
                layout.rects[i] = QRect(x, top, w, height);
                x += w;
            }
        } else {
            // Whole-pixel widths, then the leftover pixels one each from the
            // left so the row adds up to the width exactly.
            QList<int> widths;
            int used = 0;
            for (int i = first; i < end; ++i) {
                const int w = std::max(
                    1 + gap,
                    static_cast<int>(std::floor(clampedAspect(aspects.at(i)) * (height - gap))) +
                        gap);
                widths.append(w);
                used += w;
            }
            int extra = width - used;
            for (int i = 0; extra != 0 && !widths.isEmpty(); i = (i + 1) % widths.size()) {
                const int step = extra > 0 ? 1 : -1;
                if (widths[i] + step >= 1 + gap) {
                    widths[i] += step;
                    extra -= step;
                } else if (std::ranges::all_of(widths, [&](int w) { return w <= 1 + gap; })) {
                    break;
                }
            }
            int x = 0;
            for (int i = first; i < end; ++i) {
                layout.rects[i] = QRect(x, top, widths.at(i - first), height);
                x += widths.at(i - first);
            }
        }

        layout.rows.append({first, items, top, height});
        top += height;
        first = end;
    }
    return layout;
}

int MosaicLayout::rowOf(int item) const
{
    if (rows.isEmpty()) {
        return -1;
    }
    const auto it = std::upper_bound(rows.cbegin(), rows.cend(), item,
                                     [](int value, const Row &row) { return value < row.first; });
    return static_cast<int>(std::max<qsizetype>(0, (it - rows.cbegin()) - 1));
}

int MosaicLayout::verticalNeighbour(int item, int delta) const
{
    if (rows.isEmpty() || rects.isEmpty()) {
        return -1;
    }
    const int from = std::clamp(item, 0, static_cast<int>(rects.size()) - 1);
    const int target = std::clamp(rowOf(from) + delta, 0, static_cast<int>(rows.size()) - 1);
    const Row &row = rows.at(target);
    const int centre = rects.at(from).center().x();
    int best = row.first;
    int bestDistance = std::abs(rects.at(best).center().x() - centre);
    for (int i = row.first + 1; i < row.first + row.count; ++i) {
        const int distance = std::abs(rects.at(i).center().x() - centre);
        if (distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

QPair<int, int> MosaicLayout::itemsInBand(int top, int bottom) const
{
    if (rows.isEmpty() || bottom <= top) {
        return {-1, -1};
    }
    const auto firstRow =
        std::ranges::find_if(rows, [top](const Row &row) { return row.top + row.height > top; });
    if (firstRow == rows.cend()) {
        return {-1, -1};
    }
    const auto lastRow =
        std::ranges::find_if(rows, [bottom](const Row &row) { return row.top >= bottom; });
    const auto lastIncluded = lastRow == rows.cbegin() ? lastRow : std::prev(lastRow);
    if (lastIncluded < firstRow) {
        return {-1, -1};
    }
    return {firstRow->first, lastIncluded->first + lastIncluded->count - 1};
}

} // namespace pf::ui
