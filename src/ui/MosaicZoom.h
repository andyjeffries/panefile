#pragma once

#include <QByteArray>
#include <QObject>

namespace pf::ui {

/// The mosaic's target row height: one value for every panel and folder,
/// remembered in `<stateDir>/mosaic.json`.
class MosaicZoom : public QObject
{
    Q_OBJECT

public:
    static constexpr int kMin = 96;
    static constexpr int kMax = 480;
    static constexpr int kDefault = 192;
    static constexpr int kStep = 24;

    static MosaicZoom &instance();

    int rowHeight();
    void setRowHeight(int height);
    void zoomBy(int steps);

    /// Back to the default without touching the file. For tests.
    void clear();

    static QByteArray toJson(int rowHeight);
    /// The default for a missing or garbled file.
    static int fromJson(const QByteArray &bytes);

Q_SIGNALS:
    void rowHeightChanged(int height);

private:
    MosaicZoom() = default;

    void ensureLoaded();
    void save() const;

    int m_rowHeight = kDefault;
    bool m_loaded = false;
};

} // namespace pf::ui
