import time

from backend.models import TagPosition, ScannerZoneMapping, Zone, DetectionPacket
from backend.positioning.base import BasePositioner

class ZonePositioner(BasePositioner):
    def __init__(
        self,
        zones: list[Zone],
        mappings: list[ScannerZoneMapping]
    ):
        self.zones = {
            zone.id: zone
            for zone in zones
        }

        self.scanner_to_zone = {
            mapping.scanner_id: mapping.zone_id
            for mapping in mappings
        }

    def process_detection(
        self,
        packet: DetectionPacket,
    ) -> TagPosition:

        zone_id = self.scanner_to_zone.get(packet.scanner_id)

        if zone_id is None:
            raise ValueError(
                f"Scanner {packet.scanner_id} is not assigned to a zone"
            )

        zone = self.zones.get(zone_id)

        if zone is None:
            raise ValueError(
                f"Zone {zone_id} does not exist"
            )

        return TagPosition(
            tag_id=packet.tag_id,
            mode="zone",
            zone_id=zone_id,
            zone_name=zone.name,
            x=packet.measurements.get("x"),
            y=packet.measurements.get("y"),
            last_seen=0.0,
            status="active",
        )