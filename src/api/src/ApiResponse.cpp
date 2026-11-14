// Building the local API's JSON and error responses.
#include "ApiResponse.h"

using namespace Qt::StringLiterals;

namespace mikserstudio::api {

ApiResponse jsonResponse(const QJsonObject& body, HttpStatus status)
{
    return ApiResponse{status, QJsonDocument(body), {}};
}

QJsonObject errorBody(const QString& code, const QString& message)
{
    return QJsonObject{{u"error"_s, QJsonObject{{u"code"_s, code}, {u"message"_s, message}}}};
}

ApiResponse errorResponse(HttpStatus status, const QString& code, const QString& message)
{
    return jsonResponse(errorBody(code, message), status);
}

}  // namespace mikserstudio::api
