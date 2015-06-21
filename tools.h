#ifndef WEBMUSIC_TOOLS_H
#define WEBMUSIC_TOOLS_H

#include <functional>

#include <Qt>
#include <QObject>

#if !defined(Q_VERIFY)
#  ifndef QT_NO_DEBUG
#    define Q_VERIFY(cond) Q_ASSERT(cond)
#  else
#    define Q_VERIFY(cond) ((!(cond)) ? qt_assert(#cond,__FILE__,__LINE__) : qt_noop())
#  endif
#endif


namespace detail
{

class connect_simple_helper : public QObject
{
    Q_OBJECT
public:
    connect_simple_helper(QObject* parent, const std::function<void()>& f)
        : QObject(parent), f_(f), del_(false) {}

public Q_SLOTS:
    void signaled() { f_(); if (del_) deleteLater(); }


public:
    std::function<void()> f_;
    bool del_;
};

}

template <class T>
bool connect(QObject* sender, const char* signal, const T& reciever, Qt::ConnectionType type = Qt::AutoConnection)
{
    return QObject::connect(sender, signal, new detail::connect_simple_helper(sender, reciever), SLOT(signaled()), type);
}




#endif


