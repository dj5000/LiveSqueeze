#define DR_WAV_IMPLEMENTATION
#include "wav_io.hpp"

#include "dr_wav.h"

namespace lsqcli {

bool readWav(const std::string& path, Audio& out, std::string& error) {
    drwav wav;
    if (!drwav_init_file(&wav, path.c_str(), nullptr)) {
        error = "cannot open '" + path + "' as a WAV file";
        return false;
    }
    if (wav.channels < 1 || wav.channels > lsq::kMaxChannels) {
        error = "unsupported channel count " + std::to_string(wav.channels);
        drwav_uninit(&wav);
        return false;
    }

    out.sampleRate = wav.sampleRate;
    out.map = lsq::ChannelMap::fromWaveMask(wav.fmt.channelMask, static_cast<int>(wav.channels));
    out.samples.assign(static_cast<std::size_t>(wav.totalPCMFrameCount) * wav.channels, 0.0f);
    const drwav_uint64 got =
        drwav_read_pcm_frames_f32(&wav, wav.totalPCMFrameCount, out.samples.data());
    out.samples.resize(static_cast<std::size_t>(got) * wav.channels);
    drwav_uninit(&wav);
    return true;
}

bool writeWav(const std::string& path, const Audio& audio, std::string& error) {
    drwav_data_format fmt;
    fmt.container = drwav_container_riff;
    fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels = audio.map.n;
    fmt.sampleRate = audio.sampleRate;
    fmt.bitsPerSample = 32;

    drwav wav;
    if (!drwav_init_file_write(&wav, path.c_str(), &fmt, nullptr)) {
        error = "cannot create '" + path + "'";
        return false;
    }
    const drwav_uint64 frames = audio.frames();
    const drwav_uint64 wrote = drwav_write_pcm_frames(&wav, frames, audio.samples.data());
    drwav_uninit(&wav);
    if (wrote != frames) {
        error = "short write to '" + path + "'";
        return false;
    }
    return true;
}

} // namespace lsqcli
