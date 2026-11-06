// Captures the system's output for the visuals via miniaudio: WASAPI loopback on Windows, a
// PulseAudio/PipeWire monitor on Linux, the default input (microphone) on macOS.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fh {

struct CaptureDevice {
  std::string name;
  bool monitor = false;
  bool is_default = false;
};

class AudioCapture {
 public:
  AudioCapture();
  ~AudioCapture();
  AudioCapture(const AudioCapture&) = delete;
  AudioCapture& operator=(const AudioCapture&) = delete;
  AudioCapture(AudioCapture&&) = delete;
  AudioCapture& operator=(AudioCapture&&) = delete;

  // The capture devices the system offers (empty, with `error` filled in, if no audio system could
  // be reached).
  static std::vector<CaptureDevice> list_devices(std::string& error);

  // Starts listening. `hint` empty: the sound being played (an output's monitor); otherwise the
  // first device whose name contains `hint` (case does not matter). Returns false with a reason a
  // person can read.
  bool start(const std::string& hint, std::string& error);
  void stop();

  [[nodiscard]] bool running() const;
  [[nodiscard]] const std::string& device_name() const;
  [[nodiscard]] double sample_rate() const;  // of the device, once started

  // Gives the analyser everything heard since the last call and reads the spectrum (1024 bytes) and
  // waveform (2048 bytes).
  void read(std::vector<std::uint8_t>& spectrum, std::vector<std::uint8_t>& waveform);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fh
