#include "../test_check.hpp"
#include "core/status.hpp"
#include "gui/components/operator_commands.hpp"
#include "ym2612/operator_text.hpp"
#include "ym2612/patch.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

struct TestEnvironment {
  ym2612::ChannelInstrument instrument;
  ui::OperatorEditState state;
  ym2612::OperatorClipboard clipboard;
  std::string text;
  bool clipboard_available = true;
  std::vector<std::string> events;
  ym2612::ChannelInstrument before;
  ym2612::ChannelInstrument after;
  ui::OperatorCommandContext context{instrument, state, clipboard,
                                     [this](const std::string &label) {
                                       events.push_back(label);
                                       before = instrument;
                                     },
                                     [this] {
                                       events.push_back("commit");
                                       after = instrument;
                                     }};

  TestEnvironment() {
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    auto &platform = ImGui::GetPlatformIO();
    platform.Platform_ClipboardUserData = this;
    platform.Platform_GetClipboardTextFn = [](ImGuiContext *) -> const char * {
      auto &env = *static_cast<TestEnvironment *>(
          ImGui::GetPlatformIO().Platform_ClipboardUserData);
      return env.clipboard_available ? env.text.c_str() : nullptr;
    };
    platform.Platform_SetClipboardTextFn = [](ImGuiContext *,
                                              const char *text) {
      auto &env = *static_cast<TestEnvironment *>(
          ImGui::GetPlatformIO().Platform_ClipboardUserData);
      env.text = text;
    };
    instrument.feedback = 6;
    instrument.algorithm = 3;
    for (int slot = 0; slot < 4; ++slot) {
      auto &op = ym2612::operator_at(instrument, slot);
      op.attack_rate = static_cast<uint8_t>(slot + 10);
      op.total_level = static_cast<uint8_t>(slot * 20);
      op.multiple = static_cast<uint8_t>(slot + 1);
      op.ssg_enable = true;
      op.ssg_type_envelope_control = static_cast<uint8_t>(slot);
      op.amplitude_modulation_enable = true;
      op.enable = slot % 2 == 0;
    }
    megatoy::status::clear();
  }

  ~TestEnvironment() {
    megatoy::status::clear();
    ImGui::DestroyContext();
  }
};

void check_notice(megatoy::status::Severity severity, const std::string &text) {
  const auto notices = megatoy::status::entries();
  CHECK(!notices.empty());
  CHECK(notices.back().severity == severity);
  CHECK(notices.back().message == text);
}

void check_pasted(const ym2612::OperatorSettings &actual,
                  ym2612::OperatorSettings source, bool enabled) {
  source.enable = enabled;
  CHECK(actual == source);
}

void test_labels_and_availability_follow_the_selection() {
  TestEnvironment env;
  CHECK(!ui::can_copy_operator(env.state));
  CHECK(!ui::can_paste_operator(env.state, env.clipboard));
  CHECK(!ui::can_swap_operators(env.state));
  CHECK(ui::copy_operator_label(env.state) == "Copy Operator");
  CHECK(ui::paste_operator_label(env.state) == "Paste Operator");
  CHECK(ui::swap_operators_label(env.state) == "Swap Operators");
  env.state.selection.select_only(2);
  env.state.selection.extend(0);
  CHECK(ui::can_copy_operator(env.state));
  CHECK(ui::can_swap_operators(env.state));
  CHECK(ui::copy_operator_label(env.state) == "Copy OP1, OP3");
  CHECK(ui::paste_operator_label(env.state) == "Paste to OP1, OP3");
  CHECK(ui::swap_operators_label(env.state) == "Swap OP1 and OP3");
}

void test_copy_without_a_selection_preserves_both_clipboards() {
  TestEnvironment env;
  env.clipboard = ym2612::copy_operator(env.instrument, 0);
  env.text = "other application";
  ui::copy_operator_command(env.context);
  CHECK(env.clipboard.has_value);
  CHECK(env.clipboard.op == ym2612::operator_at(env.instrument, 0));
  CHECK(env.clipboard.feedback == 6);
  CHECK(env.text == "other application");
  CHECK(env.events.empty());
  check_notice(megatoy::status::Severity::Warning,
               "Select an operator to copy.");
}

void test_copy_exports_selected_rows_in_display_order() {
  TestEnvironment env;
  const auto original = env.instrument;
  env.state.selection.select_only(3);
  env.state.selection.extend(1);
  ui::copy_operator_command(env.context);
  CHECK(env.text ==
        ym2612::format_operator_line(ym2612::operator_at(original, 1), 1) +
            ym2612::format_operator_line(ym2612::operator_at(original, 3), 3));
  CHECK(env.clipboard.text == env.text);
  CHECK(env.clipboard.op == ym2612::operator_at(original, 1));
  CHECK(!env.clipboard.feedback);
  CHECK(env.instrument == original);
  CHECK(env.events.empty());
  check_notice(megatoy::status::Severity::Info, "Copied OP2, OP4.");
}

