import pytest
from pydantic import ValidationError

from backend.models import DetectionPacket


def test_create_zone_detection_packet():
    packet = DetectionPacket(
        tag_id="tag-001",
        scanner_id="scanner-1",
        measurements={"rssi": -65},
    )

    assert packet.tag_id == "tag-001"
    assert packet.scanner_id == "scanner-1"
    assert packet.timestamp is None
    assert packet.measurements == {"rssi": -65}
    assert packet.schema_version == 1


def test_create_coordinate_detection_packet():
    packet = DetectionPacket(
        tag_id="tag-001",
        scanner_id="scanner-1",
        timestamp=1794481200.0,
        measurements={
            "rssi": -65,
            "distance": 3.4,
            "angle": 42.5,
        },
    )

    assert packet.measurements["rssi"] == -65
    assert packet.measurements["distance"] == 3.4
    assert packet.measurements["angle"] == 42.5


def test_measurements_can_be_empty():
    packet = DetectionPacket(
        tag_id="tag-001",
        scanner_id="scanner-1",
    )

    assert packet.measurements == {}


def test_empty_tag_id_is_rejected():
    with pytest.raises(ValidationError):
        DetectionPacket(
            tag_id="",
            scanner_id="scanner-1",
        )


def test_empty_scanner_id_is_rejected():
    with pytest.raises(ValidationError):
        DetectionPacket(
            tag_id="tag-001",
            scanner_id="",
        )


def test_invalid_measurement_is_rejected():
    with pytest.raises(ValidationError):
        DetectionPacket(
            tag_id="tag-001",
            scanner_id="scanner-1",
            measurements={"rssi": "not-a-number"},
        )
