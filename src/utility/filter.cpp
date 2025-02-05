/* Copyright (C) 2004-2021 Robert Griebl. All rights reserved.
**
** This file is part of BrickStore.
**
** This file may be distributed and/or modified under the terms of the GNU
** General Public License version 2 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://fsf.org/licensing/licenses/gpl.html for GPL licensing information.
*/

#include <QStringList>
#include <QCoreApplication>
#include <QVariant>
#include <QRegularExpression>
#include <QDebug>
#include <QLocale>

#include "utility.h"
#include "filter.h"


static QString quote(const QString &str)
{
    if (str.isEmpty() || str.contains(' '_l1))
        return u'"' % str % u'"';
    else
        return str;
};


void Filter::setField(int field)
{
    m_field = field;
}

void Filter::setExpression(const QString &expr)
{
    m_expression = expr;

    QLocale loc;
    bool isInt = false;
    m_asInt = loc.toInt(expr, &isInt);
    m_isInt = isInt;
    bool isDouble = false;
    m_asDouble = loc.toDouble(expr, &isDouble);
    m_isDouble = isDouble;

    if (expr.contains('?'_l1) || expr.contains('*'_l1) || expr.contains('['_l1)) {
        m_isRegExp = true;
        m_asRegExp.setPattern(QRegularExpression::wildcardToRegularExpression(expr));
        m_asRegExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    }
}

void Filter::setComparison(Comparison cmp)
{
    m_comparison = cmp;
}

void Filter::setCombination(Combination cmb)
{
    m_combination = cmb;
}

bool Filter::matches(const QVariant &v) const
{
    bool isInt = false;
    qint64 i1 = 0, i2 = 0;
    QString s1, s2;
    
    switch (v.userType()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong: {
        if (!m_isInt)
            return false; // data is int, but expression is not
        i1 = m_asInt;
        i2 = v.toInt();
        isInt = true;
        break;
    }   
    case QMetaType::Double: {
        if (!m_isDouble)
            return false;
        i1 = qRound64(m_asDouble * 1000.);
        i2 = qRound64(v.toDouble() * 1000.);
        isInt = true;
        break;
    }    
    default:
        s1 = m_expression;
        s2 = v.toString();
        break;
    }

    switch (comparison()) {
    case Is:
        return isInt ? i2 == i1 : s2.compare(s1, Qt::CaseInsensitive) == 0;
    case IsNot:
        return isInt ? i2 != i1 : s2.compare(s1, Qt::CaseInsensitive) != 0;
    case Less:
        return isInt ? i2 < i1 : false;
    case LessEqual:
        return isInt ? i2 <= i1 : false;
    case Greater:
        return isInt ? i2 > i1 : false;
    case GreaterEqual:
        return isInt ? i2 >= i1 : false;
    case StartsWith:
        return isInt ? false : s2.startsWith(s1, Qt::CaseInsensitive);
    case DoesNotStartWith:
        return isInt ? false : !s2.startsWith(s1, Qt::CaseInsensitive);
    case EndsWith:
        return isInt ? false : s2.endsWith(s1, Qt::CaseInsensitive);
    case DoesNotEndWith:
        return isInt ? false : !s2.endsWith(s1, Qt::CaseInsensitive);
    case Matches:
    case DoesNotMatch: {
        if (m_isRegExp) {
            // We are using QRegularExpressions in multiple threads here, although the class is not
            // marked thread-safe. We are relying on the const match() function to be thread-safe,
            // which it currently is up to Qt 6.0.

            bool res = m_asRegExp.match(v.toString()).hasMatch();
            return (comparison() == Matches) ? res : !res;
        } else {
            bool res = v.toString().contains(m_expression, Qt::CaseInsensitive);
            return (comparison() == Matches) ? res : !res;
        }
    }
    }
    return false;
}

QString Filter::Parser::toString(const QVector<Filter> &filter, bool preferSymbolic) const
{
    QString result;

    for (int i = 0; i < filter.size(); ++i) {
        const Filter &f = filter.at(i);

        if ((f.field() != -1) || (f.comparison() != Matches)) {
            for (const auto &ft : m_field_tokens) {
                if (ft.first == f.field()) {
                    result = result % quote(ft.second);
                    break;
                }
            }
            for (const auto &ct : m_comparison_tokens) {
                if (ct.first == f.comparison()) {
                    if (preferSymbolic && ct.second.at(0).isLetterOrNumber())
                        continue;
                    result = result % u' ' % quote(ct.second) % u' ';
                    break;
                }
            }
        }
        result = result % quote(f.expression());

        if (i < (filter.size() - 1)) {
            for (const auto &ct : m_combination_tokens) {
                if (ct.first == f.combination()) {
                    if (preferSymbolic && ct.second.at(0).isLetterOrNumber())
                        continue;
                    result = result % u' ' % quote(ct.second) % u' ';
                    break;
                }
            }
        }
    }
    if (result == R"("")"_l1)
        result.clear();
    return result;
}


