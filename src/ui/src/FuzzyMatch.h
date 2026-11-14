// Fuzzy matching of a query against a text as a case-insensitive subsequence, scored for ranking the
// command palette.
#pragma once

#include <QStringView>

#include <optional>

namespace mikserstudio::ui {

// Scores how well `query` matches `text` as a case-insensitive subsequence ("tlac" matches
// "timeline.add_clip"). Higher is better; matches at word starts (after ' ', '.', '_', '-', '/',
// or a lower-to-upper case change) and runs of consecutive characters score more, gaps score
// less. std::nullopt when `query` is not a subsequence of `text`. An empty query scores 0.
[[nodiscard]] std::optional<int> fuzzyScore(QStringView query, QStringView text);

}  // namespace mikserstudio::ui
