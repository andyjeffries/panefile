#include "ui/modals/CompressModal.h"

#include "ui/ThemePalette.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace pf::ui {

using fs::ArchiveFormat;

CompressModal::CompressModal(QWidget *parent)
    : Modal(parent), m_summary(new QLabel), m_name(new QLineEdit), m_format(new QComboBox),
      m_problem(new QLabel), m_create(new QPushButton)
{
    setSizePercent(46, 34);

    auto *layout = new QVBoxLayout(contentWidget());
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    auto *title = new QLabel(tr("Create Archive"));
    title->setObjectName(QStringLiteral("modalTitle"));
    layout->addWidget(title);

    m_summary->setObjectName(QStringLiteral("modalHint"));
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto *form = new QFormLayout;
    m_name->setObjectName(QStringLiteral("modalInput"));
    form->addRow(tr("Name:"), m_name);

    m_format->setObjectName(QStringLiteral("compressFormat"));
    form->addRow(tr("Format:"), m_format);
    layout->addLayout(form);

    m_problem->setObjectName(QStringLiteral("modalProblem"));
    m_problem->setWordWrap(true);
    m_problem->hide();
    layout->addWidget(m_problem);

    layout->addStretch(1);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);

    auto *cancel = new QPushButton(tr("Cancel"));
    cancel->setFocusPolicy(Qt::NoFocus);
    connect(cancel, &QPushButton::clicked, this, &CompressModal::dismiss);
    buttons->addWidget(cancel);

    m_create->setText(tr("Create"));
    m_create->setDefault(true);
    connect(m_create, &QPushButton::clicked, this, &CompressModal::accept);
    buttons->addWidget(m_create);

    layout->addLayout(buttons);

    // Changing the format rewrites the extension in the name box, so what the
    // user is about to create is always spelled out in front of them.
    connect(m_format, &QComboBox::currentIndexChanged, this, [this] { updateExtension(); });
    connect(m_name, &QLineEdit::textChanged, this, [this] { m_problem->hide(); });
}

fs::ArchiveFormat CompressModal::selectedFormat() const
{
    const int index = m_format->currentIndex();
    if (index < 0 || index >= m_available.size()) {
        return ArchiveFormat::Zip;
    }
    return m_available.at(index);
}

void CompressModal::updateExtension()
{
    QString name = m_name->text();

    // Strip whichever known extension is there and put the current one on, so
    // switching format twice does not give `foo.zip.tar.gz`.
    for (const ArchiveFormat format : {ArchiveFormat::Zip, ArchiveFormat::TarGz,
                                       ArchiveFormat::TarZst, ArchiveFormat::SevenZip}) {
        const QString extension = fs::extensionFor(format);
        if (name.endsWith(extension, Qt::CaseInsensitive)) {
            name.chop(extension.size());
            break;
        }
    }

    m_name->setText(name + fs::extensionFor(selectedFormat()));
}

void CompressModal::start(const QStringList &sources, const QString &destinationDirectory)
{
    m_sources = sources;
    m_directory = destinationDirectory;
    m_problem->hide();

    m_summary->setText(sources.size() == 1
                           ? tr("Archiving “%1”").arg(QFileInfo(sources.constFirst()).fileName())
                           : tr("Archiving %n item(s)", nullptr, static_cast<int>(sources.size())));

    // §7.10: "if supported". Asked of libarchive rather than assumed, because
    // zstd and 7z are compile-time options and offering a format that then
    // fails at the moment of writing is worse than not offering it.
    m_available.clear();
    m_format->clear();

    for (const ArchiveFormat format : {ArchiveFormat::Zip, ArchiveFormat::TarGz,
                                       ArchiveFormat::TarZst, ArchiveFormat::SevenZip}) {
        if (fs::isFormatSupported(format)) {
            m_available.append(format);
            m_format->addItem(fs::displayNameFor(format) +
                              QStringLiteral("  (%1)").arg(fs::extensionFor(format)));
        }
    }

    if (m_available.isEmpty()) {
        m_problem->setText(tr("This build cannot create archives"));
        m_problem->setStyleSheet(
            QStringLiteral("color: %1;").arg(currentPalette().error.name(QColor::HexRgb)));
        m_problem->show();
        m_create->setEnabled(false);
    } else {
        m_create->setEnabled(true);
    }

    // §7.10: "defaulting to the cursor item's basename". Several items have no
    // single basename, so the directory's name is the next best thing.
    const QString base = sources.size() == 1 ? QFileInfo(sources.constFirst()).fileName()
                                             : QFileInfo(destinationDirectory).fileName();

    m_name->setText(base + fs::extensionFor(selectedFormat()));

    showModal();
    m_name->setFocus(Qt::ShortcutFocusReason);

    // The stem is preselected so typing replaces the name and keeps the
    // extension the format chose.
    m_name->setSelection(0, static_cast<int>(base.size()));
}

void CompressModal::accept()
{
    const QString name = m_name->text().trimmed();

    if (name.isEmpty() || name.contains(QLatin1Char('/'))) {
        m_problem->setText(tr("Enter a name without “/”"));
        m_problem->setStyleSheet(
            QStringLiteral("color: %1;").arg(currentPalette().error.name(QColor::HexRgb)));
        m_problem->show();
        return;
    }

    const QString destination = QDir(m_directory).absoluteFilePath(name);
    if (QFileInfo::exists(destination)) {
        // Caught here as well as in the job, so the user can fix it in the box
        // they are already looking at rather than reading it from the footer
        // after the modal has gone.
        m_problem->setText(tr("“%1” already exists").arg(name));
        m_problem->setStyleSheet(
            QStringLiteral("color: %1;").arg(currentPalette().error.name(QColor::HexRgb)));
        m_problem->show();
        return;
    }

    Q_EMIT compressRequested(m_sources, destination, selectedFormat());
    Modal::accept();
}

} // namespace pf::ui
