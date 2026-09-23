#pragma once

#include <QRandomGenerator>
#include <QRegularExpression>
#include <QString>

// The device ID is a discovery label, never an authorization credential.
namespace DeskAccess {
inline bool validId(const QString& value)
{
    return QRegularExpression(QStringLiteral("^[0-9]{9}$")).match(value).hasMatch();
}

inline bool validPassword(const QString& value)
{
    return QRegularExpression(QStringLiteral("\\A[!-~]{6,64}\\z")).match(value).hasMatch();
}

inline QString newPassword()
{
    return QString::number(QRandomGenerator::system()->bounded(100000u, 1000000u));
}

inline QString newId()
{
    return QString::number(QRandomGenerator::system()->bounded(100000000u, 1000000000u));
}
}
