#pragma once

#include "audio/audio_transport.hpp"

#include <memory>

/// The desktop's audio output: Core Audio on macOS, SDL elsewhere.
std::unique_ptr<AudioTransport> make_native_audio_transport();
