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
#include <QRegularExpression>
#include <QStringBuilder>

#include "bricklink/core.h"
#include "bricklink/lot.h"


BrickLink::Lot::Lot(const Color *color, const Item *item)
    : m_item(item)
    , m_color(color)
{ }

BrickLink::Lot::Lot(const Lot &copy)
{
    *this = copy;
}

BrickLink::Lot &BrickLink::Lot::operator=(const Lot &copy)
{
    if (this == &copy)
        return *this;

    m_item                = copy.m_item;
    m_color               = copy.m_color;

    m_incomplete.reset(copy.m_incomplete ? new Incomplete(*copy.m_incomplete.get())
                                         : nullptr);

    m_status              = copy.m_status;
    m_condition           = copy.m_condition;
    m_scondition          = copy.m_scondition;
    m_retain              = copy.m_retain;
    m_stockroom           = copy.m_stockroom;
    m_alternate           = copy.m_alternate;
    m_alt_id              = copy.m_alt_id;
    m_cpart               = copy.m_cpart;
    m_lot_id              = copy.m_lot_id;
    m_reserved            = copy.m_reserved;
    m_comments            = copy.m_comments;
    m_remarks             = copy.m_remarks;
    m_quantity            = copy.m_quantity;
    m_bulk_quantity       = copy.m_bulk_quantity;
    m_tier_quantity[0]    = copy.m_tier_quantity[0];
    m_tier_quantity[1]    = copy.m_tier_quantity[1];
    m_tier_quantity[2]    = copy.m_tier_quantity[2];
    m_sale                = copy.m_sale;
    m_price               = copy.m_price;
    m_cost                = copy.m_cost;
    m_tier_price[0]       = copy.m_tier_price[0];
    m_tier_price[1]       = copy.m_tier_price[1];
    m_tier_price[2]       = copy.m_tier_price[2];
    m_weight              = copy.m_weight;
    m_markerText          = copy.m_markerText;
    m_markerColor         = copy.m_markerColor;
    m_dateAdded           = copy.m_dateAdded;
    m_dateLastSold        = copy.m_dateLastSold;

    return *this;
}

bool BrickLink::Lot::operator!=(const Lot &cmp) const
{
    return !operator==(cmp);
}

void BrickLink::Lot::setItem(const Item *i)
{
    m_item = i;

    if (m_item && m_color && m_incomplete)
        m_incomplete.reset();
}

void BrickLink::Lot::setColor(const Color *c)
{
    m_color = c;

    if (m_item && m_color && m_incomplete)
        m_incomplete.reset();
}

bool BrickLink::Lot::operator==(const Lot &cmp) const
{
    return (!m_incomplete && !cmp.m_incomplete)
            && (m_item             == cmp.m_item)
            && (m_color            == cmp.m_color)
            && (m_status           == cmp.m_status)
            && (m_condition        == cmp.m_condition)
            && (m_scondition       == cmp.m_scondition)
            && (m_retain           == cmp.m_retain)
            && (m_stockroom        == cmp.m_stockroom)
            && (m_lot_id           == cmp.m_lot_id)
            && (m_reserved         == cmp.m_reserved)
            && (m_comments         == cmp.m_comments)
            && (m_remarks          == cmp.m_remarks)
            && (m_quantity         == cmp.m_quantity)
            && (m_bulk_quantity    == cmp.m_bulk_quantity)
            && (m_tier_quantity[0] == cmp.m_tier_quantity[0])
            && (m_tier_quantity[1] == cmp.m_tier_quantity[1])
            && (m_tier_quantity[2] == cmp.m_tier_quantity[2])
            && (m_sale             == cmp.m_sale)
            && qFuzzyCompare(m_price,         cmp.m_price)
            && qFuzzyCompare(m_cost,          cmp.m_cost)
            && qFuzzyCompare(m_tier_price[0], cmp.m_tier_price[0])
            && qFuzzyCompare(m_tier_price[1], cmp.m_tier_price[1])
            && qFuzzyCompare(m_tier_price[2], cmp.m_tier_price[2])
            && qFuzzyCompare(m_weight,        cmp.m_weight)
            && (m_markerText       == cmp.m_markerText)
            && (m_markerColor      == cmp.m_markerColor)
            && (m_dateAdded        == cmp.m_dateAdded)
            && (m_dateLastSold     == cmp.m_dateLastSold);
}

BrickLink::Lot::~Lot()
{ }

