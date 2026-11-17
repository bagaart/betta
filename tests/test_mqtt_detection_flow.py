
import json
from types import SimpleNamespace

import pytest

from backend.models import Zone, RectangleShape, ScannerZoneMapping
from backend.mqtt import MQTTAdapter
from backend.positioning.zone_positioner import ZonePositioner
from backend.services.detection_service import DetectionService
from backend.storage.tag_storage import TagStore


@pytest.mark.anyio
async def test_mqtt_detection_reaches_tag_store():
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

    service = DetectionService(positioner, tag_store)
    adapter = MQTTAdapter(service.handle_detection)

    payload = {
        "schema_version": 1,
        "tag_id": "tag-001",
        "measurements": {"rssi": -65},
    }

    message = SimpleNamespace(
        topic="ips/scanners/scanner-1/detections",
        payload=json.dumps(payload).encode("utf-8"),
    )

    await adapter._handle_message(message)

    tag = tag_store.get_tag("tag-001")

    assert tag is not None
    assert tag.tag_id == "tag-001"
    assert tag.zone_id == "zone-1"
    assert tag.zone_name == "Стол №1"
    assert tag.status == "active"


@pytest.fixture
def anyio_backend():
    return "asyncio"
