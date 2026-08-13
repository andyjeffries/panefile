#pragma once

#include "fs/jobs/ArchiveJob.h"
#include "ui/modals/Modal.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace pf::ui {

/// §7.10's create-archive modal.
///
/// "A modal picks format (`zip`, `tar.gz`, `tar.zst`, `7z` if supported) and
/// name, defaulting to the cursor item's basename."
///
/// The "if supported" is a runtime question, not a build-time one: zstd and 7z
/// are compile-time options in libarchive, so the list is built by asking the
/// library rather than by assuming.
class CompressModal : public Modal
{
    Q_OBJECT

public:
    explicit CompressModal(QWidget *parent);

    /// The paths to archive and the directory the archive lands in.
    void start(const QStringList &sources, const QString &destinationDirectory);

Q_SIGNALS:
    void compressRequested(const QStringList &sources, const QString &destination,
                           pf::fs::ArchiveFormat format);

protected:
    void accept() override;

private:
    fs::ArchiveFormat selectedFormat() const;
    void updateExtension();

    QLabel *m_summary = nullptr;
    QLineEdit *m_name = nullptr;
    QComboBox *m_format = nullptr;
    QLabel *m_problem = nullptr;
    QPushButton *m_create = nullptr;

    QStringList m_sources;
    QString m_directory;

    /// The formats this build's libarchive can actually write, in the order
    /// they are offered.
    QList<fs::ArchiveFormat> m_available;
};

} // namespace pf::ui
