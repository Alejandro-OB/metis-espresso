from __future__ import annotations

import hashlib
import secrets
from dataclasses import dataclass
from datetime import timedelta
from uuid import uuid4

from aplicacion_web.application.ports import PasswordHasherPort, Repository
from aplicacion_web.domain.errors import (
    AuthenticationError,
    ConflictError,
    NotFoundError,
    ValidationError,
)
from aplicacion_web.domain.models import UserIdentity, utc_now


@dataclass(frozen=True, slots=True)
class LoginResult:
    token: str
    user: UserIdentity


class AuthService:
    def __init__(
        self, repository: Repository, password_hasher: PasswordHasherPort, session_hours: int
    ) -> None:
        self.repository = repository
        self.password_hasher = password_hasher
        self.session_hours = session_hours
        self._dummy_password_hash = password_hasher.hash(secrets.token_urlsafe(32))

    @staticmethod
    def _token_hash(token: str) -> str:
        return hashlib.sha256(token.encode()).hexdigest()

    def ensure_user(
        self, identifier: str, username: str, display_name: str, password: str, role: str
    ) -> None:
        existing = self.repository.user_credentials(username)
        if existing is None:
            self.repository.save_user(
                identifier,
                username,
                display_name,
                self.password_hasher.hash(password),
                role,
            )

    def login(self, username: str, password: str) -> LoginResult:
        credentials = self.repository.user_credentials(username.strip())
        password_hash = credentials[1] if credentials else self._dummy_password_hash
        verified = self.password_hasher.verify(password_hash, password)
        if credentials is None or not credentials[0].active or not verified:
            raise AuthenticationError("Usuario o contraseña incorrectos.")
        token = secrets.token_urlsafe(32)
        expires_at = utc_now() + timedelta(hours=self.session_hours)
        self.repository.create_session(self._token_hash(token), credentials[0].id, expires_at)
        return LoginResult(token, credentials[0])

    def current_user(self, token: str | None) -> UserIdentity:
        user = (
            self.repository.user_for_session(self._token_hash(token), utc_now()) if token else None
        )
        if user is None:
            raise AuthenticationError("Inicia sesión para continuar.")
        return user

    def logout(self, token: str | None) -> None:
        if token:
            self.repository.revoke_session(self._token_hash(token))

    def list_users(self, page: int, page_size: int) -> dict:
        return self.repository.list_users(page, page_size).as_dict()

    def create_user(self, username: str, display_name: str, password: str, role: str) -> dict:
        normalized = username.strip()
        if self.repository.user_credentials(normalized) is not None:
            raise ConflictError("ya existe una cuenta con ese usuario")
        identifier = f"user-{uuid4().hex[:12]}"
        self.repository.save_user(
            identifier,
            normalized,
            display_name.strip(),
            self.password_hasher.hash(password),
            role,
        )
        user, _ = self.repository.user_credentials(normalized) or (None, "")
        if user is None:
            raise ValidationError("no fue posible crear la cuenta")
        return user.as_dict() | {"active": user.active}

    def update_user(
        self,
        identifier: str,
        display_name: str,
        role: str,
        active: bool,
        actor_id: str,
    ) -> dict:
        if identifier == actor_id and not active:
            raise ValidationError("no puedes desactivar tu propia cuenta")
        user = self.repository.update_user(identifier, display_name.strip(), role, active)
        if user is None:
            raise NotFoundError("la cuenta no existe")
        return user.as_dict() | {"active": user.active}
