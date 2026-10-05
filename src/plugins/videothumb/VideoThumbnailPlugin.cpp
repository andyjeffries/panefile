// pf-video-thumb: libffmpegthumbnailer for §7.7's video thumbnails.

#include "plugins/PluginInterfaces.h"

#include <QFile>
#include <QObject>

#include <libffmpegthumbnailer/videothumbnailerc.h>

#include <memory>

namespace pf::plugins {

class FfmpegThumbnailPlugin : public QObject, public VideoThumbnailPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PF_VIDEO_THUMBNAIL_PLUGIN_IID)
    Q_INTERFACES(pf::plugins::VideoThumbnailPlugin)

public:
    QImage thumbnail(const QString &path, int sizePx, QString *error) const override
    {
        // One thumbnailer per call. It holds per-file decoder state, and the
        // thumbnail pool calls this from several threads at once; creating one
        // is cheap beside decoding a frame.
        const std::unique_ptr<video_thumbnailer, decltype(&video_thumbnailer_destroy)> thumbnailer(
            video_thumbnailer_create(), &video_thumbnailer_destroy);
        const std::unique_ptr<image_data, decltype(&video_thumbnailer_destroy_image_data)> data(
            video_thumbnailer_create_image_data(), &video_thumbnailer_destroy_image_data);
        if (!thumbnailer || !data) {
            if (error != nullptr) {
                *error = QStringLiteral("could not create a thumbnailer");
            }
            return {};
        }

        video_thumbnailer_set_size(thumbnailer.get(), sizePx, 0);
        thumbnailer->thumbnail_image_type = Png;
        thumbnailer->maintain_aspect_ratio = 1;
        // Past the opening titles or fade-in, which is what the default 10%
        // is for, and what other freedesktop thumbnailers do too.
        thumbnailer->seek_percentage = 10;

        const QByteArray file = QFile::encodeName(path);
        if (video_thumbnailer_generate_thumbnail_to_buffer(thumbnailer.get(), file.constData(),
                                                           data.get()) != 0) {
            if (error != nullptr) {
                *error = QStringLiteral("could not decode a frame");
            }
            return {};
        }

        QImage image;
        image.loadFromData(data->image_data_ptr, data->image_data_size, "PNG");
        if (image.isNull() && error != nullptr) {
            *error = QStringLiteral("the thumbnailer produced no image");
        }
        return image;
    }
};

} // namespace pf::plugins

#include "VideoThumbnailPlugin.moc"
