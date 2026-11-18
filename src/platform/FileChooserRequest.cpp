#include "platform/FileChooserRequest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QMimeType>

namespace pf::platform {
namespace {

const char *modeName(FileChooserRequest::Mode mode)
{
    switch (mode) {
    case FileChooserRequest::Mode::Open:
        return "open";
    case FileChooserRequest::Mode::Save:
        return "save";
    case FileChooserRequest::Mode::SaveFiles:
        return "save_files";
    }
    return "open";
}

QJsonArray toArray(const QStringList &list)
{
    QJsonArray array;
    for (const QString &item : list) {
        array.append(item);
    }
    return array;
}

QStringList toStringList(const QJsonValue &value)
{
    QStringList list;
    for (const QJsonValue &item : value.toArray()) {
        list << item.toString();
    }
    return list;
}

} // namespace

QByteArray FileChooserRequest::toJson() const
{
    QJsonArray filterArray;
    for (const FileFilter &filter : filters) {
        QJsonArray patterns;
        for (const auto &[kind, pattern] : filter.patterns) {
            patterns.append(QJsonArray{static_cast<int>(kind), pattern});
        }
        filterArray.append(QJsonObject{{"name", filter.name}, {"patterns", patterns}});
    }

    QJsonArray choiceArray;
    for (const FileChoice &choice : choices) {
        QJsonArray options;
        for (const auto &[value, label] : choice.options) {
            options.append(QJsonArray{value, label});
        }
        choiceArray.append(QJsonObject{{"id", choice.id},
                                       {"label", choice.label},
                                       {"options", options},
                                       {"initial", choice.initial}});
    }

    const QJsonObject object{{"mode", QLatin1String(modeName(mode))},
                             {"app_id", appId},
                             {"title", title},
                             {"accept_label", acceptLabel},
                             {"multiple", multiple},
                             {"directory", directory},
                             {"filters", filterArray},
                             {"current_filter", currentFilter},
                             {"choices", choiceArray},
                             {"current_name", currentName},
                             {"current_folder", currentFolder},
                             {"current_file", currentFile},
                             {"files", toArray(files)}};
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

FileChooserRequest FileChooserRequest::fromJson(const QByteArray &json)
{
    FileChooserRequest request;
    const QJsonObject object = QJsonDocument::fromJson(json).object();

    const QString modeText = object.value("mode").toString();
    request.mode = modeText == QLatin1String("save")         ? Mode::Save
                   : modeText == QLatin1String("save_files") ? Mode::SaveFiles
                                                             : Mode::Open;
    request.appId = object.value("app_id").toString();
    request.title = object.value("title").toString();
    request.acceptLabel = object.value("accept_label").toString();
    request.multiple = object.value("multiple").toBool();
    request.directory = object.value("directory").toBool();
    request.currentFilter = object.value("current_filter").toInt(-1);
    request.currentName = object.value("current_name").toString();
    request.currentFolder = object.value("current_folder").toString();
    request.currentFile = object.value("current_file").toString();
    request.files = toStringList(object.value("files"));

    for (const QJsonValue &value : object.value("filters").toArray()) {
        FileFilter filter;
        filter.name = value.toObject().value("name").toString();
        for (const QJsonValue &pattern : value.toObject().value("patterns").toArray()) {
            const QJsonArray pair = pattern.toArray();
            filter.patterns.append({static_cast<uint>(pair.at(0).toInt()), pair.at(1).toString()});
        }
        request.filters.append(filter);
    }

    for (const QJsonValue &value : object.value("choices").toArray()) {
        const QJsonObject item = value.toObject();
        FileChoice choice;
        choice.id = item.value("id").toString();
        choice.label = item.value("label").toString();
        choice.initial = item.value("initial").toString();
        for (const QJsonValue &option : item.value("options").toArray()) {
            const QJsonArray pair = option.toArray();
            choice.options.append({pair.at(0).toString(), pair.at(1).toString()});
        }
        request.choices.append(choice);
    }
    return request;
}

QByteArray FileChooserResult::toJson() const
{
    QJsonArray choiceArray;
    for (const auto &[id, value] : choices) {
        choiceArray.append(QJsonArray{id, value});
    }
    return QJsonDocument(QJsonObject{{"paths", toArray(paths)},
                                     {"filter", filterIndex},
                                     {"choices", choiceArray}})
        .toJson(QJsonDocument::Compact);
}

bool FileChooserResult::fromJson(const QByteArray &json, FileChooserResult *result)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject() || !document.object().contains("paths")) {
        return false;
    }
    const QJsonObject object = document.object();
    result->paths = toStringList(object.value("paths"));
    result->filterIndex = object.value("filter").toInt(-1);
    result->choices.clear();
    for (const QJsonValue &value : object.value("choices").toArray()) {
        const QJsonArray pair = value.toArray();
        result->choices.append({pair.at(0).toString(), pair.at(1).toString()});
    }
    return true;
}

QStringList globsForFilter(const FileFilter &filter)
{
    static constexpr uint kMimePattern = 1;

    QStringList globs;
    const QMimeDatabase database;
    for (const auto &[kind, pattern] : filter.patterns) {
        if (kind != kMimePattern) {
            globs << pattern;
        } else if (pattern == QLatin1String("*/*") ||
                   pattern == QLatin1String("application/octet-stream")) {
            globs << QStringLiteral("*");
        } else if (pattern.endsWith(QLatin1String("/*"))) {
            const QString prefix = pattern.chopped(1);
            for (const QMimeType &type : database.allMimeTypes()) {
                if (type.name().startsWith(prefix)) {
                    globs << type.globPatterns();
                }
            }
        } else {
            const QMimeType named = database.mimeTypeForName(pattern);
            if (named.isValid()) {
                globs << named.globPatterns();
                for (const QMimeType &type : database.allMimeTypes()) {
                    if (type.inherits(named.name())) {
                        globs << type.globPatterns();
                    }
                }
            }
        }
    }
    globs.removeDuplicates();
    return globs;
}

} // namespace pf::platform
