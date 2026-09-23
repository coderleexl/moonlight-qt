#pragma once

#include <QByteArray>
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
    return QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{22}$")).match(value).hasMatch();
}

inline QString newPassword()
{
    QByteArray random(16, '\0');
    for (int i = 0; i < random.size(); i += 4) {
        const quint32 word = QRandomGenerator::system()->generate();
        for (int j = 0; j < 4; ++j)
            random[i + j] = char(word >> (8 * j));
    }
    return QString::fromLatin1(random.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

inline QString newId()
{
    return QString::number(QRandomGenerator::system()->bounded(100000000u, 1000000000u));
}
}
