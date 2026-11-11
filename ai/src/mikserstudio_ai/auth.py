"""Keeps the API to the process that started the sidecar: every route but /v1/health needs its token."""

import hmac

from aiohttp import web
from aiohttp.typedefs import Middleware

from mikserstudio_ai.errors import Handler, error_response

PUBLIC_PATHS = frozenset({"/v1/health"})


def bearer_auth(token: str) -> Middleware:
    expected = f"Bearer {token}".encode()

    @web.middleware
    async def middleware(request: web.Request, handler: Handler) -> web.StreamResponse:
        if request.path in PUBLIC_PATHS:
            return await handler(request)
        given = request.headers.get("Authorization", "").encode()
        if not hmac.compare_digest(given, expected):
            response = error_response(
                401, "unauthorized", 'Missing or wrong token. Send the header "Authorization: Bearer <token>".'
            )
            response.headers["WWW-Authenticate"] = "Bearer"
            return response
        return await handler(request)

    return middleware
