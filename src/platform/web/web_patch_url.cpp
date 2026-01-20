#include "platform/web/web_patch_url.hpp"

#include "formats/common.hpp"
#include "platform/platform_config.hpp"
#include "ym2612/types.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(MEGATOY_PLATFORM_WEB)
#include <emscripten.h>
#include <emscripten/val.h>
#endif

namespace platform::web::patch_url {
namespace {

constexpr size_t kOperatorValueCount = 13;

bool is_unreserved(unsigned char c) {
  return std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~';
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  return -1;
}

std::string decode_component(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c == '+') {
      result.push_back(' ');
      continue;
    }
    if (c == '%' && i + 2 < text.size()) {
      int hi = hex_value(text[i + 1]);
      int lo = hex_value(text[i + 2]);
      if (hi >= 0 && lo >= 0) {
        result.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    result.push_back(c);
  }
  return result;
}

std::string encode_component(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  constexpr char kHex[] = "0123456789ABCDEF";
  for (unsigned char c : text) {
    if (is_unreserved(c)) {
      result.push_back(static_cast<char>(c));
    } else {
      result.push_back('%');
      result.push_back(kHex[c >> 4]);
      result.push_back(kHex[c & 0x0F]);
    }
  }
  return result;
}

std::optional<int> parse_int(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  int value = 0;
  auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc() ||
      result.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::vector<std::string_view> split(std::string_view text, char delimiter) {
  std::vector<std::string_view> parts;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find(delimiter, start);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    parts.emplace_back(text.substr(start, end - start));
    if (end == text.size()) {
      break;
    }
    start = end + 1;
  }
  return parts;
}

bool parse_int_list(std::string_view text, size_t expected,
                    std::vector<int> &out) {
  out.clear();
  auto parts = split(text, ',');
  if (parts.size() < expected) {
    return false;
  }
  out.reserve(parts.size());
  for (auto part : parts) {
    auto value = parse_int(part);
    if (!value.has_value()) {
      return false;
    }
    out.push_back(*value);
  }
  return true;
}

int clamp_int(int value, int min, int max) {
  return std::clamp(value, min, max);
}

bool parse_global(std::string_view value, ym2612::GlobalSettings &global) {
  std::vector<int> values;
  if (!parse_int_list(value, 3, values)) {
    return false;
  }
  global.dac_enable = values[0] != 0;
  global.lfo_enable = values[1] != 0;
  global.lfo_frequency =
      static_cast<uint8_t>(clamp_int(values[2], 0, 7));
  return true;
}

bool parse_channel(std::string_view value, ym2612::ChannelSettings &channel) {
  std::vector<int> values;
  if (!parse_int_list(value, 4, values)) {
    return false;
  }
  channel.left_speaker = values[0] != 0;
  channel.right_speaker = values[1] != 0;
  channel.amplitude_modulation_sensitivity =
      static_cast<uint8_t>(clamp_int(values[2], 0, 3));
  channel.frequency_modulation_sensitivity =
      static_cast<uint8_t>(clamp_int(values[3], 0, 7));
  return true;
}

bool parse_instrument(std::string_view value, ym2612::ChannelInstrument &inst) {
  std::vector<int> values;
  if (!parse_int_list(value, 2, values)) {
    return false;
  }
  inst.feedback = static_cast<uint8_t>(clamp_int(values[0], 0, 7));
  inst.algorithm = static_cast<uint8_t>(clamp_int(values[1], 0, 7));
  return true;
}

bool parse_operator(std::string_view value, ym2612::OperatorSettings &op) {
  std::vector<int> values;
  if (!parse_int_list(value, kOperatorValueCount, values)) {
    return false;
  }
  op.attack_rate = static_cast<uint8_t>(clamp_int(values[0], 0, 31));
  op.decay_rate = static_cast<uint8_t>(clamp_int(values[1], 0, 31));
  op.sustain_rate = static_cast<uint8_t>(clamp_int(values[2], 0, 31));
  op.release_rate = static_cast<uint8_t>(clamp_int(values[3], 0, 15));
  op.sustain_level = static_cast<uint8_t>(clamp_int(values[4], 0, 15));
  op.total_level = static_cast<uint8_t>(clamp_int(values[5], 0, 127));
  op.key_scale = static_cast<uint8_t>(clamp_int(values[6], 0, 3));
  op.multiple = static_cast<uint8_t>(clamp_int(values[7], 0, 15));
  op.detune = formats::detune_from_dmp_to_patch(clamp_int(values[8], 0, 7));
  op.ssg_type_envelope_control =
      static_cast<uint8_t>(clamp_int(values[9], 0, 7));
  op.ssg_enable = values[10] != 0;
  op.amplitude_modulation_enable = values[11] != 0;
  op.enable = values[12] != 0;
  return true;
}

void append_int(std::string &out, int value) {
  out += std::to_string(value);
}

void append_list(std::string &out, std::initializer_list<int> values) {
  bool first = true;
  for (int value : values) {
    if (!first) {
      out.push_back(',');
    }
    first = false;
    append_int(out, value);
  }
}

const ym2612::OperatorSettings &operator_for_ui_index(
    const ym2612::Patch &patch, size_t ui_index) {
  // UI operator order uses all_operator_indices (Op1, Op2, Op3, Op4).
  auto op_index =
      static_cast<size_t>(ym2612::all_operator_indices[ui_index]);
  return patch.instrument.operators[op_index];
}

ym2612::OperatorSettings &operator_for_ui_index(ym2612::Patch &patch,
                                                size_t ui_index) {
  // UI operator order uses all_operator_indices (Op1, Op2, Op3, Op4).
  auto op_index =
      static_cast<size_t>(ym2612::all_operator_indices[ui_index]);
  return patch.instrument.operators[op_index];
}

std::string strip_query_prefix(std::string_view query) {
  if (!query.empty() && query.front() == '?') {
    query.remove_prefix(1);
  }
  return std::string(query);
}

#if defined(MEGATOY_PLATFORM_WEB)
std::string &cached_query() {
  static std::string value;
  return value;
}

bool &cached_query_ready() {
  static bool ready = false;
  return ready;
}

void ensure_cached_query() {
  if (cached_query_ready()) {
    return;
  }
  using emscripten::val;
  auto search = val::global("window")["location"]["search"];
  if (!search.isNull() && !search.isUndefined()) {
    cached_query() = search.as<std::string>();
  }
  cached_query_ready() = true;
}
#endif

} // namespace

std::string build_query(const ym2612::Patch &patch) {
  std::string query = "?v=" + std::to_string(kQueryVersion);
  query += "&n=";
  query += encode_component(patch.name);

  query += "&g=";
  append_list(query,
              {patch.global.dac_enable ? 1 : 0,
               patch.global.lfo_enable ? 1 : 0,
               patch.global.lfo_frequency});

  query += "&c=";
  append_list(query,
              {patch.channel.left_speaker ? 1 : 0,
               patch.channel.right_speaker ? 1 : 0,
               patch.channel.amplitude_modulation_sensitivity,
               patch.channel.frequency_modulation_sensitivity});

  query += "&i=";
  append_list(query, {patch.instrument.feedback, patch.instrument.algorithm});

  for (size_t ui_index = 0; ui_index < 4; ++ui_index) {
    const auto &op = operator_for_ui_index(patch, ui_index);
    query += "&o";
    query += std::to_string(ui_index + 1);
    query += "=";
    append_list(query,
                {op.attack_rate, op.decay_rate, op.sustain_rate,
                 op.release_rate, op.sustain_level, op.total_level,
                 op.key_scale, op.multiple,
                 formats::detune_from_patch_to_dmp(op.detune & 0x07),
                 op.ssg_type_envelope_control, op.ssg_enable ? 1 : 0,
                 op.amplitude_modulation_enable ? 1 : 0, op.enable ? 1 : 0});
  }

  return query;
}

std::optional<ym2612::Patch>
parse_query(std::string_view query, const ym2612::Patch &defaults,
            std::string *error) {
  std::string normalized = strip_query_prefix(query);
  if (normalized.empty()) {
    return std::nullopt;
  }

  bool has_version = false;
  int version = 0;
  bool has_global = false;
  bool has_channel = false;
  bool has_instrument = false;
  std::array<bool, 4> has_operator = {false, false, false, false};

  ym2612::Patch patch = defaults;

  auto pairs = split(normalized, '&');
  for (auto pair : pairs) {
    if (pair.empty()) {
      continue;
    }
    size_t eq = pair.find('=');
    if (eq == std::string_view::npos) {
      continue;
    }
    auto raw_key = pair.substr(0, eq);
    auto raw_value = pair.substr(eq + 1);
    auto key = decode_component(raw_key);
    auto value = decode_component(raw_value);

    if (key == "v") {
      auto parsed = parse_int(value);
      if (!parsed.has_value()) {
        continue;
      }
      has_version = true;
      version = *parsed;
      continue;
    }

    if (key == "n") {
      patch.name = value;
      continue;
    }

    if (key == "g") {
      if (parse_global(value, patch.global)) {
        has_global = true;
      }
      continue;
    }

    if (key == "c") {
      if (parse_channel(value, patch.channel)) {
        has_channel = true;
      }
      continue;
    }

    if (key == "i") {
      if (parse_instrument(value, patch.instrument)) {
        has_instrument = true;
      }
      continue;
    }

    if (key.size() == 2 && key[0] == 'o') {
      int index = key[1] - '1';
      if (index >= 0 && index < 4) {
        auto &op = operator_for_ui_index(patch, static_cast<size_t>(index));
        if (parse_operator(value, op)) {
          has_operator[static_cast<size_t>(index)] = true;
        }
      }
      continue;
    }
  }

  if (!has_version || version != kQueryVersion) {
    if (error) {
      *error = "Unsupported or missing patch URL version.";
    }
    return std::nullopt;
  }

  if (!has_global || !has_channel || !has_instrument ||
      std::any_of(has_operator.begin(), has_operator.end(),
                  [](bool value) { return !value; })) {
    if (error) {
      *error = "Incomplete patch URL data.";
    }
    return std::nullopt;
  }

  return patch;
}

std::optional<ym2612::Patch>
load_patch_from_current_url(const ym2612::Patch &defaults,
                            std::string *error) {
#if defined(MEGATOY_PLATFORM_WEB)
  ensure_cached_query();
  if (cached_query().empty()) {
    return std::nullopt;
  }
  return parse_query(cached_query(), defaults, error);
#else
  (void)defaults;
  (void)error;
  return std::nullopt;
#endif
}

void sync_patch_to_url_if_needed(const ym2612::Patch &patch) {
#if defined(MEGATOY_PLATFORM_WEB)
  ensure_cached_query();
  std::string query = build_query(patch);
  if (query == cached_query()) {
    return;
  }
  std::string query_copy = query;
  EM_ASM(
      {
        const query = UTF8ToString($0);
        if (typeof window === 'undefined') return;
        const path = window.location.pathname || '';
        const hash = window.location.hash || '';
        const next = query.length ? (path + query + hash) : (path + hash);
        if (window.history && window.history.replaceState) {
          window.history.replaceState(null, '', next);
        }
      },
      query_copy.c_str());
  cached_query() = std::move(query);
#else
  (void)patch;
#endif
}

} // namespace platform::web::patch_url
