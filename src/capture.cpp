#include "fh/capture.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>

#include "fh/analyser.hpp"

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace fh {

namespace {

std::string lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}
bool is_monitor_name(const std::string& name) {
  return lower(name).find("monitor") != std::string::npos;
}

}  // namespace

struct AudioCapture::Impl {
  ma_context context{};
  ma_device device{};
  bool have_context = false, have_device = false;
  std::string name;
  double rate = 0;

  std::mutex lock;
  std::vector<float> pending;
  Analyser analyser;

  static void on_audio(ma_device* dev, void*, const void* input, ma_uint32 frames) {
    auto* self = static_cast<Impl*>(dev->pUserData);
    if (!input || frames == 0) {
      return;
    }
    const auto* in = static_cast<const float*>(input);
    const std::scoped_lock guard(self->lock);
    self->pending.insert(self->pending.end(), in, in + frames);
    constexpr std::size_t keep = 16384;  // a stalled reader must not make this grow without end
    if (self->pending.size() > keep) {
      self->pending.erase(self->pending.begin(),
                          self->pending.end() - static_cast<std::ptrdiff_t>(keep));
    }
  }

  void close_all() {
    if (have_device) {
      ma_device_uninit(&device);
      have_device = false;
    }
    if (have_context) {
      ma_context_uninit(&context);
      have_context = false;
    }
  }
};

AudioCapture::AudioCapture() : impl_(std::make_unique<Impl>()) {}
AudioCapture::~AudioCapture() { stop(); }

std::vector<CaptureDevice> AudioCapture::list_devices(std::string& error) {
  std::vector<CaptureDevice> out;
  ma_context ctx;
  if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) {
    error = "No audio system could be reached.";
    return out;
  }
  ma_device_info* playback = nullptr;
  ma_device_info* capture = nullptr;
  ma_uint32 playback_count = 0;
  ma_uint32 capture_count = 0;
  if (ma_context_get_devices(&ctx, &playback, &playback_count, &capture, &capture_count) ==
      MA_SUCCESS) {
    for (ma_uint32 i = 0; i < capture_count; i++) {
      out.push_back({capture[i].name, is_monitor_name(capture[i].name), capture[i].isDefault != 0});
    }
  } else {
    error = "The audio devices could not be listed.";
  }
  ma_context_uninit(&ctx);
  return out;
}

bool AudioCapture::start(const std::string& hint, std::string& error) {
  stop();
  Impl& m = *impl_;
  if (ma_context_init(nullptr, 0, nullptr, &m.context) != MA_SUCCESS) {
    error = "No audio system could be reached.";
    return false;
  }
  m.have_context = true;

  ma_device_config cfg;
#ifdef _WIN32
  // Windows can listen to the output directly: no device to pick unless a name was asked for.
  const bool loopback = hint.empty();
  cfg = ma_device_config_init(loopback ? ma_device_type_loopback : ma_device_type_capture);
#else
  const bool loopback = false;
  cfg = ma_device_config_init(ma_device_type_capture);
#endif
  ma_device_id chosen{};
  if (!loopback) {
    ma_device_info* playback = nullptr;
    ma_device_info* capture = nullptr;
    ma_uint32 playback_count = 0;
    ma_uint32 capture_count = 0;
    if (ma_context_get_devices(&m.context, &playback, &playback_count, &capture, &capture_count) !=
            MA_SUCCESS ||
        capture_count == 0) {
      error = "The system has no devices to listen to.";
      m.close_all();
      return false;
    }
    const ma_device_info* pick = nullptr;
    const std::string want = lower(hint);
    for (ma_uint32 i = 0; i < capture_count && !pick; i++) {
      const std::string name = lower(capture[i].name);
      if (!want.empty() ? name.find(want) != std::string::npos : is_monitor_name(name)) {
        pick = &capture[i];
      }
    }
    if (!pick && want.empty()) {  // no output monitor on offer (macOS, or a bare ALSA setup): the
                                  // default input
      for (ma_uint32 i = 0; i < capture_count && !pick; i++) {
        if (capture[i].isDefault) {
          pick = &capture[i];
        }
      }
      if (!pick) {
        pick = &capture[0];
      }
    }
    if (!pick) {
      error = "No device matches \"" + hint + "\".";
      m.close_all();
      return false;
    }
    chosen = pick->id;
    m.name = pick->name;
    cfg.capture.pDeviceID = &chosen;
  } else {
    m.name = "the sound being played";
  }
  cfg.capture.format = ma_format_f32;
  cfg.capture.channels = 1;  // miniaudio mixes whatever the device has down to one channel
  cfg.sampleRate = 0;  // the device's own rate
  cfg.dataCallback = &Impl::on_audio;
  cfg.pUserData = &m;
  if (ma_device_init(&m.context, &cfg, &m.device) != MA_SUCCESS) {
    error = "The device \"" + m.name + "\" could not be opened.";
    m.close_all();
    return false;
  }
  m.have_device = true;
  m.rate = static_cast<double>(m.device.sampleRate);
  if (ma_device_start(&m.device) != MA_SUCCESS) {
    error = "The device \"" + m.name + "\" could not be started.";
    m.close_all();
    return false;
  }
  m.analyser.reset();
  m.pending.clear();
  return true;
}

void AudioCapture::stop() {
  Impl& m = *impl_;
  m.close_all();
  const std::scoped_lock guard(m.lock);
  m.pending.clear();
}

bool AudioCapture::running() const { return impl_->have_device; }
const std::string& AudioCapture::device_name() const { return impl_->name; }
double AudioCapture::sample_rate() const { return impl_->rate; }

void AudioCapture::read(std::vector<std::uint8_t>& spectrum, std::vector<std::uint8_t>& waveform) {
  Impl& m = *impl_;
  std::vector<float> got;
  {
    const std::scoped_lock guard(m.lock);
    got.swap(m.pending);
  }
  m.analyser.push(got);
  m.analyser.read(spectrum, waveform);
}

}  // namespace fh
