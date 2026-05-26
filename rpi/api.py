"""FastAPI webapp — status display and manual ON/OFF controls."""
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from pydantic import BaseModel
from typing import Optional
import os

from efsm import EFSM

app = FastAPI(title="Automatic Lights")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

_efsm: Optional[EFSM] = None


def set_efsm(efsm: EFSM) -> None:
    global _efsm
    _efsm = efsm


# ------------------------------------------------------------------
# REST endpoints
# ------------------------------------------------------------------

@app.get("/api/state")
def get_state() -> dict:
    return _efsm.get_snapshot()


@app.post("/api/light/on")
async def manual_on() -> dict:
    await _efsm.handle_manual_on()
    return {"result": "ok", "light": "ON"}


@app.post("/api/light/off")
async def manual_off() -> dict:
    allowed = await _efsm.handle_manual_off()
    if not allowed:
        raise HTTPException(
            status_code=403,
            detail="Cannot turn off: a known user is still in the room",
        )
    return {"result": "ok", "light": "OFF"}


class UserDetectedEvent(BaseModel):
    user: str
    detected: bool = True
    rssi: Optional[float] = None


@app.post("/api/user/detected")
async def user_detected(body: UserDetectedEvent) -> dict:
    """Called by the Android app when BLE beacon proximity changes."""
    await _efsm.handle_user_detected(body.user, body.detected)
    return {"result": "ok"}


class UserConfig(BaseModel):
    name: str


_configured_users: list[str] = []


@app.post("/api/user/configure")
def configure_user(body: UserConfig) -> dict:
    """Register a user name for welcome messages (persists in memory)."""
    if body.name not in _configured_users:
        _configured_users.append(body.name)
    return {"result": "ok", "users": _configured_users}


@app.get("/api/users")
def list_users() -> dict:
    return {"users": _configured_users}


# ------------------------------------------------------------------
# Serve static UI if the static/ directory exists
# ------------------------------------------------------------------

_static_dir = os.path.join(os.path.dirname(__file__), "static")
if os.path.isdir(_static_dir):
    app.mount("/", StaticFiles(directory=_static_dir, html=True), name="static")
