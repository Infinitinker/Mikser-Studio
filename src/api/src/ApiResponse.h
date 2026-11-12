// What a local API route answers: a status, a JSON body and headers, and the shape of an error body.
#pragma once

#include <QHttpHeaders>
#include <QHttpServerResponder>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace mikserstudio::api {

using HttpStatus = QHttpServerResponder::StatusCode;

// What a route answers: always a JSON body.
struct ApiResponse {
    HttpStatus status = HttpStatus::Ok;
    QJsonDocument body;
    QHttpHeaders headers;
};

[[nodiscard]] ApiResponse jsonResponse(const QJsonObject& body, HttpStatus status = HttpStatus::Ok);
// {"error": {"code": <code>, "message": <message>}}
[[nodiscard]] QJsonObject errorBody(const QString& code, const QString& message);
[[nodiscard]] ApiResponse errorResponse(HttpStatus status, const QString& code, const QString& message);

}  // namespace mikserstudio::api
