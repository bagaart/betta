
import pytest

from backend.models import (
    DetectionPacket,
    Zone,
    RectangleShape,
    ScannerZoneMapping,
)

from backend.positioning.zone_positioner import ZonePositioner


@pytest.fixture
def zones():
    return [
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
        ),
        Zone(
            id="zone-2",
            name="Стол №2",
            shape=RectangleShape(
                type="rectangle",
                x=7,
                y=1,
                width=4,
                height=3,
            ),
        ),
    ]


@pytest.fixture
def mappings():
    return [
        ScannerZoneMapping(
            scanner_id="scanner-1",
            zone_id="zone-1",
        ),
        ScannerZoneMapping(
            scanner_id="scanner-2",
            zone_id="zone-2",
        ),
    ]


def test_detect_zone(zones, mappings):
    positioner = ZonePositioner(zones, mappings)

    packet = DetectionPacket(
        tag_id="tag-001",
        scanner_id="scanner-1",
        measurements={"rssi": -63},
    )

    result = positioner.process_detection(packet)

    assert result.tag_id == "tag-001"
    assert result.mode == "zone"
    assert result.zone_id == "zone-1"
    assert result.zone_name == "Стол №1"
    assert result.status == "active"
    assert result.x is None
    assert result.y is None


def test_second_scanner_detects_second_zone(zones, mappings):
    positioner = ZonePositioner(zones, mappings)

    packet = DetectionPacket(
        tag_id="tag-002",
        scanner_id="scanner-2",
    )

    result = positioner.process_detection(packet)

    assert result.zone_id == "zone-2"
    assert result.zone_name == "Стол №2"


def test_unknown_scanner(zones, mappings):
    positioner = ZonePositioner(zones, mappings)

    packet = DetectionPacket(
        tag_id="tag-001",
        scanner_id="unknown-scanner",
    )

    with pytest.raises(ValueError):
        positioner.process_detection(packet)
