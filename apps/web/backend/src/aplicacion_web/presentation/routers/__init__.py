from .admin import router as admin_router
from .auth import router as auth_router
from .resources import router as resources_router
from .system import router as system_router

__all__ = ["admin_router", "auth_router", "resources_router", "system_router"]
