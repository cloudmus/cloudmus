#pragma once

#include <QObject>
#include <QSettings>

#define DECL_OPTION(Type, Name, DefValue) \
    struct Name##Option { \
        typedef Type ValueType; \
        static const char* name() { return #Name; } \
        static Type defValue() { return DefValue; } \
    };

class Options : public QObject
{
    Q_OBJECT
public:

    ////////////////////////////////////////////////////////////////////////////

    DECL_OPTION(bool, DontClose, true)
    DECL_OPTION(bool, StartHidden, false)

    ////////////////////////////////////////////////////////////////////////////

    explicit Options(QObject* parent = 0);
    ~Options();

    static Options* instance();

    template <typename OptionType>
    static typename OptionType::ValueType value()
    {
        return instance()->internalValue<OptionType>();
    }

    template <typename OptionType>
    static void setValue(const typename OptionType::ValueType& val)
    {
        instance()->internalSetValue<OptionType>(val);
    }

signals:
    void changed(const char* optName);

private:
    template <typename OptionType>
    void internalSetValue(const typename OptionType::ValueType& val)
    {
        QSettings s;
        s.setValue(OptionType::name(), QVariant::fromValue(val));
        emit changed(OptionType::name());
    }

    template <typename OptionType>
    typename OptionType::ValueType internalValue()
    {
        QSettings s;
        return s.value(OptionType::name(), OptionType::defValue()).value<typename OptionType::ValueType>();
    }

};
