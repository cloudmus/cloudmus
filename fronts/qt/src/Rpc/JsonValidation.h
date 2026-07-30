#pragma once

#include <initializer_list>
#include <optional>
#include <string>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QList>
#include <QMap>
#include <QString>

#include "ProtocolParseError.h"

// Hand-written helpers generated/Models.h calls into (qualified as
// Rpc::requiredField<T> etc. — see protocol/codegen/cpprender.py) — one line
// per field instead of an inlined presence/type-check block. See
// protocol/README.md.
//
// JsonField<T> is the per-type plumbing (type-check a QJsonValue, extract a
// T from it, convert a T back to QJsonValue). The primary template covers
// every generated struct — anything with a static T::fromJson(QJsonObject)
// and a T::toJson() const — since Models.h's structs already provide both;
// primitives get explicit specializations below.

namespace Rpc {

template <typename T>
struct JsonField {
    static bool check(const QJsonValue& v) { return v.isObject(); }
    static T extract(const QJsonValue& v) { return T::fromJson(v.toObject()); }
    static QJsonValue toJson(const T& v) { return v.toJson(); }
    static const char* typeName() { return "object"; }
};

template <>
struct JsonField<QString> {
    static bool check(const QJsonValue& v) { return v.isString(); }
    static QString extract(const QJsonValue& v) { return v.toString(); }
    static QJsonValue toJson(const QString& v) { return v; }
    static const char* typeName() { return "string"; }
};

template <>
struct JsonField<int> {
    static bool check(const QJsonValue& v) { return v.isDouble(); }
    static int extract(const QJsonValue& v) { return v.toInt(); }
    static QJsonValue toJson(int v) { return v; }
    static const char* typeName() { return "integer"; }
};

template <>
struct JsonField<double> {
    static bool check(const QJsonValue& v) { return v.isDouble(); }
    static double extract(const QJsonValue& v) { return v.toDouble(); }
    static QJsonValue toJson(double v) { return v; }
    static const char* typeName() { return "number"; }
};

template <>
struct JsonField<bool> {
    static bool check(const QJsonValue& v) { return v.isBool(); }
    static bool extract(const QJsonValue& v) { return v.toBool(); }
    static QJsonValue toJson(bool v) { return v; }
    static const char* typeName() { return "boolean"; }
};

// The "any" case (e.g. ErrorParams.data) — no type check, passthrough.
template <>
struct JsonField<QJsonValue> {
    static bool check(const QJsonValue&) { return true; }
    static QJsonValue extract(const QJsonValue& v) { return v; }
    static QJsonValue toJson(const QJsonValue& v) { return v; }
    static const char* typeName() { return "any"; }
};

namespace Detail {
inline std::string fieldPath(const char* className, const char* fieldName)
{
    return std::string(className) + "." + fieldName;
}
} // namespace Detail

// --- fromJson helpers ---

template <typename T>
T requiredField(const QJsonObject& obj, const char* className, const char* fieldName)
{
    if (!obj.contains(fieldName)) {
        throw ProtocolParseError(Detail::fieldPath(className, fieldName) + " is required");
    }
    const QJsonValue v = obj.value(fieldName);
    if (!JsonField<T>::check(v)) {
        throw ProtocolParseError(Detail::fieldPath(className, fieldName) + ": expected " + JsonField<T>::typeName());
    }
    return JsonField<T>::extract(v);
}

template <typename T>
std::optional<T> optionalField(const QJsonObject& obj, const char* className, const char* fieldName)
{
    if (!obj.contains(fieldName) || obj.value(fieldName).isNull()) {
        return std::nullopt;
    }
    const QJsonValue v = obj.value(fieldName);
    if (!JsonField<T>::check(v)) {
        throw ProtocolParseError(Detail::fieldPath(className, fieldName) + ": expected " + JsonField<T>::typeName());
    }
    return JsonField<T>::extract(v);
}

inline void checkEnumValue(const QString& value, const char* className, const char* fieldName,
                           std::initializer_list<const char*> allowed)
{
    for (const char* a : allowed) {
        if (value == QLatin1String(a))
            return;
    }
    throw ProtocolParseError(Detail::fieldPath(className, fieldName) + ": value is not one of the allowed enum values");
}

inline QString requiredEnumField(const QJsonObject& obj, const char* className, const char* fieldName,
                                 std::initializer_list<const char*> allowed)
{
    QString value = requiredField<QString>(obj, className, fieldName);
    checkEnumValue(value, className, fieldName, allowed);
    return value;
}

inline std::optional<QString> optionalEnumField(const QJsonObject& obj, const char* className, const char* fieldName,
                                                std::initializer_list<const char*> allowed)
{
    std::optional<QString> value = optionalField<QString>(obj, className, fieldName);
    if (value.has_value()) {
        checkEnumValue(*value, className, fieldName, allowed);
    }
    return value;
}

template <typename T>
QList<T> requiredArray(const QJsonObject& obj, const char* className, const char* fieldName)
{
    if (!obj.contains(fieldName)) {
        throw ProtocolParseError(Detail::fieldPath(className, fieldName) + " is required");
    }
    const QJsonValue v = obj.value(fieldName);
    if (!v.isArray()) {
        throw ProtocolParseError(Detail::fieldPath(className, fieldName) + ": expected array");
    }
    QJsonArray arr = v.toArray();
    QList<T> out;
    out.reserve(arr.size());
    for (const QJsonValue& item : arr) {
        if (!JsonField<T>::check(item)) {
            throw ProtocolParseError(Detail::fieldPath(className, fieldName) + "[]: expected "
                                     + JsonField<T>::typeName());
        }
        out.push_back(JsonField<T>::extract(item));
    }
    return out;
}

template <typename T>
QMap<QString, T> requiredMap(const QJsonObject& obj, const char* className, const char* fieldName)
{
    if (!obj.contains(fieldName)) {
        throw ProtocolParseError(Detail::fieldPath(className, fieldName) + " is required");
    }
    const QJsonValue v = obj.value(fieldName);
    if (!v.isObject()) {
        throw ProtocolParseError(Detail::fieldPath(className, fieldName) + ": expected object");
    }
    QJsonObject sub = v.toObject();
    QMap<QString, T> out;
    for (auto it = sub.constBegin(); it != sub.constEnd(); ++it) {
        if (!JsonField<T>::check(it.value())) {
            throw ProtocolParseError(Detail::fieldPath(className, fieldName) + "{}: expected "
                                     + JsonField<T>::typeName());
        }
        out.insert(it.key(), JsonField<T>::extract(it.value()));
    }
    return out;
}

template <typename T>
std::optional<QMap<QString, T>> optionalMap(const QJsonObject& obj, const char* className, const char* fieldName)
{
    if (!obj.contains(fieldName) || obj.value(fieldName).isNull()) {
        return std::nullopt;
    }
    return requiredMap<T>(obj, className, fieldName);
}

// --- toJson helpers ---

template <typename T>
QJsonValue toJsonValue(const T& v)
{
    return JsonField<T>::toJson(v);
}

template <typename T>
QJsonArray toJsonArray(const QList<T>& values)
{
    QJsonArray arr;
    for (const auto& v : values) {
        arr.append(JsonField<T>::toJson(v));
    }
    return arr;
}

template <typename T>
QJsonObject toJsonMap(const QMap<QString, T>& values)
{
    QJsonObject obj;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        obj.insert(it.key(), JsonField<T>::toJson(it.value()));
    }
    return obj;
}

template <typename T>
void insertOptional(QJsonObject& obj, const char* key, const std::optional<T>& value)
{
    if (value.has_value()) {
        obj.insert(key, JsonField<T>::toJson(*value));
    }
}

template <typename T>
void insertOptionalMap(QJsonObject& obj, const char* key, const std::optional<QMap<QString, T>>& value)
{
    if (value.has_value()) {
        obj.insert(key, toJsonMap(*value));
    }
}

} // namespace Rpc
