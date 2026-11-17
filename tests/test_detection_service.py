
import pytest

from backend.models import (
    DetectionPacket,
    Zone,
    RectangleShape,
    ScannerZoneMapping,
)
from backend.positioning.zone_positioner import ZonePositioner
from backend.storage.tag_storage import TagStore
from backend.services.detection_service import DetectionService


@pytest.fixture
def service():
    zones = [
        Zone(
            id="zone-1",
            name="Стол №1",
            shape=RectangleShape(
                type="rectangle",
                x=1,
                y=1,
                width=4,
                height=3,
            ),
        )
    ]

    mappings = [
        ScannerZoneMapping(
            scanner_id="scanner-1",
            zone_id="zone-1",
        )
    ]

    positioner = ZonePositioner(zones, mappings)
    tag_store = TagStore()

    return DetectionService(positioner, tag_store), tag_store


@pytest.mark.anyio
async def test_detection_is_saved(service):
    detection_service, tag_store = service

    packet = DetectionPacket(
        tag_id="tag-001",
        scanner_id="scanner-1",
        measurements={"rssi": -65},
    )

    await detection_service.handle_detection(packet)

    tag = tag_store.get_tag("tag-001")

    assert tag is not None
    assert tag.tag_id == "tag-001"
    assert tag.zone_id == "zone-1"
    assert tag.zone_name == "Стол №1"
    assert tag.status == "active"


@pytest.mark.anyio
async def test_unknown_scanner_is_ignored(service):
    detection_service, tag_store = service

    packet = DetectionPacket(
        tag_id="tag-001",
        scanner_id="unknown-scanner",
    )

    await detection_service.handle_detection(packet)

    assert tag_store.get_tag("tag-001") is None


@pytest.mark.anyio
async def test_repeated_detection_updates_tag(service):
    detection_service, tag_store = service

    packet = DetectionPacket(
        tag_id="tag-001",
        scanner_id="scanner-1",
    )

    await detection_service.handle_detection(packet)
    first_tag = tag_store.get_tag("tag-001")
    first_seen = first_tag.last_seen

    await detection_service.handle_detection(packet)
    second_tag = tag_store.get_tag("tag-001")

    assert second_tag is not None
    assert second_tag.status == "active"
    assert second_tag.zone_id == "zone-1"
    assert second_tag.last_seen >= first_seen
