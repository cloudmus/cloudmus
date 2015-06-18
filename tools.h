#ifndef WEBMUSIC_TOOLS_H
#define WEBMUSIC_TOOLS_H

#include <Qt>

#if !defined(Q_VERIFY)
#  ifndef QT_NO_DEBUG
#    define Q_VERIFY(cond) Q_ASSERT(cond)
#  else
#    define Q_VERIFY(cond) ((!(cond)) ? qt_assert(#cond,__FILE__,__LINE__) : qt_noop())
#  endif
#endif

#endif


