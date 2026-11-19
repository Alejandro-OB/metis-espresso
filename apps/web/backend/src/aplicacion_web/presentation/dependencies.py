from __future__ import annotations

from typing import Annotated

from fastapi import Depends, Header, Request

from aplicacion_web.application.services import ApplicationService
from aplicacion_web.domain.errors import AuthenticationError, ForbiddenError
from aplicacion_web.domain.models import Identity


def service(request: Request) -> ApplicationService:
    return request.app.state.service


Service = Annotated[ApplicationService, Depends(service)]


def identity(
    request: Request,
    authorization: Annotated[str | None, Header()] = None,
    x_test_role: Annotated[str | None, Header()] = None,
    x_test_actor: Annotated[str | None, Header()] = None,
) -> Identity:
    settings = request.app.state.settings
    current: Identity
    if settings.environment == "test" and x_test_role:
        if x_test_role not in {"viewer", "barista", "admin"}:
            raise ForbiddenError("rol de prueba no soportado")
        current = Identity(x_test_actor or f"test-{x_test_role}", x_test_role)
    elif request.cookies.get(settings.session_cookie_name):
        current = request.app.state.auth_service.current_user(
            request.cookies[settings.session_cookie_name]
        ).as_identity()
    else:
        token = authorization.removeprefix("Bearer ") if authorization else ""
        tokens = {
            settings.viewer_token: Identity("api-viewer", "viewer"),
            settings.barista_token: Identity("api-barista", "barista"),
            settings.admin_token: Identity("api-admin", "admin"),
        }
        if token in tokens:
            current = tokens[token]
        elif settings.environment == "test":
            current = Identity("local-viewer", "viewer")
        else:
            raise AuthenticationError("Inicia sesión para continuar.")
    request.state.identity = current
    return current


IdentityDep = Annotated[Identity, Depends(identity)]


def writer(current: IdentityDep) -> Identity:
    if not current.can_write():
        raise ForbiddenError("esta operación requiere rol barista o administración")
    return current


Writer = Annotated[Identity, Depends(writer)]


def admin(current: IdentityDep) -> Identity:
    if not current.can_admin():
        raise ForbiddenError("esta operación requiere administración")
    return current


Admin = Annotated[Identity, Depends(admin)]
