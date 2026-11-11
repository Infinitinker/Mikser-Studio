"""Answers every failed request in the Mikser Studio API error format: {"error": {"code", "message"}}."""

import logging
from collections.abc import Awaitable, Callable

from aiohttp import web

logger = logging.getLogger(__name__)

type Handler = Callable[[web.Request], Awaitable[web.StreamResponse]]


class ApiError(Exception):
    def __init__(self, status: int, code: str, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.code = code
        self.message = message


def error_response(status: int, code: str, message: str) -> web.Response:
    return web.json_response({"error": {"code": code, "message": message}}, status=status)


@web.middleware
async def json_errors(request: web.Request, handler: Handler) -> web.StreamResponse:
    try:
        return await handler(request)
    except ApiError as error:
        return error_response(error.status, error.code, error.message)
    except web.HTTPNotFound:
        return error_response(404, "not_found", f"There is no route {request.method} {request.path}.")
    except web.HTTPMethodNotAllowed as error:
        allowed = ", ".join(sorted(error.allowed_methods))
        return error_response(
            405, "method_not_allowed", f"{request.path} does not take {request.method}; it takes {allowed}."
        )
    except web.HTTPException as error:
        if error.status < 400:
            raise
        return error_response(error.status, "http_error", error.reason)
    except Exception as error:
        logger.exception("%s %s failed", request.method, request.path)
        return error_response(
            500, "internal", f"The AI service hit an unexpected error ({error}). See its log for details."
        )
