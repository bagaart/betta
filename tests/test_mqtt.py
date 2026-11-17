
import asyncio
import json
from types import SimpleNamespace

from backend.mqtt import MQTTAdapter


def make_message(topic, payload):
    return SimpleNamespace(
        topic=topic,
        payload=json.dumps(payload).encode("utf-8"),
    )


def test_valid_message():
    received = []

    async def handler(packet):
        received.append(packet)

    adapter = MQTTAdapter(handler)

    message = make_message(
        "ips/scanners/scanner-1/detections",
        {
            "schema_version": 1,
            "tag_id": "tag-001",
            "measurements": {"rssi": -65},
        },
    )

    asyncio.run(adapter._handle_message(message))

    assert len(received) == 1
    assert received[0].tag_id == "tag-001"
    assert received[0].scanner_id == "scanner-1"
    assert received[0].measurements == {"rssi": -65}


def test_invalid_topic_is_ignored():
    received = []

    async def handler(packet):
        received.append(packet)

    adapter = MQTTAdapter(handler)

    message = make_message(
        "ips/scanners/scanner-1/rssi",
        {"tag_id": "tag-001"},
    )

    asyncio.run(adapter._handle_message(message))

    assert received == []


def test_invalid_json_is_ignored():
    received = []

    async def handler(packet):
        received.append(packet)

    adapter = MQTTAdapter(handler)

    message = SimpleNamespace(
        topic="ips/scanners/scanner-1/detections",
        payload=b"not json",
    )

    asyncio.run(adapter._handle_message(message))

    assert received == []


def test_invalid_packet_is_ignored():
    received = []

    async def handler(packet):
        received.append(packet)

    adapter = MQTTAdapter(handler)

    message = make_message(
        "ips/scanners/scanner-1/detections",
        {
            "schema_version": 1,
            "tag_id": "",
            "measurements": {},
        },
    )

    asyncio.run(adapter._handle_message(message))

    assert received == []
