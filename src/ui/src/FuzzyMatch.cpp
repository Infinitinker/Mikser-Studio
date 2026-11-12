#include "FuzzyMatch.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace mikserstudio::ui {

namespace {

constexpr int kNone = std::numeric_limits<int>::min() / 2;
constexpr int kMatch = 1;
constexpr int kWordStart = 10;
constexpr int kFirstCharacter = 6;
constexpr int kConsecutive = 8;
constexpr int kMaxGapPenalty = 6;

bool isWordStart(QStringView text, qsizetype index)
{
    if (index == 0)
        return true;
    const QChar previous = text.at(index - 1);
    const QChar current = text.at(index);
    if (previous == u' ' || previous == u'.' || previous == u'_' || previous == u'-' || previous == u'/')
        return true;
    return previous.isLower() && current.isUpper();
}

int gapPenalty(qsizetype gap)
{
    return static_cast<int>(std::min<qsizetype>(gap, kMaxGapPenalty));
}

}  // namespace

std::optional<int> fuzzyScore(QStringView query, QStringView text)
{
    const qsizetype n = query.size();
    const qsizetype m = text.size();
    if (n == 0)
        return 0;
    if (n > m)
        return std::nullopt;

    // best[i][j]: best score of query[0..i] with query[i] matched at text[j].
    std::vector<std::vector<int>> best(static_cast<std::size_t>(n),
                                       std::vector<int>(static_cast<std::size_t>(m), kNone));
    const auto at = [&best](qsizetype i, qsizetype j) -> int& {
        return best[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
    };
    for (qsizetype i = 0; i < n; ++i) {
        const QChar wanted = query.at(i).toLower();
        for (qsizetype j = i; j < m; ++j) {
            if (text.at(j).toLower() != wanted)
                continue;
            const int bonus = kMatch + (isWordStart(text, j) ? kWordStart : 0) + (j == 0 ? kFirstCharacter : 0);
            if (i == 0) {
                at(i, j) = bonus - gapPenalty(j);
                continue;
            }
            int previous = kNone;
            for (qsizetype k = i - 1; k < j; ++k) {
                if (at(i - 1, k) == kNone)
                    continue;
                const int link = k == j - 1 ? kConsecutive : -gapPenalty(j - k - 1);
                previous = std::max(previous, at(i - 1, k) + link);
            }
            if (previous != kNone)
                at(i, j) = previous + bonus;
        }
    }
    int result = kNone;
    for (qsizetype j = 0; j < m; ++j)
        result = std::max(result, at(n - 1, j));
    if (result == kNone)
        return std::nullopt;
    return result;
}

}  // namespace mikserstudio::ui
