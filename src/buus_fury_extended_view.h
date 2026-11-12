#pragma once

#include <cstdint>

namespace gbarecomp {
struct ExtendedViewFrameInfo;
}

namespace buus_fury {

void install_extended_view(std::uint32_t extra_left,
                           std::uint32_t extra_right);

// Per-frame hook (RunOptions::extended_view_frame). Keeps the gameplay QoL
// hooks installed at every view width, including the faithful 240px view.
void frame_start(const gbarecomp::ExtendedViewFrameInfo* frame);

}  // namespace buus_fury
