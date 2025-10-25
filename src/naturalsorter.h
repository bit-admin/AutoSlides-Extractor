#ifndef NATURALSORTER_H
#define NATURALSORTER_H

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QList>

/**
 * @brief Natural-order string sorting with weekday/month awareness.
 *
 * Splits strings into typed tokens (integers vs. text) so "slide_2" sorts
 * before "slide_10", and recognises Chinese (星期一..日) and English weekday
 * and month names so date-stamped folder names order chronologically rather
 * than lexically. Previously duplicated verbatim in PdfMakerDialog and
 * ReviewSlidesDialog; this is the single shared copy.
 */
namespace NaturalSorter {

/** Tokenize a string into a mix of int and QString tokens. */
QList<QVariant> tokenize(const QString& str);

/** Compare two strings by their natural-order token sequences. */
bool lessThan(const QString& a, const QString& b);

/** Sort a string list in place using lessThan(). */
void sort(QStringList& list);

} // namespace NaturalSorter

#endif // NATURALSORTER_H
