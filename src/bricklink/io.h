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
#pragma once

#include <QtCore/QString>
#include <QtCore/QHash>
#include <QtXml/QDomElement>

#include "bricklink/global.h"
#include "bricklink/lot.h"

namespace BrickLink {
namespace IO {

class ParseResult
{
public:
    ParseResult() = default;
    ParseResult(const LotList &lots);
    ParseResult(const ParseResult &) = delete;
    ParseResult(ParseResult &&pr);

    virtual ~ParseResult();

    bool hasLots() const         { return !m_lots.isEmpty(); }
    const LotList &lots() const  { return m_lots; }
    LotList takeLots();
    QString currencyCode() const { return m_currencyCode; }
    bool hasOrder() const        { return (m_order); }
    Order *order() const         { return m_order; }
    Order *takeOrder();
    int invalidLotCount() const  { return m_invalidLotCount; }
    int fixedLotCount() const    { return m_fixedLotCount; }
    const QHash<const Lot *, Lot> &differenceModeBase() const { return m_differenceModeBase; }

    void addLot(Lot *&&lot);
    void setCurrencyCode(const QString &ccode) { m_currencyCode = ccode; }
    void addOrder();
    void incInvalidLotCount()    { ++m_invalidLotCount; }
    void incFixedLotCount()      { ++m_fixedLotCount; }
    void addToDifferenceModeBase(const Lot *lot, const Lot &base);

private:
    LotList m_lots;
    QString m_currencyCode;
    Order *m_order = nullptr;

    bool m_ownLots = true;
    bool m_ownOrder = true;
    int m_invalidLotCount = 0;
    int m_fixedLotCount = 0;

    QHash<const Lot *, Lot> m_differenceModeBase;
};

QString toWantedListXML(const LotList &lots, const QString &wantedList);
QString toInventoryRequest(const LotList &lots);
QString toBrickLinkUpdateXML(const LotList &lots,
                             std::function<const Lot *(const Lot *)> differenceBaseLot);

enum class Hint {
    Plain = 0x01,
    Wanted = 0x02,
    Order = 0x04,
    Store = 0x08,

    Any = Plain | Wanted | Order | Store
};

QString toBrickLinkXML(const LotList &lots);
ParseResult fromBrickLinkXML(const QByteArray &xml, Hint hint = Hint::Plain);

} // namespace IO
} // namespace BrickLink
