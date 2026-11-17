import logging

from backend.models import DetectionPacket
from backend.positioning.base import BasePositioner
from backend.storage.tag_storage import TagStore

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s | %(levelname)s | %(name)s | %(message)s",
    force=True,
)

logger = logging.getLogger(__name__)

class DetectionService:
    def __init__(
        self,
        positioner: BasePositioner,
        tag_store: TagStore,
    ):
        self.positioner = positioner
        self.tag_store = tag_store

    async def handle_detection(
        self,
        packet: DetectionPacket,
    ) -> None:
        try:
            position = self.positioner.process_detection(packet)
        except ValueError as error:
            logger.warning(
                "Detection rejected: tag=%s, scanner=%s, reason=%s",
                packet.tag_id,
                packet.scanner_id,
                error,
            )
            return

        self.tag_store.update_detection(position)
        logger.debug(
            "Detection processed: tag=%s, zone=%s",
            position.tag_id,
            position.zone_id,
        )