class DomainError(Exception):
    """Error esperado y comunicable del dominio."""

    code = "domain_error"


class NotFoundError(DomainError):
    code = "not_found"


class ConflictError(DomainError):
    code = "conflict"


class ValidationError(DomainError):
    code = "validation_error"


class ForbiddenError(DomainError):
    code = "forbidden"


class AuthenticationError(DomainError):
    code = "unauthorized"