bool BrickLink::Lot::mergeFrom(const Lot &from, bool useCostQtyAg)
{
    if ((&from == this) ||
        (from.isIncomplete() || isIncomplete()) ||
        (from.item() != item()) ||
        (from.color() != color()) ||
        (from.condition() != condition()) ||
        (from.subCondition() != subCondition()))
        return false;

    if (useCostQtyAg) {
        int fromQty = std::abs(from.quantity());
        int toQty = std::abs(quantity());

        if ((fromQty == 0) && (toQty == 0))
            fromQty = toQty = 1;

        setCost((cost() * toQty + from.cost() * fromQty) / (toQty + fromQty));
    } else if (!qFuzzyIsNull(from.cost()) && qFuzzyIsNull(cost())) {
        setCost(from.cost());
    }
    setQuantity(quantity() + from.quantity());

    if (!qFuzzyIsNull(from.price()) && qFuzzyIsNull(price()))
        setPrice(from.price());
    if ((from.bulkQuantity() != 1) && (bulkQuantity() == 1))
        setBulkQuantity(from.bulkQuantity());
    if ((from.sale()) && !sale())
        setSale(from.sale());

    for (int i = 0; i < 3; i++) {
        if (!qFuzzyIsNull(from.tierPrice(i)) && qFuzzyIsNull(tierPrice(i)))
            setTierPrice(i, from.tierPrice(i));
        if (from.tierQuantity(i) && !tierQuantity(i))
            setTierQuantity(i, from.tierQuantity(i));
    }

    if (!from.remarks().isEmpty() && !remarks().isEmpty() && (from.remarks() != remarks())) {
        QRegularExpression fromRe { u"\\b" % QRegularExpression::escape(from.remarks()) % u"\\b" };

        if (!fromRe.match(remarks()).hasMatch()) {
            QRegularExpression thisRe { u"\\b" % QRegularExpression::escape(remarks()) % u"\\b" };

            if (thisRe.match(from.remarks()).hasMatch())
                setRemarks(from.remarks());
            else
                setRemarks(remarks() % u" " % from.remarks());
        }
    } else if (!from.remarks().isEmpty()) {
        setRemarks(from.remarks());
    }

    if (!from.comments().isEmpty() && !comments().isEmpty() && (from.comments() != comments())) {
        QRegularExpression fromRe { u"\\b" % QRegularExpression::escape(from.comments()) % u"\\b" };

        if (!fromRe.match(comments()).hasMatch()) {
            QRegularExpression thisRe { u"\\b" % QRegularExpression::escape(comments()) % u"\\b" };

            if (thisRe.match(from.comments()).hasMatch())
                setComments(from.comments());
            else
                setComments(comments() % u" " % from.comments());
        }
    } else if (!from.comments().isEmpty()) {
        setComments(from.comments());
    }

    if (!from.reserved().isEmpty() && reserved().isEmpty())
        setReserved(from.reserved());

    //TODO: add marker or remove this completely

    if ((from.dateAdded().isValid()) && !dateAdded().isValid())
        setDateAdded(from.dateAdded());
    if ((from.dateLastSold().isValid()) && !dateLastSold().isValid())
        setDateLastSold(from.dateLastSold());

    return true;
}

void BrickLink::Lot::save(QDataStream &ds) const
{
    ds << QByteArray("II") << qint32(4)
       << QString::fromLatin1(itemId())
       << qint8(itemType() ? itemType()->id() : ItemType::InvalidId)
       << uint(color() ? color()->id() : Color::InvalidId)
       << qint8(m_status) << qint8(m_condition) << qint8(m_scondition) << qint8(m_retain ? 1 : 0)
       << qint8(m_stockroom) << m_lot_id << m_reserved << m_comments << m_remarks
       << m_quantity << m_bulk_quantity
       << m_tier_quantity[0] << m_tier_quantity[1] << m_tier_quantity[2]
       << m_sale << m_price << m_cost
       << m_tier_price[0] << m_tier_price[1] << m_tier_price[2]
       << m_weight
       << m_markerText << m_markerColor
       << m_dateAdded << m_dateLastSold;
}

BrickLink::Lot *BrickLink::Lot::restore(QDataStream &ds)
{
    std::unique_ptr<Lot> lot;

    QByteArray tag;
    qint32 version;
    ds >> tag >> version;
    if ((ds.status() != QDataStream::Ok) || (tag != "II") || (version < 2) || (version > 4))
        return nullptr;

    QString itemid;
    uint colorid = 0;
    qint8 itemtypeid = 0;

    ds >> itemid >> itemtypeid >> colorid;

    if (ds.status() != QDataStream::Ok)
        return nullptr;

    auto item = core()->item(itemtypeid, itemid.toLatin1());
    auto color = core()->color(colorid);
    std::unique_ptr<Incomplete> inc;

    if (!item || !color) {
        inc.reset(new Incomplete);
        if (!item) {
            inc->m_item_id = itemid.toLatin1();
            inc->m_itemtype_id = itemtypeid;
        }
        if (!color) {
            inc->m_color_id = colorid;
            inc->m_color_name = QString::number(colorid);
        }

        if (core()->applyChangeLog(item, color, inc.get()))
            inc.reset();
    }
    lot.reset(new Lot(color, item));
    if (inc)
        lot->setIncomplete(inc.release());

    // alternate, cpart and altid are left out on purpose!

    qint8 status = 0, cond = 0, scond = 0, retain = 0, stockroom = 0;
    ds >> status >> cond >> scond >> retain >> stockroom
            >> lot->m_lot_id >> lot->m_reserved >> lot->m_comments >> lot->m_remarks
            >> lot->m_quantity >> lot->m_bulk_quantity
            >> lot->m_tier_quantity[0] >> lot->m_tier_quantity[1] >> lot->m_tier_quantity[2]
            >> lot->m_sale >> lot->m_price >> lot->m_cost
            >> lot->m_tier_price[0] >> lot->m_tier_price[1] >> lot->m_tier_price[2]
            >> lot->m_weight;
    if (version >= 3)
        ds >> lot->m_markerText >> lot->m_markerColor;
    if (version >= 4)
        ds >> lot->m_dateAdded >> lot->m_dateLastSold;

    if (ds.status() != QDataStream::Ok)
        return nullptr;

    lot->m_status = static_cast<Status>(status);
    lot->m_condition = static_cast<Condition>(cond);
    lot->m_scondition = static_cast<SubCondition>(scond);
    lot->m_retain = (retain);
    lot->m_stockroom = static_cast<Stockroom>(stockroom);

    return lot.release();
}