void test_multi_operator_copy_pastes_distinct_rows() {
  TestEnvironment env;
  const auto original = env.instrument;
  env.state.selection.select_only(3);
  env.state.selection.extend(0);
  ui::copy_operator_command(env.context);
  env.state.selection.select_only(2);
  env.state.selection.extend(1);
  ui::paste_operator_command(env.context);
  check_pasted(ym2612::operator_at(env.instrument, 1),
               ym2612::operator_at(original, 0), false);
  check_pasted(ym2612::operator_at(env.instrument, 2),
               ym2612::operator_at(original, 3), true);
  CHECK(env.instrument.feedback == original.feedback);
  CHECK(
      (env.events == std::vector<std::string>{"Paste to OP2, OP3", "commit"}));
}

void test_own_single_copy_restores_feedback_only_at_op1() {
  TestEnvironment env;
  env.state.selection.select_only(0);
  ui::copy_operator_command(env.context);
  const auto source = env.clipboard.op;
  env.instrument.feedback = 1;
  env.state.selection.select_only(1);
  ui::paste_operator_command(env.context);
  CHECK(env.instrument.feedback == 1);
  check_pasted(ym2612::operator_at(env.instrument, 1), source, false);
  env.state.selection.select_only(0);
  env.state.selection.extend(3);
  CHECK(ui::can_paste_operator(env.state, env.clipboard));
  const auto original = env.instrument;
  ui::paste_operator_command(env.context);
  CHECK(env.instrument.feedback == 6);
  check_pasted(ym2612::operator_at(env.instrument, 3), source, false);
  CHECK(env.before == original);
  CHECK(env.after == env.instrument);
  CHECK(
      (env.events == std::vector<std::string>{"Paste to OP2", "commit",
                                              "Paste to OP1, OP4", "commit"}));
}

void test_external_single_row_fills_selected_slots_and_preserves_mutes() {
  TestEnvironment env;
  const auto original = env.instrument;
  env.clipboard = ym2612::copy_operator(env.instrument, 0);
  env.clipboard.text = "different clipboard text";
  env.text = "31 20 19 15 14 127 3 15 7 115 ; external\r\n";
  env.state.selection.select_only(0);
  env.state.selection.extend(1);
  const auto source = ym2612::parse_operator_lines(env.text).front();
  ui::paste_operator_command(env.context);
  check_pasted(ym2612::operator_at(env.instrument, 0), source, true);
  check_pasted(ym2612::operator_at(env.instrument, 1), source, false);
  CHECK(ym2612::operator_at(env.instrument, 2) ==
        ym2612::operator_at(original, 2));
  CHECK(env.instrument.feedback == original.feedback);
  CHECK(
      (env.events == std::vector<std::string>{"Paste to OP1, OP2", "commit"}));
  check_notice(megatoy::status::Severity::Success, "Pasted into OP1, OP2.");
}

void test_four_rows_paste_without_a_selection() {
  for (const int selected : {-1, 2}) {
    TestEnvironment env;
    if (selected >= 0) {
      env.state.selection.select_only(selected);
    }
    const auto original = env.instrument;
    env.text = ym2612::format_operator_lines(original, {3, 2, 1, 0});
    CHECK(ui::can_paste_operator(env.state, env.clipboard));
    ui::paste_operator_command(env.context);
    for (int slot = 0; slot < 4; ++slot) {
      check_pasted(ym2612::operator_at(env.instrument, slot),
                   ym2612::operator_at(original, 3 - slot), slot % 2 == 0);
    }
    CHECK(env.instrument.feedback == original.feedback);
    CHECK((env.events ==
           std::vector<std::string>{"Paste to OP1, OP2, OP3, OP4", "commit"}));
    check_notice(megatoy::status::Severity::Success,
                 "Pasted 4 operators into OP1, OP2, OP3, OP4.");
  }
}

void test_partial_rows_follow_selection_order_and_ignore_other_text() {
  TestEnvironment env;
  const auto original = env.instrument;
  env.text =
      "@1\nmalformed row\n" + ym2612::format_operator_lines(original, {3, 0});
  env.state.selection.select_only(3);
  env.state.selection.extend(1);
  env.state.selection.extend(2);
  ui::paste_operator_command(env.context);
  check_pasted(ym2612::operator_at(env.instrument, 1),
               ym2612::operator_at(original, 3), false);
  check_pasted(ym2612::operator_at(env.instrument, 2),
               ym2612::operator_at(original, 0), true);
  CHECK(ym2612::operator_at(env.instrument, 3) ==
        ym2612::operator_at(original, 3));
  CHECK(
      (env.events == std::vector<std::string>{"Paste to OP2, OP3", "commit"}));
}

