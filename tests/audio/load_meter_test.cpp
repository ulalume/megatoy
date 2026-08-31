#include "audio/load_meter.hpp"

#include "../test_check.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

bool near(float value, float expected) {
  return std::fabs(value - expected) < 1e-5f;
}

void test_load_ratio() {
  // 512 frames at 48 kHz play for 10.666... ms.
  constexpr std::int64_t block_ns = 10'666'666;
  CHECK(near(audio::load_ratio(block_ns, 512, 48000), 1.0f));
  CHECK(near(audio::load_ratio(block_ns / 2, 512, 48000), 0.5f));
  CHECK(near(audio::load_ratio(block_ns / 4, 512, 48000), 0.25f));

  // The ratio is the elapsed time over the deadline, so twice the frames at
  // the same cost is half the load.
  CHECK(near(audio::load_ratio(block_ns, 1024, 48000), 0.5f));
  // ...and the same block at half the rate has twice as long to render.
  CHECK(near(audio::load_ratio(block_ns, 512, 24000), 0.5f));

  // Overrun is reported as it is, not clamped: the graph clamps for drawing,
  // but the number stays honest.
  CHECK(near(audio::load_ratio(block_ns * 3, 512, 48000), 3.0f));
}

void test_load_ratio_edges() {
  CHECK(audio::load_ratio(0, 512, 48000) == 0.0f);
  CHECK(audio::load_ratio(-1, 512, 48000) == 0.0f);
  CHECK(audio::load_ratio(1000, 0, 48000) == 0.0f);
  CHECK(audio::load_ratio(1000, 512, 0) == 0.0f);
}

void test_history_order() {
  audio::LoadMeter meter;
  CHECK(meter.history().count == 0);

  // A tenth of the deadline, repeated: one full block at 48 kHz is
  // 512 / 48000 s, so 1/10th of that in nanoseconds.
  constexpr std::int64_t tenth_ns = 512LL * 1'000'000'000LL / 48000LL / 10LL;
  meter.record(tenth_ns, 512, 48000);
  meter.record(tenth_ns * 2, 512, 48000);

  audio::LoadMeter::History history = meter.history();
  CHECK(history.count == 2);
  CHECK(near(history.values[0], 0.1f));
  CHECK(near(history.values[1], 0.2f));

  // Older values fall off the front once the ring is full.
  for (std::size_t i = 0; i < audio::LoadMeter::kCapacity; ++i) {
    meter.record(tenth_ns * 5, 512, 48000);
  }
  history = meter.history();
  CHECK(history.count == audio::LoadMeter::kCapacity);
  for (std::size_t i = 0; i < history.count; ++i) {
    CHECK(near(history.values[i], 0.5f));
  }

  meter.record(tenth_ns * 9, 512, 48000);
  history = meter.history();
  CHECK(history.count == audio::LoadMeter::kCapacity);
  CHECK(near(history.values[history.count - 1], 0.9f));
  CHECK(near(history.values[0], 0.5f));

  meter.clear();
  CHECK(meter.history().count == 0);
}

} // namespace

int main() {
  test_load_ratio();
  test_load_ratio_edges();
  test_history_order();
  std::cout << "load_meter_test passed" << std::endl;
  return 0;
}
