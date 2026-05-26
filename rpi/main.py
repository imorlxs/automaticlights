"""Entry point — wires EFSM, MQTT handler, and FastAPI together."""
import asyncio
import logging

import uvicorn

import config
from efsm import EFSM
from mqtt_handler import MQTTHandler
import api

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)


async def main() -> None:
    efsm = EFSM(lux_th=config.LUX_TH, id_timeout=config.ID_TIMEOUT)
    mqtt = MQTTHandler(efsm)
    api.set_efsm(efsm)

    server = uvicorn.Server(
        uvicorn.Config(
            api.app,
            host=config.WEBAPP_HOST,
            port=config.WEBAPP_PORT,
            log_level="info",
        )
    )

    await asyncio.gather(
        mqtt.run(),
        server.serve(),
    )


if __name__ == "__main__":
    asyncio.run(main())
