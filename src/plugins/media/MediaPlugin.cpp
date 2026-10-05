// pf-media: QtMultimedia playback for Quick Look's media renderer (§7.6).
//
// "`QMediaPlayer` playback with scrub bar, `p` to play/pause, plus duration,
// resolution and codec." Video and audio share one renderer: an audio file is a
// video without a picture, and the controls and metadata are the same.

#include "plugins/PluginInterfaces.h"

#include <QAudioOutput>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaFormat>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QObject>
#include <QSlider>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWidget>

namespace pf::plugins {
namespace {

using ui::QuickLookContent;

QString formatTime(qint64 milliseconds)
{
    const qint64 seconds = milliseconds / 1000;
    const qint64 hours = seconds / 3600;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

class MediaPlayerRenderer : public ui::QuickLookRenderer
{
public:
    ~MediaPlayerRenderer() override
    {
        // The widgets outlive this object — they belong to the Quick Look
        // stack — so the lambdas that captured `this` are disconnected first,
        // and the player is then stopped rather than left playing into a
        // renderer that no longer exists.
        if (m_player != nullptr) {
            QObject::disconnect(m_player, nullptr, &m_context, nullptr);
            m_player->stop();
        }
    }

    QString id() const override { return QStringLiteral("media-player"); }

    bool canRender(const QMimeType &mime, const FileEntry &entry) const override
    {
        Q_UNUSED(mime)
        Q_UNUSED(entry)
        return true;
    }

    QWidget *createWidget(QWidget *parent) override
    {
        if (m_root != nullptr) {
            return m_root;
        }

        m_root = new QWidget(parent);
        auto *layout = new QVBoxLayout(m_root);
        layout->setContentsMargins(0, 0, 0, 0);

        m_video = new QVideoWidget(m_root);
        m_video->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        layout->addWidget(m_video, 1);

        m_info = new QLabel(m_root);
        m_info->setAlignment(Qt::AlignCenter);
        m_info->setWordWrap(true);
        m_info->setTextFormat(Qt::PlainText);
        layout->addWidget(m_info, 1);

        // A scrub bar, not a focus target: Quick Look's keys go to the panel,
        // so the slider takes the mouse and nothing else.
        m_scrub = new QSlider(Qt::Horizontal, m_root);
        m_scrub->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(m_scrub);

        m_player = new QMediaPlayer(m_root);
        m_audio = new QAudioOutput(m_root);
        m_player->setAudioOutput(m_audio);
        m_player->setVideoOutput(m_video);

        QObject::connect(m_player, &QMediaPlayer::durationChanged, &m_context,
                         [this](qint64 duration) {
                             m_scrub->setRange(0, static_cast<int>(duration));
                             updateInfo();
                             notifyStatusChanged();
                         });
        QObject::connect(m_player, &QMediaPlayer::positionChanged, &m_context,
                         [this](qint64 position) {
                             if (!m_scrub->isSliderDown()) {
                                 m_scrub->setValue(static_cast<int>(position));
                             }
                             // The footer shows whole seconds; refreshing it
                             // for every position tick would be wasted layout.
                             if (position / 1000 != m_shownSecond) {
                                 m_shownSecond = position / 1000;
                                 notifyStatusChanged();
                             }
                         });
        QObject::connect(m_scrub, &QSlider::sliderMoved, &m_context,
                         [this](int position) { m_player->setPosition(position); });
        QObject::connect(m_player, &QMediaPlayer::playbackStateChanged, &m_context,
                         [this] { notifyStatusChanged(); });
        // A poster frame. A stopped player shows nothing at all, so a video
        // previewed without playing it would be an empty rectangle; pausing a
        // freshly loaded one decodes its first frame and nothing more — no
        // sound, and the position stays at zero.
        QObject::connect(m_player, &QMediaPlayer::mediaStatusChanged, &m_context,
                         [this](QMediaPlayer::MediaStatus status) {
                             if (status == QMediaPlayer::LoadedMedia &&
                                 m_player->playbackState() == QMediaPlayer::StoppedState) {
                                 m_player->pause();
                             }
                         });
        QObject::connect(m_player, &QMediaPlayer::metaDataChanged, &m_context, [this] {
            updateInfo();
            notifyStatusChanged();
        });
        QObject::connect(m_player, &QMediaPlayer::hasVideoChanged, &m_context,
                         [this](bool hasVideo) {
                             m_video->setVisible(hasVideo);
                             m_info->setVisible(!hasVideo);
                         });
        QObject::connect(m_player, &QMediaPlayer::errorOccurred, &m_context,
                         [this](QMediaPlayer::Error, const QString &message) {
                             m_error = message;
                             updateInfo();
                             m_video->hide();
                             m_info->show();
                             notifyStatusChanged();
                         });

        return m_root;
    }

    void setContent(QuickLookContent &&content) override
    {
        if (m_player == nullptr) {
            return;
        }

        m_content = std::move(content);
        m_error.clear();
        m_shownSecond = -1;
        m_scrub->setRange(0, 0);

        // Shown as information until the player says there is a picture: an
        // audio file never gets one, and a video's first frame takes a moment.
        m_video->hide();
        m_info->show();
        updateInfo();

        // Not played automatically. Quick Look follows the cursor, and a file
        // manager that started making noise as the user moved down a music
        // directory would be unusable. `p` plays.
        m_player->setSource(QUrl::fromLocalFile(m_content.path));
    }

    void clear() override
    {
        if (m_player != nullptr) {
            m_player->stop();
            m_player->setSource(QUrl());
        }
        m_content = {};
        m_error.clear();
    }

    QString statusText() const override
    {
        if (m_player == nullptr || m_content.path.isEmpty()) {
            return {};
        }
        if (!m_error.isEmpty()) {
            return m_error;
        }

        QStringList parts;
        const bool playing = m_player->playbackState() == QMediaPlayer::PlayingState;
        parts << QStringLiteral("%1 %2 / %3")
                     .arg(playing ? QObject::tr("Playing") : QObject::tr("Paused"),
                          formatTime(m_player->position()), formatTime(m_player->duration()));

        const QMediaMetaData meta = m_player->metaData();
        const QSize resolution = meta.value(QMediaMetaData::Resolution).toSize();
        if (resolution.isValid()) {
            parts << QStringLiteral("%1×%2").arg(resolution.width()).arg(resolution.height());
        }
        const QString codec = codecName(meta);
        if (!codec.isEmpty()) {
            parts << codec;
        }
        parts << (playing ? QObject::tr("p pause") : QObject::tr("p play"));
        return parts.join(QStringLiteral(" · "));
    }

    bool handleKey(QKeyEvent *event) override
    {
        if (m_player == nullptr || event->key() != Qt::Key_P ||
            event->modifiers() != Qt::NoModifier) {
            return false;
        }
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            m_player->pause();
        } else {
            m_player->play();
        }
        return true;
    }

private:
    static QString codecName(const QMediaMetaData &meta)
    {
        const QVariant video = meta.value(QMediaMetaData::VideoCodec);
        if (video.isValid()) {
            const auto codec = video.value<QMediaFormat::VideoCodec>();
            if (codec != QMediaFormat::VideoCodec::Unspecified) {
                return QMediaFormat::videoCodecName(codec);
            }
        }
        const QVariant audio = meta.value(QMediaMetaData::AudioCodec);
        if (audio.isValid()) {
            const auto codec = audio.value<QMediaFormat::AudioCodec>();
            if (codec != QMediaFormat::AudioCodec::Unspecified) {
                return QMediaFormat::audioCodecName(codec);
            }
        }
        return {};
    }

    /// The text shown in place of a picture: what the file is, and the tags
    /// an audio file carries.
    void updateInfo()
    {
        QStringList lines;
        lines << QFileInfo(m_content.path).fileName();
        lines << QString();

        const QMediaMetaData meta = m_player->metaData();
        for (const auto key : {QMediaMetaData::Title, QMediaMetaData::ContributingArtist,
                               QMediaMetaData::AlbumArtist, QMediaMetaData::AlbumTitle,
                               QMediaMetaData::Genre, QMediaMetaData::Date}) {
            const QString value = meta.stringValue(key);
            if (!value.isEmpty()) {
                lines << QStringLiteral("%1: %2").arg(QMediaMetaData::metaDataKeyToString(key),
                                                      value);
            }
        }
        if (m_player->duration() > 0) {
            lines << QStringLiteral("%1: %2").arg(QObject::tr("Duration"),
                                                  formatTime(m_player->duration()));
        }
        const QString codec = codecName(meta);
        if (!codec.isEmpty()) {
            lines << QStringLiteral("%1: %2").arg(QObject::tr("Codec"), codec);
        }
        if (!m_error.isEmpty()) {
            lines << QString() << m_error;
        }

        m_info->setText(lines.join(QLatin1Char('\n')));
    }

    /// The connection context for every lambda above that captures `this`, so
    /// that destroying the renderer disconnects them all.
    QObject m_context;

    QWidget *m_root = nullptr;
    QVideoWidget *m_video = nullptr;
    QLabel *m_info = nullptr;
    QSlider *m_scrub = nullptr;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;

    QuickLookContent m_content;
    QString m_error;
    qint64 m_shownSecond = -1;
};

} // namespace

class MultimediaPlugin : public QObject, public RendererPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PF_RENDERER_PLUGIN_IID)
    Q_INTERFACES(pf::plugins::RendererPlugin)

public:
    std::unique_ptr<ui::QuickLookRenderer> createRenderer() override
    {
        return std::make_unique<MediaPlayerRenderer>();
    }
};

} // namespace pf::plugins

#include "MediaPlugin.moc"
