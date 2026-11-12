// Helpers the core tests share: Catch2 printers for Qt types and a parser for JSON written in a test.
#pragma once

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <string_view>

template <> struct Catch::StringMaker<QString> {
    static std::string convert(const QString& value) { return '"' + value.toStdString() + '"'; }
};

template <> struct Catch::StringMaker<QStringList> {
    static std::string convert(const QStringList& value)
    {
        return '[' + value.join(QStringLiteral(" | ")).toStdString() + ']';
    }
};

template <> struct Catch::StringMaker<QJsonObject> {
    static std::string convert(const QJsonObject& value)
    {
        return QJsonDocument(value).toJson(QJsonDocument::Compact).toStdString();
    }
};

namespace mikserstudio::tests {

// Parses a JSON object literal written in a test; fails the test on a typo.
inline QJsonObject json(std::string_view text)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(text.data(), static_cast<qsizetype>(text.size())),
                                                           &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        FAIL("Invalid JSON in test: " << std::string(text));
    return document.object();
}

}  // namespace mikserstudio::tests
