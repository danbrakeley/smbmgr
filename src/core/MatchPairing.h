#pragma once

#include <QList>
#include <QString>

#include <algorithm>

// The pure pairing math behind MatchSearcher, kept free of the traversal so
// the size-window edge cases are testable without a server. MatchSearcher
// gathers FileRecords from directory enumerations and hands them here.
namespace matchpairing {

// Which of the two searched trees a file belongs to (bits in FileRecord::sides).
constexpr quint8 kPrimary = 1;
constexpr quint8 kSecondary = 2;

// Default safety valves against pathological inputs (huge windows of
// equal-size files). Hitting either reports the results as truncated.
constexpr int kMaxMatches = 100000;
constexpr qint64 kMaxPairsExamined = 20000000;

// One candidate near-duplicate pair.
struct Match
{
    QString primaryPath;   // full share-absolute file path
    QString secondaryPath; // full share-absolute file path
    quint64 primarySize = 0;
    quint64 secondarySize = 0;
};

struct FileRecord
{
    QString path;
    quint64 size = 0;
    quint64 inode = 0;
    quint8 sides = 0;
};

struct Result
{
    QList<Match> matches; // biggest primary first
    bool truncated = false;
};

// In-progress LinkRunner temporaries (see LinkRunner.cpp); never candidates.
inline bool isTmpName(const QString &name)
{
    return name.endsWith(QLatin1String(".smbmgr-tmp"));
}

// Sort by size, then for each file scan the window of files within
// sizeDiffBytes above it. Pairs that already share an inode are dropped as
// already linked, and exactly one row is produced per unordered pair: the
// lower-sorted file as primary when its sides allow it, otherwise the flipped
// orientation (files in the overlap of both trees qualify either way; the
// first form wins).
inline Result computeMatches(QList<FileRecord> files, quint64 sizeDiffBytes,
                             int maxMatches = kMaxMatches,
                             qint64 maxPairsExamined = kMaxPairsExamined)
{
    std::sort(files.begin(), files.end(),
              [](const FileRecord &a, const FileRecord &b) { return a.size < b.size; });

    Result result;
    qint64 pairsExamined = 0;
    for (int i = 0; i < files.size() && !result.truncated; ++i) {
        const FileRecord &a = files.at(i);
        for (int j = i + 1; j < files.size(); ++j) {
            const FileRecord &b = files.at(j);
            if (b.size - a.size > sizeDiffBytes) {
                break; // sorted: everything further is too far apart
            }
            if (++pairsExamined > maxPairsExamined) {
                result.truncated = true;
                break;
            }
            if (a.path == b.path) {
                continue;
            }
            if (a.inode != 0 && a.inode == b.inode) {
                continue; // already hard-linked
            }
            if ((a.sides & kPrimary) && (b.sides & kSecondary)) {
                result.matches.append({a.path, b.path, a.size, b.size});
            } else if ((b.sides & kPrimary) && (a.sides & kSecondary)) {
                result.matches.append({b.path, a.path, b.size, a.size});
            } else {
                continue;
            }
            if (result.matches.size() >= maxMatches) {
                result.truncated = true;
                break;
            }
        }
    }

    // Biggest potential savings first.
    std::stable_sort(result.matches.begin(), result.matches.end(),
                     [](const Match &a, const Match &b) {
                         return a.primarySize > b.primarySize;
                     });

    return result;
}

} // namespace matchpairing
