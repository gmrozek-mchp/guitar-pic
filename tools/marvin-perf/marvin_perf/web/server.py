"""FastAPI app composition + uvicorn entry. Imported lazily by cli.py."""

from __future__ import annotations

from pathlib import Path

from fastapi import FastAPI
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from . import api


STATIC_DIR = Path(__file__).parent / "static"

# The UI assets must revalidate on every load. Starlette sends ETag and
# Last-Modified but no Cache-Control, which leaves the browser free to reuse a
# cached app.js on heuristic freshness alone — so an edited front-end serves
# stale silently, surviving a reload and a server restart. "no-cache" means
# revalidate, not don't-store: the ETag still answers 304, so this costs a
# conditional request, not a re-download. Capture pixels are the opposite case
# and stay immutable-cached on their own /api route.
_REVALIDATE = "no-cache"


class _RevalidatingStatic(StaticFiles):
    """StaticFiles that pins Cache-Control so edits can't be missed."""

    def file_response(self, *args, **kwargs):  # type: ignore[no-untyped-def]
        response = super().file_response(*args, **kwargs)
        response.headers["Cache-Control"] = _REVALIDATE
        return response


def build_app() -> FastAPI:
    app = FastAPI(
        title="marvin-perf viewer",
        description="Visual review for marvin firmware perf-log captures.",
        version="0.1.0",
    )
    app.include_router(api.router)

    if STATIC_DIR.exists():
        app.mount("/static", _RevalidatingStatic(directory=STATIC_DIR), name="static")

        @app.get("/")
        def root_index() -> FileResponse:
            return FileResponse(
                STATIC_DIR / "index.html",
                headers={"Cache-Control": _REVALIDATE},
            )

    return app


def run(*, host: str = "127.0.0.1", port: int = 8765, capture: str | None = None) -> int:
    import uvicorn

    if capture:
        api._preload(capture)

    uvicorn.run(build_app(), host=host, port=port, log_level="info")
    return 0
