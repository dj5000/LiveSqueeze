#include <algorithm>
#include <cctype>
#include <string>

#include "lsq/audio_backend.hpp"

namespace lsq {

bool nameLooksVirtual(const std::string& name) {
    std::string s(name);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* needle : {"cable", "blackhole", "virtual", "monitor of", "monitor", "loopback",
                               "livesqueeze", "vb-audio", "voicemeeter", "soundflower", "null"}) {
        if (s.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace lsq
