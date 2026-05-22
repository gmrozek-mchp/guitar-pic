"""FastAPI app composition + uvicorn entry. Imported lazily by cli.py."""

from __future__ import annotations

from pathlib import Path

from fastapi import FastAPI
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from . import api


STATIC_DIR = Path(__file__).parent / "static"


def build_app() -> FastAPI:
    app = FastAPI(
        title="marvin-perf viewer",
        description="Visual review for marvin firmware perf-log captures.",
        version="0.1.0",
    )
    app.include_router(api.router)

    if STATIC_DIR.exists():
        app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")

        @app.get("/")
        def root_index() -> FileResponse:
            return FileResponse(STATIC_DIR / "index.html")

    return app


def run(*, host: str = "127.0.0.1", port: int = 8765, capture: str | None = None) -> int:
    import uvicorn

    if capture:
        api._preload(capture)

    uvicorn.run(build_app(), host=host, port=port, log_level="info")
    return 0
