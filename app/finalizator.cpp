#include "finalizator.h"

#include <exception>
#include <QDebug>

Finalizator::~Finalizator()
{
    if (!canceled_ && func_) {
        try {
            func_();
        } catch (const std::exception& e) {
            qCritical() << e.what();
        } catch (...) {
            qCritical() << "<2ff90e2d> Unknown error";
        }
    }
}

Finalizator::Finalizator(const Finalizator& src)
    : func_(src.func_)
    , canceled_(false)
{
    src.canceled_ = true;
}

void Finalizator::operator=(const Finalizator& src)
{
    func_ = src.func_;
    canceled_ = false;
    src.canceled_ = true;
}
