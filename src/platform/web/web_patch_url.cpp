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
constexpr uint8_t kBinaryVersion = 1;
constexpr int kBinaryVersionBits = 4;
constexpr int kNameLengthBits = 6;

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

std::string decode_component(std::string_view text, bool plus_as_space) {
  std::string result;
  result.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c == '+') {
      result.push_back(plus_as_space ? ' ' : '+');
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

[[maybe_unused]] std::string encode_component(std::string_view text) {
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

int decode_base64url_char(unsigned char c) {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<int>(c - 'A');
  }
  if (c >= 'a' && c <= 'z') {
    return static_cast<int>(c - 'a' + 26);
  }
  if (c >= '0' && c <= '9') {
    return static_cast<int>(c - '0' + 52);
  }
  if (c == '-') {
    return 62;
  }
  if (c == '_') {
    return 63;
  }
  return -1;
}

std::string base64url_encode(const std::vector<uint8_t> &data) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);
  size_t i = 0;
  for (; i + 2 < data.size(); i += 3) {
    uint32_t value = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8) |
                     static_cast<uint32_t>(data[i + 2]);
    out.push_back(kAlphabet[(value >> 18) & 0x3F]);
    out.push_back(kAlphabet[(value >> 12) & 0x3F]);
    out.push_back(kAlphabet[(value >> 6) & 0x3F]);
    out.push_back(kAlphabet[value & 0x3F]);
  }
  size_t remaining = data.size() - i;
  if (remaining == 1) {
    uint32_t value = static_cast<uint32_t>(data[i]) << 16;
    out.push_back(kAlphabet[(value >> 18) & 0x3F]);
    out.push_back(kAlphabet[(value >> 12) & 0x3F]);
  } else if (remaining == 2) {
    uint32_t value = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8);
    out.push_back(kAlphabet[(value >> 18) & 0x3F]);
    out.push_back(kAlphabet[(value >> 12) & 0x3F]);
    out.push_back(kAlphabet[(value >> 6) & 0x3F]);
  }
  return out;
}

std::optional<std::vector<uint8_t>>
base64url_decode(std::string_view text) {
  while (!text.empty() && text.back() == '=') {
    text.remove_suffix(1);
  }
  if (text.empty()) {
    return std::vector<uint8_t>{};
  }
  size_t remainder = text.size() % 4;
  if (remainder == 1) {
    return std::nullopt;
  }
  size_t output_size = (text.size() / 4) * 3;
  if (remainder == 2) {
    output_size += 1;
  } else if (remainder == 3) {
    output_size += 2;
  }
  std::vector<uint8_t> out;
  out.reserve(output_size);

  size_t i = 0;
  for (; i + 4 <= text.size(); i += 4) {
    int a = decode_base64url_char(static_cast<unsigned char>(text[i]));
    int b = decode_base64url_char(static_cast<unsigned char>(text[i + 1]));
    int c = decode_base64url_char(static_cast<unsigned char>(text[i + 2]));
    int d = decode_base64url_char(static_cast<unsigned char>(text[i + 3]));
    if (a < 0 || b < 0 || c < 0 || d < 0) {
      return std::nullopt;
    }
    uint32_t value = (static_cast<uint32_t>(a) << 18) |
                     (static_cast<uint32_t>(b) << 12) |
                     (static_cast<uint32_t>(c) << 6) |
                     static_cast<uint32_t>(d);
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
  }

  if (remainder != 0) {
    int a = decode_base64url_char(static_cast<unsigned char>(text[i]));
    int b = decode_base64url_char(static_cast<unsigned char>(text[i + 1]));
    if (a < 0 || b < 0) {
      return std::nullopt;
    }
    uint32_t value = (static_cast<uint32_t>(a) << 18) |
                     (static_cast<uint32_t>(b) << 12);
    if (remainder == 2) {
      out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    } else {
      int c = decode_base64url_char(static_cast<unsigned char>(text[i + 2]));
      if (c < 0) {
        return std::nullopt;
      }
      value |= static_cast<uint32_t>(c) << 6;
      out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
      out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }
  }

  return out;
}

class BitWriter {
public:
  void write_bits(uint32_t value, int count) {
    for (int i = 0; i < count; ++i) {
      if (value & (1u << i)) {
        current_ |= static_cast<uint8_t>(1u << bit_pos_);
      }
      ++bit_pos_;
      if (bit_pos_ == 8) {
        data_.push_back(current_);
        current_ = 0;
        bit_pos_ = 0;
      }
    }
  }

  std::vector<uint8_t> finish() {
    if (bit_pos_ != 0) {
      data_.push_back(current_);
      current_ = 0;
      bit_pos_ = 0;
    }
    return data_;
  }

private:
  std::vector<uint8_t> data_;
  uint8_t current_ = 0;
  int bit_pos_ = 0;
};

