#ifndef DESK_CLIPBOARDPUBLICATION_H
#define DESK_CLIPBOARDPUBLICATION_H

#include <QString>

// A failed OS clipboard write must not consume an offer. Limit retries and let
// a subsequent local copy take precedence over a pending remote publication.
class ClipboardPublication {
public:
    bool begin(const QString& id, qint64 now)
    {
        if (id != m_Id) {
            m_Id = id;
            m_Attempts = 0;
            m_NextAttempt = 0;
            m_Complete = false;
        }
        if (m_Complete || m_Attempts >= 3 || now < m_NextAttempt)
            return false;
        ++m_Attempts;
        m_NextAttempt = now + 1000;
        return true;
    }
    void succeeded() { m_Complete = true; }
    void localCopy() { m_Complete = true; }
    void rejected() { m_Complete = true; }

private:
    QString m_Id;
    qint64 m_NextAttempt = 0;
    int m_Attempts = 0;
    bool m_Complete = true;
};

#endif
