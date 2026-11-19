from typing import Literal

from fastapi import APIRouter, Request, Response
from pydantic import BaseModel, Field

from aplicacion_web.application.auth import AuthService
from aplicacion_web.domain.errors import AuthenticationError

router = APIRouter(prefix="/api/v1/auth", tags=["sesión"])


class LoginRequest(BaseModel):
    username: str = Field(min_length=1, max_length=80)
    password: str = Field(min_length=8, max_length=128)


class SessionUser(BaseModel):
    id: str
    username: str
    displayName: str
    role: Literal["barista", "admin"]


def _auth(request: Request) -> AuthService:
    return request.app.state.auth_service


@router.post("/login", response_model=SessionUser)
def login(payload: LoginRequest, request: Request, response: Response) -> dict:
    result = _auth(request).login(payload.username, payload.password)
    settings = request.app.state.settings
    response.set_cookie(
        settings.session_cookie_name,
        result.token,
        max_age=settings.session_hours * 3600,
        httponly=True,
        secure=settings.environment == "production",
        samesite="strict",
        path="/",
    )
    request.state.identity = result.user.as_identity()
    return result.user.as_dict()


@router.get("/session", response_model=SessionUser)
def session(request: Request) -> dict:
    token = request.cookies.get(request.app.state.settings.session_cookie_name)
    return _auth(request).current_user(token).as_dict()


@router.post("/logout", status_code=204)
def logout(
    request: Request,
    response: Response,
) -> None:
    token = request.cookies.get(request.app.state.settings.session_cookie_name)
    user = None
    if token:
        try:
            user = _auth(request).current_user(token)
        except AuthenticationError:
            user = None
    _auth(request).logout(token)
    if user:
        request.state.identity = user.as_identity()
    response.delete_cookie(
        request.app.state.settings.session_cookie_name,
        path="/",
        secure=request.app.state.settings.environment == "production",
        httponly=True,
        samesite="strict",
    )
