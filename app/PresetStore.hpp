#pragma once

#include <QString>
#include <QStringList>
#include <vector>

#include "lsq/params.hpp"

namespace lsqapp {

struct UserPreset {
    QString name;
    lsq::Params params;
};

// The user's own presets, stored as one versioned JSON file. Reading tolerates a missing, damaged
// or newer-format file (it yields no presets rather than an error), and writing is atomic.
class PresetStore {
public:
    static constexpr int kSchemaVersion = 1;

    // `path` empty: the platform's standard per-user location.
    explicit PresetStore(const QString& path = {});

    std::vector<UserPreset> load() const;
    // Adds or replaces the preset with this name. Returns false if the file cannot be written.
    bool save(const UserPreset& preset);
    bool remove(const QString& name);

    QString fileName() const { return path_; }

private:
    bool write(const std::vector<UserPreset>& all) const;
    QString path_;
};

} // namespace lsqapp
