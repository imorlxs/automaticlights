"""
Extended Finite State Machine for the Automatic Lights coordinator.

States: IDLE → DOOR_OPEN_CHECK → OCCUPIED ↔ VACATING → IDLE

Presence is derived from the number of tracked users (N > 0).
The mmWave sensor is not used.
"""
import asyncio
import logging
from enum import Enum
from typing import Callable, Awaitable, Optional

logger = logging.getLogger(__name__)

Callback = Callable[..., Awaitable[None]]


class State(Enum):
    IDLE = "IDLE"
    DOOR_OPEN_CHECK = "DOOR_OPEN_CHECK"
    OCCUPIED = "OCCUPIED"
    VACATING = "VACATING"


class EFSM:
    def __init__(self, lux_th: float, id_timeout: float) -> None:
        self._lux_th = lux_th
        self._id_timeout = id_timeout

        self.state = State.IDLE
        self.light_on = False
        self.door_open = False
        #self.presence = False
        self.lux = 0.0
        self.users_inside: dict[str, bool] = {}
        self.users_last_seen: dict[str, float] = {}
        self.last_user: Optional[str] = None
        self.last_welcome: Optional[str] = None
        self.last_welcome_ts: float = 0.0

        self._timer: Optional[asyncio.Task] = None

        self.on_light_cmd: Optional[Callback] = None
        self.on_welcome: Optional[Callback] = None
        self.on_state_update: Optional[Callback] = None

    # ------------------------------------------------------------------
    # Public properties
    # ------------------------------------------------------------------

    @property
    def N(self) -> int:
        return sum(1 for inside in self.users_inside.values() if inside)

    @property
    def presence(self) -> bool:
        """Presence is true whenever at least one known user is inside."""
        return self.N > 0

    # ------------------------------------------------------------------
    # Event handlers
    # ------------------------------------------------------------------

    async def handle_door(self, open: bool) -> None:
        self.door_open = open
        logger.info("Door %s | state=%s", "OPEN" if open else "CLOSED", self.state.value)

        if open:
            if self.state == State.IDLE:
                self.state = State.DOOR_OPEN_CHECK
                self._start_id_timer()
            elif self.state == State.OCCUPIED:
                self.state = State.VACATING
        else:
            if self.state == State.DOOR_OPEN_CHECK:
                self._cancel_timer()
                self.state = State.IDLE
            elif self.state == State.VACATING:
                if self.N == 0: #and not self .presence: # presence is derived from N, because sensor broken
                    await self._turn_light(False)
                    self.state = State.IDLE
                else:
                    self.state = State.OCCUPIED

        await self._emit_state()

    async def handle_presence(self, present: bool) -> None:
        self.presence = present
        logger.info("Presence %s | state=%s N=%d", present, self.state.value, self.N)

        if self.state == State.DOOR_OPEN_CHECK and present:
            # Presence arrived — check if we already have a user identified
            if self.N > 0:
                await self._confirm_entry()
        elif self.state == State.VACATING and not present and not self.door_open and self.N == 0:
            await self._turn_light(False)
            self.state = State.IDLE

        await self._emit_state()

    async def handle_lux(self, lux: float) -> None:
        self.lux = lux
        if self.state == State.OCCUPIED and not self.light_on and lux <= self._lux_th:
            logger.info("Lux %.1f <= %.1f while occupied — turning light on", lux, self._lux_th)
            await self._turn_light(True)
        await self._emit_state()

    async def handle_user_detected(self, user: str, detected: bool) -> None:
        self.users_inside[user] = detected
        logger.info("User '%s' detected=%s | state=%s N=%d", user, detected, self.state.value, self.N)

        if detected:
            import time
            self.users_last_seen[user] = time.time()
            self.last_user = user
            if self.state == State.DOOR_OPEN_CHECK: # and self.presence:
                await self._confirm_entry()
            elif self.state in (State.IDLE, State.OCCUPIED, State.VACATING):
                # User detected outside the normal door-open flow (door sensor
                # missed the event, or BLE reported late) — still turn on.
                if self.state != State.OCCUPIED:
                    self.state = State.OCCUPIED
                if not self.light_on and self.lux <= self._lux_th:
                    await self._turn_light(True)
        else:
            if self.state in (State.VACATING, State.OCCUPIED) and self.N == 0 and not self.door_open:
                await self._turn_light(False)
                self.state = State.IDLE

        await self._emit_state()

    async def check_user_timeouts(self, timeout: float) -> None:
        """Called periodically — marks users absent if no heartbeat received within timeout seconds."""
        import time
        now = time.time()
        for user, last_seen in list(self.users_last_seen.items()):
            if self.users_inside.get(user) and (now - last_seen) > timeout:
                logger.warning("User '%s' timed out (no heartbeat for %.0fs) — marking absent", user, now - last_seen)
                await self.handle_user_detected(user, False)

    async def handle_manual_on(self) -> None:
        logger.info("Manual ON | state=%s", self.state.value)
        await self._turn_light(True)
        await self._emit_state()

    async def handle_manual_off(self) -> bool:
        """Returns True if the command was accepted, False if blocked."""
        if self.N > 0: #or self.presence:
            logger.warning("Manual OFF blocked: N=%d presence=%s", self.N, self.presence)
            return False
        logger.info("Manual OFF | state=%s", self.state.value)
        await self._turn_light(False)
        await self._emit_state()
        return True

    # ------------------------------------------------------------------
    # State snapshot
    # ------------------------------------------------------------------

    def get_snapshot(self) -> dict:
        return {
            "fsm_state": self.state.value,
            "light": "ON" if self.light_on else "OFF",
            "N": self.N,
            "door": "OPEN" if self.door_open else "CLOSED",
            "presence": "PRESENT" if self.presence else "ABSENT",
            "lux": self.lux,
            "lastUser": self.last_user,
            "lastWelcome": self.last_welcome,
            "lastWelcomeTs": self.last_welcome_ts,
        }

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    async def _confirm_entry(self) -> None:
        import time
        self._cancel_timer()
        self.state = State.OCCUPIED
        if self.lux <= self._lux_th:
            await self._turn_light(True)
        if self.last_user:
            self.last_welcome = self.last_user
            self.last_welcome_ts = time.time()
            if self.on_welcome:
                await self.on_welcome(self.last_user)

    async def _turn_light(self, on: bool) -> None:
        self.light_on = on
        logger.info("Light → %s", "ON" if on else "OFF")
        if self.on_light_cmd:
            await self.on_light_cmd(on)

    async def _emit_state(self) -> None:
        if self.on_state_update:
            await self.on_state_update()

    def _start_id_timer(self) -> None:
        self._cancel_timer()
        self._timer = asyncio.create_task(self._id_timeout_task())

    def _cancel_timer(self) -> None:
        if self._timer and not self._timer.done():
            self._timer.cancel()
        self._timer = None

    async def _id_timeout_task(self) -> None:
        try:
            await asyncio.sleep(self._id_timeout)
            if self.state == State.DOOR_OPEN_CHECK:
                logger.info("ID timeout — no user identified, back to IDLE")
                self.state = State.IDLE
                await self._emit_state()
        except asyncio.CancelledError:
            pass