enum State {
    StateStart,
    StateCompare,
    StateExpression,
    StateCombination,
    StateInvalid
};


template <typename T>
static T findInTokens(const QString &word, const QVector<QPair<T, QString>> &tokens, T notFound = T { })
{
    for (const auto &token : tokens) {
        if (word.compare(token.second, Qt::CaseInsensitive) == 0)
            return token.first;
    }
    return notFound;
}

QVector<Filter> Filter::Parser::parse(const QString &str)
{
    // match words, which are either quoted with ["], quoted with ['] or unquoted
    static const QRegularExpression re(R"-("([^"]*)"|'([^']*)'|([^ ]+))-"_l1);
    auto matches = re.globalMatch(str % " &&"_l1);

    QVector<Filter> filters;
    Filter f;
    State state = StateStart;

    while (state != StateInvalid && matches.hasNext()) {
        auto match = matches.next();
        QString word = match.captured(1) % match.captured(2) % match.captured(3);

retryState:
        switch(state) {
        case StateStart: {
            int field = findInTokens(word, m_field_tokens, -2);

            if (field > -2) {
                f.setField(field);
                state = StateCompare;
            } else {
                state = StateExpression;
                goto retryState;
            }
            break;
        }
        case StateCompare: {
            auto comparison = findInTokens(word, m_comparison_tokens, Filter::Matches);
            f.setComparison(comparison);
            state = StateExpression;
            break;
        }
        case StateExpression: {
            f.setExpression(word);
            state = StateCombination;
            break;
        }
        case StateCombination: {
            auto combination = findInTokens(word, m_combination_tokens, Combination(-1));
            if (combination < 0) {
                state = StateInvalid;
            } else {
                f.setCombination(combination);
                filters.append(f);
                f = Filter();
                state = StateStart;
            }
            break;
        }
        case StateInvalid:
            break;
        }
    }
    return filters;
}


template<typename T>
static QString toHtml(const QVector<QPair<T, QString>> &tokens, const QString &before, const QString &after,
                      const QString &key_before, const QString &key_after, const QString &key_separator,
                      const QString &value_before, const QString &value_after, const QString &value_separator)
{
    QVector<T> keys;
    for (const auto &p : tokens) {
        if (!keys.contains(p.first))
            keys.append(p.first);
    }

    QString res;
    bool first_key = true;
    for (const T &key : qAsConst(keys)) {
        if (first_key)
            first_key = false;
        else
            res = res % key_separator;
        res = res % key_before;
        bool first_value = true;
        for (const auto &p : tokens) {
            if (p.first != key)
                continue;

            if (first_value)
                first_value = false;
            else
                res = res % value_separator;
            res = res % value_before % quote(p.second).toHtmlEscaped() % value_after;
        }
        res = res % key_after;
    }
    if (!res.isEmpty())
        res = before % res % after;
    return res;
}

QString Filter::Parser::toolTip() const
{
    QString tt = Filter::tr("<p>Enter the filter expression in either (near) natural language or with logical operators.<br />"
       "A single expression looks like <b><i>FIELDNAME COMPARSION</i> TEXT</b>. <b><i>FIELDNAME</i></b> and "
       "<b><i>COMPARISON</i></b> are optional and default to <b>in any field</b> and <b>contains</b> respectively.</p>"
       "<p>Multiple expressions can be combined by separating them with a <b>COMBINATION</b> token.</p>"
       "<p>E.g. to search for anything resembling an brick in blue, you could use: <b>brick and color is blue</b></p>");

    QString block = "<b><u>%1</u></b>%2"_l1;
    tt += block.arg(Filter::tr("Field names:"),
                    toHtml(m_field_tokens,
                           "<ul><li>"_l1, "</li></ul>"_l1,
                           QString(),  QString(), ", "_l1,
                           "<b>"_l1, "</b>"_l1, " / "_l1));
    tt += block.arg(Filter::tr("Comparisons:"),
                    toHtml(m_comparison_tokens,
                           "<ul>"_l1, "</ul>"_l1,
                           "<li>"_l1, "</li>"_l1, QString(),
                           "<b>"_l1, "</b>"_l1, " / "_l1));
    tt += block.arg(Filter::tr("Combinations:"),
                    toHtml(m_combination_tokens,
                           "<ul>"_l1, "</ul>"_l1,
                           "<li>"_l1, "</li>"_l1, QString(),
                           "<b>"_l1, "</b>"_l1, " / "_l1));
    return tt;
}

void Filter::Parser::setFieldTokens(const QVector<QPair<int, QString>> &tokens)
{
    m_field_tokens = tokens;
}

void Filter::Parser::setComparisonTokens(const QVector<QPair<Filter::Comparison, QString>> &tokens)
{
    m_comparison_tokens = tokens;
}

void Filter::Parser::setCombinationTokens(const QVector<QPair<Filter::Combination, QString>> &tokens)
{
    m_combination_tokens = tokens;
}

QVector<QPair<int, QString>> Filter::Parser::fieldTokens() const
{
    return m_field_tokens;
}

QVector<QPair<Filter::Comparison, QString>> Filter::Parser::comparisonTokens() const
{
    return m_comparison_tokens;
}

QVector<QPair<Filter::Combination, QString>> Filter::Parser::combinationTokens() const
{
    return m_combination_tokens;
}

void Filter::Parser::setStandardComparisonTokens(Filter::Comparisons mask)
{
    m_comparison_tokens = standardComparisonTokens(mask);
}

void Filter::Parser::setStandardCombinationTokens(Filter::Combinations mask)
{
    m_combination_tokens = standardCombinationTokens(mask);
}

QVector<QPair<Filter::Combination, QString>> Filter::Parser::standardCombinationTokens(Filter::Combinations mask)
{
    struct token_table { 
        Filter::Combination m_combination;
        const char *        m_symbols;
        const char *        m_words;
    } predefined[] = {
        { Filter::And, "&,&&", QT_TR_NOOP( "and" ) },
        { Filter::Or,  "|,||", QT_TR_NOOP( "or" ) },
        
        { Filter::And, nullptr, nullptr }
    };
    
    QVector<QPair<Filter::Combination, QString>> dct;
    
    for (token_table *tt = predefined; tt->m_symbols || tt->m_words; ++tt) {
        if (!(mask & tt->m_combination))
            continue;
    
        foreach (QString word, Filter::tr(tt->m_words).split(QLatin1Char(',')))
            dct.append({ tt->m_combination, word });
        foreach (QString symbol, QString::fromLatin1(tt->m_symbols).split(QLatin1Char(',')))
            dct.append({ tt->m_combination, symbol });
    }
    return dct;
}

QVector<QPair<Filter::Comparison, QString>> Filter::Parser::standardComparisonTokens(Filter::Comparisons mask)
{
    struct token_table { 
        Filter::Comparison m_comparison;
        const char *       m_symbols;
        const char *       m_words;
    } predefined[] = {
        { Filter::Is,               "=,==,===",         QT_TR_NOOP( "is,equals" ) },
        { Filter::IsNot,            "!=,=!,!==,==!,<>", QT_TR_NOOP( "is not,doesn't equal,does not equal" ) },
        { Filter::Less,             "<",                QT_TR_NOOP( "less than" ) },
        { Filter::LessEqual,        "<=,=<",            QT_TR_NOOP( "less equal than" ) },
        { Filter::Greater,          ">",                QT_TR_NOOP( "greater than" ) },
        { Filter::GreaterEqual,     ">=,=>",            QT_TR_NOOP( "greater equal than" ) },
        { Filter::Matches,          "~,~=,=~",          QT_TR_NOOP( "contains,matches" ) },
        { Filter::DoesNotMatch,     "!~,~!,!~=,!=~",    QT_TR_NOOP( "doesn't contain,does not contain,doesn't match,does not match" ) },
        { Filter::StartsWith,       "^,^=,=^",          QT_TR_NOOP( "starts with,begins with" ) },
        { Filter::DoesNotStartWith, "!^,^!=,!=^",       QT_TR_NOOP( "doesn't start with,does not start with,doesn't begin with,does not begin with" ) },
        { Filter::EndsWith,         "$,$=,=$",          QT_TR_NOOP( "ends with" ) },
        { Filter::DoesNotEndWith,   "!$,$!=,!=$",       QT_TR_NOOP( "doesn't end with,does not end with" ) },
        
        { Filter::Is, nullptr, nullptr }
    };
    
    QVector<QPair<Filter::Comparison, QString>> dct;
    
    for (token_table *tt = predefined; tt->m_symbols || tt->m_words; ++tt) {
        if (!(mask & tt->m_comparison))
            continue;
    
        foreach (QString word, Filter::tr(tt->m_words).split(QLatin1Char(',')))
            dct.append({ tt->m_comparison, word });
        foreach (QString symbol, QString::fromLatin1(tt->m_symbols).split(QLatin1Char(',')))
            dct.append({ tt->m_comparison, symbol });
    }
    return dct;
}

QDebug &operator<<(QDebug &dbg, const Filter &filter)
{
    dbg << "Filter { " << filter.field() << " " << filter.comparison() << " "  << filter.expression() << " "  << filter.combination() << " }";
    return dbg;
}
