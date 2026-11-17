from abc import ABC, abstractmethod

from backend.models import DetectionPacket, TagPosition


class BasePositioner(ABC):

    @abstractmethod
    def process_detection(
        self,
        packet: DetectionPacket,
    ) -> TagPosition:
        pass