void test_text_values_are_clamped_before_paste() {
  TestEnvironment env;
  env.state.selection.select_only(2);
  env.text = "-1 999 100 -8 99 999 99 -3 90 0\n";
  ui::paste_operator_command(env.context);
  const auto &op = ym2612::operator_at(env.instrument, 2);
  CHECK(op.attack_rate == 0);
  CHECK(op.decay_rate == 31);
  CHECK(op.sustain_rate == 31);
  CHECK(op.release_rate == 0);
  CHECK(op.sustain_level == 15);
  CHECK(op.total_level == 127);
  CHECK(op.key_scale == 3);
  CHECK(op.multiple == 0);
  CHECK(op.detune == 7);
  CHECK(!op.ssg_enable);
  CHECK(!op.amplitude_modulation_enable);
}

void test_unavailable_or_invalid_text_uses_the_internal_copy() {
  for (const std::string text :
       {"", "not MML", "1 2 3", "1 2 3 4 5 6 7 8 9 x"}) {
    TestEnvironment env;
    env.clipboard = ym2612::copy_operator(env.instrument, 2);
    env.text = text;
    env.state.selection.select_only(1);
    CHECK(ui::can_paste_operator(env.state, env.clipboard));
    ui::paste_operator_command(env.context);
    check_pasted(ym2612::operator_at(env.instrument, 1), env.clipboard.op,
                 false);
  }
  TestEnvironment env;
  env.clipboard_available = false;
  env.clipboard = ym2612::copy_operator(env.instrument, 0);
  env.state.selection.select_only(3);
  ui::paste_operator_command(env.context);
  check_pasted(ym2612::operator_at(env.instrument, 3), env.clipboard.op, false);
}

void test_failed_paste_leaves_the_patch_and_transaction_alone() {
  TestEnvironment env;
  const auto original = env.instrument;
  env.state.selection.select_only(1);
  env.text = "malformed";
  ui::paste_operator_command(env.context);
  check_notice(megatoy::status::Severity::Warning, "Copy an operator first.");
  env.clipboard = ym2612::copy_operator(env.instrument, 0);
  env.state.selection.clear();
  ui::paste_operator_command(env.context);
  check_notice(megatoy::status::Severity::Warning,
               "Select an operator to paste into.");
  env.text = ym2612::format_operator_lines(original, {0});
  ui::paste_operator_command(env.context);
  CHECK(!ui::can_paste_operator(env.state, env.clipboard));
  CHECK(env.instrument == original);
  CHECK(env.events.empty());
}

void test_swap_is_one_transaction_and_preserves_feedback_and_mutes() {
  TestEnvironment env;
  const auto original = env.instrument;
  for (const int count : {0, 1, 3, 4}) {
    env.state.selection.clear();
    for (int slot = 0; slot < count; ++slot) {
      env.state.selection.extend(slot);
    }
    ui::swap_operators_command(env.context);
    CHECK(env.instrument == original);
    CHECK(env.events.empty());
  }
  env.state.selection.select_only(3);
  env.state.selection.extend(0);
  ui::swap_operators_command(env.context);
  check_pasted(ym2612::operator_at(env.instrument, 0),
               ym2612::operator_at(original, 3), true);
  check_pasted(ym2612::operator_at(env.instrument, 3),
               ym2612::operator_at(original, 0), false);
  CHECK(env.instrument.feedback == original.feedback);
  CHECK(env.before == original);
  CHECK(env.after == env.instrument);
  CHECK((env.events == std::vector<std::string>{"Swap OP1 and OP4", "commit"}));
  ui::swap_operators_command(env.context);
  CHECK(env.instrument == original);
}

void test_commands_work_without_history_callbacks() {
  TestEnvironment env;
  const auto original = env.instrument;
  env.context.begin_history = {};
  env.context.commit_history = {};
  env.state.selection.select_only(0);
  ui::copy_operator_command(env.context);
  env.state.selection.select_only(1);
  ui::paste_operator_command(env.context);
  check_pasted(ym2612::operator_at(env.instrument, 1), env.clipboard.op, false);
  env.state.selection.extend(2);
  ui::swap_operators_command(env.context);
  check_pasted(ym2612::operator_at(env.instrument, 1),
               ym2612::operator_at(original, 2), false);
  check_pasted(ym2612::operator_at(env.instrument, 2), env.clipboard.op, true);
  CHECK(env.events.empty());
}

} // namespace

int main() {
  test_labels_and_availability_follow_the_selection();
  test_copy_without_a_selection_preserves_both_clipboards();
  test_copy_exports_selected_rows_in_display_order();
  test_multi_operator_copy_pastes_distinct_rows();
  test_own_single_copy_restores_feedback_only_at_op1();
  test_external_single_row_fills_selected_slots_and_preserves_mutes();
  test_four_rows_paste_without_a_selection();
  test_partial_rows_follow_selection_order_and_ignore_other_text();
  test_text_values_are_clamped_before_paste();
  test_unavailable_or_invalid_text_uses_the_internal_copy();
  test_failed_paste_leaves_the_patch_and_transaction_alone();
  test_swap_is_one_transaction_and_preserves_feedback_and_mutes();
  test_commands_work_without_history_callbacks();
  std::cout << "All operator command tests passed\n";
  return 0;
}
