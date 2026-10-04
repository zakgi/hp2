#include "core/presentation.hpp"

#include <cstdint>
#include <span>

namespace hp2 {

void PaletteFade::Init() {
  level_ = first_level_;
  screen_.ShowPalette(segments_, level_);
  remaining_seconds_ = step_seconds_;
}

bool PaletteFade::Tick(float delta_seconds) {
  remaining_seconds_ -= delta_seconds;
  while (remaining_seconds_ <= 0.0F and level_ != last_level_) {
    level_ = static_cast<std::uint8_t>(level_ < last_level_ ? level_ + 1 : level_ - 1);
    screen_.ShowPalette(segments_, level_);
    remaining_seconds_ += step_seconds_;
  }
  return level_ == last_level_ and remaining_seconds_ <= 0.0F;
}

}  // namespace hp2
