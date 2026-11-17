import asyncio
import json
import logging
from collections.abc import Awaitable, Callable

from aiomqtt import Client
from pydantic import ValidationError

from backend.models import DetectionPacket

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s | %(levelname)s | %(name)s | %(message)s",
    force=True,
)

logger = logging.getLogger(__name__)

MQTT_HOST = "localhost"
MQTT_PORT = 1883
MQTT_TOPIC = "ips/scanners/+/detections"

DetectionHandler = Callable[[DetectionPacket], Awaitable[None]]

class MQTTAdapter:
    def __init__(
        self,
        handler: DetectionHandler,
        hostname: str = MQTT_HOST,
        port: int = MQTT_PORT,
    ):
        self.handler = handler
        self.hostname = hostname
        self.port = port

    async def run(self) -> None:
        while True:
            try:
                async with Client(self.hostname, port=self.port) as client:
                    logger.info("MQTT connected")

                    await client.subscribe(MQTT_TOPIC)
                    logger.info("MQTT subscribed to %s", MQTT_TOPIC)

                    async for message in client.messages:
                        await self._handle_message(message)

            except asyncio.CancelledError:
                raise
            except Exception as e:
                logger.exception(f"MQTT connection error: {e}")
                logger.info("Reconnecting in 3 seconds...")
                await asyncio.sleep(3)

    async def _handle_message(self, message) -> None:
        topic = str(message.topic)
        parts = topic.split("/")

        if (
            len(parts) != 4
            or parts[0] != "ips"
            or parts[1] != "scanners"
            or parts[3] != "detections"
            or not parts[2]
        ):
            logger.warning("Invalid MQTT topic: %s", topic)
            return

        scanner_id = parts[2]
        logger.info("MQTT message received: topic=%s", topic)

        try:
            payload = json.loads(message.payload.decode("utf-8"))
        except (UnicodeDecodeError, json.decoder.JSONDecodeError):
            logger.warning("Invalid JSON payload from %s", topic)
            return

        if not isinstance(payload, dict):
            logger.warning("Expected JSON object from %s", topic)
            return

        try:
            packet = DetectionPacket(
                scanner_id=scanner_id,
                **payload,
            )
        except (ValidationError, TypeError) as e:
            logger.warning(f"Invalid detection packet from %s: {e}", topic)
            return

        await self.handler(packet)
        logger.info(
            "Detection handled: tag=%s, scanner=%s",
            packet.tag_id,
            packet.scanner_id,
        )