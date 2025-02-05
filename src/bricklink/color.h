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

#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <QtGui/QColor>


namespace BrickLink {

class Color
{
public:
    static constexpr uint InvalidId = static_cast<uint>(-1);

    uint id() const           { return m_id; }
    QString name() const      { return m_name; }
    QColor color() const      { return m_color; }

    int ldrawId() const       { return m_ldraw_id; }

    enum TypeFlag {
        Solid        = 0x0001,
        Transparent  = 0x0002,
        Glitter      = 0x0004,
        Speckle      = 0x0008,
        Metallic     = 0x0010,
        Chrome       = 0x0020,
        Pearl        = 0x0040,
        Milky        = 0x0080,
        Modulex      = 0x0100,
        Satin        = 0x0200,

        Mask         = 0x03ff
    };

    Q_DECLARE_FLAGS(Type, TypeFlag)
    Type type() const          { return m_type; }

    bool isSolid() const       { return m_type & Solid; }
    bool isTransparent() const { return m_type & Transparent; }
    bool isGlitter() const     { return m_type & Glitter; }
    bool isSpeckle() const     { return m_type & Speckle; }
    bool isMetallic() const    { return m_type & Metallic; }
    bool isChrome() const      { return m_type & Chrome; }
    bool isPearl() const       { return m_type & Pearl; }
    bool isMilky() const       { return m_type & Milky; }
    bool isModulex() const     { return m_type & Modulex; }
    bool isSatin() const       { return m_type & Satin; }

    qreal popularity() const   { return m_popularity < 0 ? 0 : m_popularity; }

    static QString typeName(TypeFlag t);

    Color() = default;
    Color(std::nullptr_t) : Color() { } // for scripting only!

private:
    QString m_name;
    uint    m_id = 0;
    int     m_ldraw_id = 0;
    QColor  m_color;
    Type    m_type = {};
    float   m_popularity = 0;
    quint16 m_year_from = 0;
    quint16 m_year_to = 0;
    // 4 bytes padding here

private:
    static bool lessThan(const Color &color, uint id) { return color.m_id < id; }

    friend class Core;
    friend class TextImport;
};

} // namespace BrickLink

Q_DECLARE_METATYPE(const BrickLink::Color *)

Q_DECLARE_OPERATORS_FOR_FLAGS(BrickLink::Color::Type)
