"""MQTT client — bridges the broker to the EFSM."""
import json
import logging
import asyncio
import aiomqtt

from efsm import EFSM
import config

logger = logging.getLogger(__name__)

SUBSCRIPTIONS = [
    config.TOPIC_DOOR,
    config.TOPIC_PRESENCE,
    config.TOPIC_LUX,
    config.TOPIC_LIGHT_STATE,
    config.TOPIC_USER_DETECTED,
]


class MQTTHandler:
    def __init__(self, efsm: EFSM) -> None:
        self._efsm = efsm
        self._client: aiomqtt.Client | None = None

    # ------------------------------------------------------------------
    # Callbacks wired into EFSM
    # ------------------------------------------------------------------

    async def publish_light_cmd(self, on: bool) -> None:
        await self._publish(config.TOPIC_LIGHT_CMD, "ON" if on else "OFF")

    async def publish_welcome(self, user: str) -> None:
        await self._publish(config.TOPIC_WELCOME, f"Welcome, {user}!")

    async def publish_state(self) -> None:
        snapshot = self._efsm.get_snapshot()
        await self._publish(config.TOPIC_STATE, json.dumps(snapshot))

    # ------------------------------------------------------------------
    # Main loop
    # ------------------------------------------------------------------

    async def run(self) -> None:
        self._efsm.on_light_cmd = self.publish_light_cmd
        self._efsm.on_welcome = self.publish_welcome
        self._efsm.on_state_update = self.publish_state

        reconnect_delay = 2
        while True:
            try:
                async with aiomqtt.Client(config.MQTT_BROKER, port=config.MQTT_PORT) as client:
                    self._client = client
                    reconnect_delay = 2
                    logger.info("Connected to MQTT broker at %s:%d", config.MQTT_BROKER, config.MQTT_PORT)

                    for topic in SUBSCRIPTIONS:
                        await client.subscribe(topic)

                    async for message in client.messages:
                        topic = str(message.topic)
                        payload = message.payload.decode()
                        try:
                            await self._dispatch(topic, payload)
                        except Exception:
                            logger.exception("Error handling message on %s: %r", topic, payload)
            except aiomqtt.MqttError as exc:
                self._client = None
                logger.warning("MQTT disconnected (%s), retrying in %ds…", exc, reconnect_delay)
                await asyncio.sleep(reconnect_delay)
                reconnect_delay = min(reconnect_delay * 2, 60)

    # ------------------------------------------------------------------
    # Internal
    # ------------------------------------------------------------------

    async def _publish(self, topic: str, payload: str) -> None:
        if self._client is None:
            logger.warning("Cannot publish to %s — not connected", topic)
            return
        try:
            await self._client.publish(topic, payload)
        except aiomqtt.MqttError:
            logger.exception("Publish failed on %s", topic)

    async def _dispatch(self, topic: str, payload: str) -> None:
        if topic == config.TOPIC_DOOR:
            await self._efsm.handle_door(payload.strip() == "OPEN")

        elif topic == config.TOPIC_PRESENCE:
            await self._efsm.handle_presence(payload.strip() == "PRESENT")

        elif topic == config.TOPIC_LUX:
            await self._efsm.handle_lux(float(payload.strip()))

        elif topic == config.TOPIC_USER_DETECTED:
            data = json.loads(payload)
            user = data["user"]
            detected = bool(data.get("detected", True))
            await self._efsm.handle_user_detected(user, detected)

        elif topic == config.TOPIC_LIGHT_STATE:
            logger.debug("Light state confirmed by actuator: %s", payload)
