#include "../test_check.hpp"
#include "platform/web/web_patch_url.hpp"

#include <iostream>
#include <string>

namespace {

namespace patch_url = platform::web::patch_url;

ym2612::Patch sample_patch() {
  ym2612::Patch patch;
  patch.name = "URL + patch / \xE9\x9F\xB3\xE8\x89\xB2";
  patch.global = {true, true, 6};
  patch.channel = {false, true, 2, 5};
  patch.instrument.feedback = 6;
  patch.instrument.algorithm = 5;
  for (uint8_t i = 0; i < 4; ++i) {
    auto &op = patch.instrument.operators[i];
    op.attack_rate = 31 - i;
    op.decay_rate = 5 + i;
    op.sustain_rate = 17 + i;
    op.release_rate = 15 - i;
    op.sustain_level = 3 + i;
    op.total_level = 97 + i;
    op.key_scale = i;
    op.multiple = 11 + i;
    op.detune = 7 - i;
    op.ssg_type_envelope_control = 3 + i;
    op.ssg_enable = i % 2 == 0;
    op.amplitude_modulation_enable = i % 2 != 0;
    op.enable = i != 2;
  }
  return patch;
}

void test_roundtrip() {
  CHECK(patch_url::build_query(sample_patch()) ==
        "?v=1&p=QVVJMYGsgMCF0Y2hgbyApH_OoifKbq37S_wJs1_7mV1ju4030kkcm-aIs"
        "pF32g");
  for (const auto &patch : {ym2612::Patch{}, sample_patch()}) {
    auto expected = patch;
    if (expected.name.empty()) {
      expected.name = "patch";
    }
    std::string error;
    const auto parsed = patch_url::parse_query(patch_url::build_query(patch),
                                               sample_patch(), &error);
    CHECK(parsed.has_value());
    CHECK(error.empty());
    CHECK(*parsed == expected);
  }
}

void test_truncated_payloads_and_versions() {
  const auto query = patch_url::build_query(sample_patch());
  const auto payload = query.find("&p=") + 3;
  for (std::size_t end = payload; end < query.size(); ++end) {
    std::string error;
    CHECK(!patch_url::parse_query(query.substr(0, end), {}, &error));
    CHECK(!error.empty());
  }
  std::string error;
  CHECK(!patch_url::parse_query("?v=1&p=AA", {}, &error));
  CHECK(error == "Unsupported patch binary version.");
  CHECK(!patch_url::parse_query("?v=1&p=AQ", {}, &error));
  CHECK(error == "Patch binary data is incomplete.");
  CHECK(!patch_url::parse_query("?v=1&p=", {}, &error));
  CHECK(error == "Patch binary data is too short.");
}

} // namespace

int main() {
  test_roundtrip();
  test_truncated_payloads_and_versions();
  std::cout << "All patch URL tests passed\n";
}