class BitReader {
public:
  explicit BitReader(const std::vector<uint8_t> &data) : data_(data) {}

  bool read_bits(int count, uint32_t &out) {
    out = 0;
    for (int i = 0; i < count; ++i) {
      if (byte_index_ >= data_.size()) {
        return false;
      }
      uint8_t bit = (data_[byte_index_] >> bit_pos_) & 0x01;
      out |= static_cast<uint32_t>(bit) << i;
      ++bit_pos_;
      if (bit_pos_ == 8) {
        bit_pos_ = 0;
        ++byte_index_;
      }
    }
    return true;
  }

private:
  const std::vector<uint8_t> &data_;
  size_t byte_index_ = 0;
  int bit_pos_ = 0;
};

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

[[maybe_unused]] void append_int(std::string &out, int value) {
  out += std::to_string(value);
}

[[maybe_unused]] void append_list(std::string &out,
                                  std::initializer_list<int> values) {
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

std::vector<uint8_t> pack_patch_binary(const ym2612::Patch &patch) {
  BitWriter writer;
  size_t name_limit = (1u << kNameLengthBits) - 1;
  size_t name_len = std::min<size_t>(patch.name.size(), name_limit);

  writer.write_bits(kBinaryVersion, kBinaryVersionBits);
  writer.write_bits(static_cast<uint32_t>(name_len), kNameLengthBits);
  for (size_t i = 0; i < name_len; ++i) {
    writer.write_bits(static_cast<uint8_t>(patch.name[i]), 8);
  }

  writer.write_bits(patch.global.dac_enable ? 1 : 0, 1);
  writer.write_bits(patch.global.lfo_enable ? 1 : 0, 1);
  writer.write_bits(clamp_int(patch.global.lfo_frequency, 0, 7), 3);

  writer.write_bits(patch.channel.left_speaker ? 1 : 0, 1);
  writer.write_bits(patch.channel.right_speaker ? 1 : 0, 1);
  writer.write_bits(
      clamp_int(patch.channel.amplitude_modulation_sensitivity, 0, 3), 2);
  writer.write_bits(
      clamp_int(patch.channel.frequency_modulation_sensitivity, 0, 7), 3);

  writer.write_bits(clamp_int(patch.instrument.feedback, 0, 7), 3);
  writer.write_bits(clamp_int(patch.instrument.algorithm, 0, 7), 3);

  for (size_t ui_index = 0; ui_index < 4; ++ui_index) {
    const auto &op = operator_for_ui_index(patch, ui_index);
    writer.write_bits(clamp_int(op.attack_rate, 0, 31), 5);
    writer.write_bits(clamp_int(op.decay_rate, 0, 31), 5);
    writer.write_bits(clamp_int(op.sustain_rate, 0, 31), 5);
    writer.write_bits(clamp_int(op.release_rate, 0, 15), 4);
    writer.write_bits(clamp_int(op.sustain_level, 0, 15), 4);
    writer.write_bits(clamp_int(op.total_level, 0, 127), 7);
    writer.write_bits(clamp_int(op.key_scale, 0, 3), 2);
    writer.write_bits(clamp_int(op.multiple, 0, 15), 4);
    writer.write_bits(clamp_int(op.detune, 0, 7), 3);
    writer.write_bits(clamp_int(op.ssg_type_envelope_control, 0, 7), 3);
    writer.write_bits(op.ssg_enable ? 1 : 0, 1);
    writer.write_bits(op.amplitude_modulation_enable ? 1 : 0, 1);
    writer.write_bits(op.enable ? 1 : 0, 1);
  }

  return writer.finish();
}

std::optional<ym2612::Patch>
unpack_patch_binary(const std::vector<uint8_t> &data,
                    const ym2612::Patch &defaults, std::string *error) {
  BitReader reader(data);
  auto fail = [&](const char *message) -> std::optional<ym2612::Patch> {
    if (error) {
      *error = message;
    }
    return std::nullopt;
  };

  uint32_t version = 0;
  if (!reader.read_bits(kBinaryVersionBits, version)) {
    return fail("Patch binary data is too short.");
  }
  if (version != kBinaryVersion) {
    return fail("Unsupported patch binary version.");
  }

  uint32_t name_len = 0;
  if (!reader.read_bits(kNameLengthBits, name_len)) {
    return fail("Patch binary data is incomplete.");
  }

  ym2612::Patch patch = defaults;
  patch.name.clear();
  patch.name.reserve(name_len);
  for (uint32_t i = 0; i < name_len; ++i) {
    uint32_t ch = 0;
    if (!reader.read_bits(8, ch)) {
      return fail("Patch binary data is incomplete.");
    }
    patch.name.push_back(static_cast<char>(ch));
  }

  uint32_t value = 0;
  if (!reader.read_bits(1, value)) {
    return fail("Patch binary data is incomplete.");
  }
  patch.global.dac_enable = value != 0;
  if (!reader.read_bits(1, value)) {
    return fail("Patch binary data is incomplete.");
  }
  patch.global.lfo_enable = value != 0;
  if (!reader.read_bits(3, value)) {
    return fail("Patch binary data is incomplete.");
  }
  patch.global.lfo_frequency = static_cast<uint8_t>(value);

  if (!reader.read_bits(1, value)) {
    return fail("Patch binary data is incomplete.");
  }
  patch.channel.left_speaker = value != 0;
  if (!reader.read_bits(1, value)) {
    return fail("Patch binary data is incomplete.");
  }
  patch.channel.right_speaker = value != 0;
  if (!reader.read_bits(2, value)) {
    return fail("Patch binary data is incomplete.");
  }
  patch.channel.amplitude_modulation_sensitivity =
      static_cast<uint8_t>(value);
  if (!reader.read_bits(3, value)) {
    return fail("Patch binary data is incomplete.");
  }
  patch.channel.frequency_modulation_sensitivity =
      static_cast<uint8_t>(value);

  if (!reader.read_bits(3, value)) {
    return fail("Patch binary data is incomplete.");
  }
  patch.instrument.feedback = static_cast<uint8_t>(value);
  if (!reader.read_bits(3, value)) {
    return fail("Patch binary data is incomplete.");
  }
  patch.instrument.algorithm = static_cast<uint8_t>(value);

  for (size_t ui_index = 0; ui_index < 4; ++ui_index) {
    auto &op = operator_for_ui_index(patch, ui_index);
    if (!reader.read_bits(5, value)) {
      return fail("Patch binary data is incomplete.");
    }
    op.attack_rate = static_cast<uint8_t>(value);
    if (!reader.read_bits(5, value)) {
      return fail("Patch binary data is incomplete.");
    }
    op.decay_rate = static_cast<uint8_t>(value);
    if (!reader.read_bits(5, value)) {
      return fail("Patch binary data is incomplete.");
    }
    op.sustain_rate = static_cast<uint8_t>(value);
    if (!reader.read_bits(4, value)) {
      return fail("Patch binary data is incomplete.");
    }
    op.release_rate = static_cast<uint8_t>(value);
    if (!reader.read_bits(4, value)) {
      return fail("Patch binary data is incomplete.");
    }
    op.sustain_level = static_cast<uint8_t>(value);
    if (!reader.read_bits(7, value)) {
      return fail("Patch binary data is incomplete.");
    }
    op.total_level = static_cast<uint8_t>(value);
    if (!reader.read_bits(2, value)) {
      return fail("Patch binary data is incomplete.");
    }
    op.key_scale = static_cast<uint8_t>(value);
    if (!reader.read_bits(4, value)) {
      return fail("Patch binary data is incomplete.");
    }
    op.multiple = static_cast<uint8_t>(value);
    if (!reader.read_bits(3, value)) {
      return fail("Patch binary data is incomplete.");
    }
    op.detune = static_cast<uint8_t>(value);
    if (!reader.read_bits(3, value)) {
      return fail("Patch binary data is incomplete.");
    }
    op.ssg_type_envelope_control = static_cast<uint8_t>(value);
    if (!reader.read_bits(1, value)) {
      return fail("Patch binary data is incomplete.");
    }
    op.ssg_enable = value != 0;
    if (!reader.read_bits(1, value)) {
      return fail("Patch binary data is incomplete.");
    }
    op.amplitude_modulation_enable = value != 0;
    if (!reader.read_bits(1, value)) {
      return fail("Patch binary data is incomplete.");
    }
    op.enable = value != 0;
  }

  return patch;
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
  std::string query =
      "?v=" + std::to_string(kQueryVersion) + "&p=";
  query += base64url_encode(pack_patch_binary(patch));
  return query;
}

std::optional<ym2612::Patch>
parse_query(std::string_view query, const ym2612::Patch &defaults,
            std::string *error) {
  std::string normalized = strip_query_prefix(query);
  if (normalized.empty()) {
    return std::nullopt;
  }

  std::optional<std::string> binary_payload;
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
    auto key = decode_component(raw_key, true);
    auto value = decode_component(raw_value, key != "p");

    if (key == "p") {
      binary_payload = std::move(value);
      continue;
    }

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

  if (binary_payload.has_value()) {
    auto decoded = base64url_decode(*binary_payload);
    if (!decoded.has_value()) {
      if (error) {
        *error = "Invalid base64url patch payload.";
      }
      return std::nullopt;
    }
    return unpack_patch_binary(*decoded, defaults, error);
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
