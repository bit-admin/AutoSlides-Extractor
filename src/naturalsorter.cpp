#include "naturalsorter.h"

#include <QMap>
#include <QChar>
#include <algorithm>

namespace NaturalSorter {

QList<QVariant> tokenize(const QString& str)
{
    QList<QVariant> tokens;
    QString currentText;
    int i = 0;

    static QMap<QChar, int> weekdayMap = {
        {QChar(0x4E00), 1},
        {QChar(0x4E8C), 2},
        {QChar(0x4E09), 3},
        {QChar(0x56DB), 4},
        {QChar(0x4E94), 5},
        {QChar(0x516D), 6},
        {QChar(0x65E5), 7},
    };

    static QMap<QString, int> englishWeekdayMap = {
        {"monday", 1}, {"mon", 1},
        {"tuesday", 2}, {"tue", 2}, {"tues", 2},
        {"wednesday", 3}, {"wed", 3},
        {"thursday", 4}, {"thu", 4}, {"thur", 4}, {"thurs", 4},
        {"friday", 5}, {"fri", 5},
        {"saturday", 6}, {"sat", 6},
        {"sunday", 7}, {"sun", 7},
    };

    static QMap<QString, int> monthMap = {
        {"january", 1}, {"jan", 1},
        {"february", 2}, {"feb", 2},
        {"march", 3}, {"mar", 3},
        {"april", 4}, {"apr", 4},
        {"may", 5},
        {"june", 6}, {"jun", 6},
        {"july", 7}, {"jul", 7},
        {"august", 8}, {"aug", 8},
        {"september", 9}, {"sep", 9}, {"sept", 9},
        {"october", 10}, {"oct", 10},
        {"november", 11}, {"nov", 11},
        {"december", 12}, {"dec", 12},
    };

    while (i < str.length()) {
        if (i + 2 < str.length() &&
            str.mid(i, 2) == QString::fromUtf8("星期")) {
            if (!currentText.isEmpty()) {
                tokens.append(currentText);
                currentText.clear();
            }
            tokens.append(QString::fromUtf8("星期"));

            QChar weekdayChar = str.at(i + 2);
            if (weekdayMap.contains(weekdayChar)) {
                tokens.append(weekdayMap[weekdayChar]);
                i += 3;
                continue;
            }
        }

        if (str.at(i).isDigit()) {
            if (!currentText.isEmpty()) {
                tokens.append(currentText);
                currentText.clear();
            }

            QString numStr;
            while (i < str.length() && str.at(i).isDigit()) {
                numStr += str.at(i);
                ++i;
            }
            tokens.append(numStr.toInt());
            continue;
        }

        if (str.at(i).isLetter()) {
            QString word;
            while (i < str.length() && str.at(i).isLetter()) {
                word += str.at(i);
                ++i;
            }

            QString lowerWord = word.toLower();

            if (englishWeekdayMap.contains(lowerWord)) {
                if (!currentText.isEmpty()) {
                    tokens.append(currentText);
                    currentText.clear();
                }
                tokens.append(QString("__weekday__"));
                tokens.append(englishWeekdayMap[lowerWord]);
                continue;
            }

            if (monthMap.contains(lowerWord)) {
                if (!currentText.isEmpty()) {
                    tokens.append(currentText);
                    currentText.clear();
                }
                tokens.append(QString("__month__"));
                tokens.append(monthMap[lowerWord]);
                continue;
            }

            currentText += word;
            continue;
        }

        currentText += str.at(i);
        ++i;
    }

    if (!currentText.isEmpty()) {
        tokens.append(currentText);
    }

    return tokens;
}

bool lessThan(const QString& a, const QString& b)
{
    QList<QVariant> tokensA = tokenize(a);
    QList<QVariant> tokensB = tokenize(b);

    int len = qMin(tokensA.size(), tokensB.size());
    for (int i = 0; i < len; ++i) {
        const QVariant& va = tokensA[i];
        const QVariant& vb = tokensB[i];

        if (va.typeId() == QMetaType::Int && vb.typeId() == QMetaType::Int) {
            if (va.toInt() != vb.toInt()) {
                return va.toInt() < vb.toInt();
            }
            continue;
        }

        if (va.typeId() == QMetaType::QString && vb.typeId() == QMetaType::QString) {
            QString sa = va.toString();
            QString sb = vb.toString();
            int cmp = QString::localeAwareCompare(sa, sb);
            if (cmp != 0) {
                return cmp < 0;
            }
            continue;
        }

        if (va.typeId() == QMetaType::Int) {
            return true;
        }
        if (vb.typeId() == QMetaType::Int) {
            return false;
        }
    }

    return tokensA.size() < tokensB.size();
}

void sort(QStringList& list)
{
    std::sort(list.begin(), list.end(), lessThan);
}

} // namespace NaturalSorter
