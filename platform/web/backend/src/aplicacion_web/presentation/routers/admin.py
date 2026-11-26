from typing import Literal

from fastapi import APIRouter, Query, Request, status
from pydantic import BaseModel, Field

from aplicacion_web.application.auth import AuthService
from aplicacion_web.presentation.dependencies import Admin

router = APIRouter(prefix="/api/v1/admin", tags=["administración"])


class UserCreate(BaseModel):
    username: str = Field(min_length=3, max_length=80)
    displayName: str = Field(min_length=1, max_length=120)
    password: str = Field(min_length=12, max_length=128)
    role: Literal["barista", "admin"]


class UserUpdate(BaseModel):
    displayName: str = Field(min_length=1, max_length=120)
    role: Literal["barista", "admin"]
    active: bool


def _auth(request: Request) -> AuthService:
    return request.app.state.auth_service


@router.get("/users")
def users(
    request: Request,
    _: Admin,
    page: int = Query(1, ge=1),
    page_size: int = Query(25, ge=1, le=100),
) -> dict:
    return _auth(request).list_users(page, page_size)


@router.post("/users", status_code=status.HTTP_201_CREATED)
def create_user(payload: UserCreate, request: Request, _: Admin) -> dict:
    return _auth(request).create_user(
        payload.username, payload.displayName, payload.password, payload.role
    )


@router.put("/users/{user_id}")
def update_user(payload: UserUpdate, user_id: str, request: Request, identity: Admin) -> dict:
    return _auth(request).update_user(
        user_id, payload.displayName, payload.role, payload.active, identity.subject
    )
