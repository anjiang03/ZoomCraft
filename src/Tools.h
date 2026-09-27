#pragma once
// Shared annotation tool kinds (used by both the Annotator and the Toolbar, so
// neither has to include the other).

namespace aj {

enum class Tool : int { Pen, Line, Arrow, Rect, Ellipse, Highlight, Text };

inline constexpr int kToolCount = 7;

} // namespace aj
