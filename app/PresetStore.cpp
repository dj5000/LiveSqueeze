#include "PresetStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace lsqapp {

PresetStore::PresetStore(const QString& path) : path_(path) {
    if (path_.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        path_ = QDir(dir).filePath(QStringLiteral("presets.json"));
    }
}

std::vector<UserPreset> PresetStore::load() const {
    std::vector<UserPreset> out;
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return out;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt(0) != kSchemaVersion) {
        return out; // an unknown format: leave it alone rather than guess
    }
    for (const QJsonValue& v : root.value(QStringLiteral("presets")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString name = o.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) {
            continue;
        }
        UserPreset p;
        p.name = name;
        p.params = lsq::Params{};
        const QJsonObject values = o.value(QStringLiteral("params")).toObject();
        for (std::size_t i = 0; i < lsq::paramCount(); ++i) {
            const lsq::ParamDesc& d = lsq::paramDesc(i);
            const QJsonValue jv = values.value(QString::fromLatin1(d.key));
            if (jv.isDouble()) {
                lsq::paramSet(p.params, d, static_cast<float>(jv.toDouble()));
            } else if (jv.isBool()) {
                lsq::paramSet(p.params, d, jv.toBool() ? 1.0f : 0.0f);
            }
        }
        lsq::sanitize(p.params);
        out.push_back(std::move(p));
    }
    return out;
}

bool PresetStore::write(const std::vector<UserPreset>& all) const {
    QJsonArray arr;
    for (const UserPreset& p : all) {
        QJsonObject values;
        for (std::size_t i = 0; i < lsq::paramCount(); ++i) {
            const lsq::ParamDesc& d = lsq::paramDesc(i);
            const double v = static_cast<double>(lsq::paramGet(p.params, d));
            if (d.kind == lsq::ParamKind::Bool) {
                values.insert(QString::fromLatin1(d.key), v >= 0.5);
            } else {
                values.insert(QString::fromLatin1(d.key), v);
            }
        }
        QJsonObject o;
        o.insert(QStringLiteral("name"), p.name);
        o.insert(QStringLiteral("params"), values);
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), kSchemaVersion);
    root.insert(QStringLiteral("presets"), arr);

    QDir().mkpath(QFileInfo(path_).absolutePath());
    QSaveFile file(path_); // writes to a temporary file and renames it on commit
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

bool PresetStore::save(const UserPreset& preset) {
    std::vector<UserPreset> all = load();
    bool replaced = false;
    for (UserPreset& p : all) {
        if (p.name.compare(preset.name, Qt::CaseInsensitive) == 0) {
            p = preset;
            replaced = true;
        }
    }
    if (!replaced) {
        all.push_back(preset);
    }
    return write(all);
}

bool PresetStore::remove(const QString& name) {
    std::vector<UserPreset> all = load();
    const auto before = all.size();
    all.erase(std::remove_if(all.begin(), all.end(),
                             [&](const UserPreset& p) {
                                 return p.name.compare(name, Qt::CaseInsensitive) == 0;
                             }),
              all.end());
    return all.size() != before && write(all);
}

} // namespace lsqapp
