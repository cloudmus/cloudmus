#include "options.h"

#include <cassert>

namespace
{

static Options* globalInstance = 0;

}

Options::Options(QObject* parent) : QObject(parent)
{
    assert(!globalInstance);
    globalInstance = this;
}

Options::~Options()
{
    globalInstance = nullptr;
}

Options* Options::instance()
{
    return globalInstance;
}

