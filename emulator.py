
import asyncio
import json
import math
import random
import time
from pathlib import Path

import aiomqtt


ROOT_DIR = Path(__file__).resolve().parent
DATA_DIR = ROOT_DIR / "backend" / "data"

MQTT_HOST = "localhost"
MQTT_PORT = 1883

UPDATE_INTERVAL = 0.5
TAGS_PER_ZONE = 2

RSSI_AT_1M = -59.0
PATH_LOSS_EXPONENT = 2.0
RSSI_NOISE = 1.5
MOVEMENT_STEP = 0.15


def load_json(filename):
    with open(DATA_DIR / filename, encoding="utf-8") as file:
        return json.load(file)


def distance_between(x1, y1, x2, y2):
    return math.hypot(x2 - x1, y2 - y1)


def calculate_rssi(distance):
    distance = max(distance, 0.1)

    rssi = (
        RSSI_AT_1M
        - 10 * PATH_LOSS_EXPONENT * math.log10(distance)
    )

    return rssi + random.uniform(-RSSI_NOISE, RSSI_NOISE)


def create_tags(zones, mappings):
    zone_by_id = {
        zone["id"]: zone
        for zone in zones
    }

    tags = []
    tag_number = 1

    for mapping in mappings:
        zone = zone_by_id[mapping["zone_id"]]
        shape = zone["shape"]

        center_x = shape["x"] + shape["width"] / 2
        center_y = shape["y"] + shape["height"] / 2

        for _ in range(TAGS_PER_ZONE):
            tags.append({
                "id": f"tag-{tag_number:03d}",
                "zone": zone,
                "x": center_x + random.uniform(-0.3, 0.3),
                "y": center_y + random.uniform(-0.3, 0.3),
            })

            tag_number += 1

    return tags


def move_tag(tag):
    shape = tag["zone"]["shape"]

    new_x = tag["x"] + random.uniform(
        -MOVEMENT_STEP,
        MOVEMENT_STEP
    )

    new_y = tag["y"] + random.uniform(
        -MOVEMENT_STEP,
        MOVEMENT_STEP
    )

    tag["x"] = max(
        shape["x"],
        min(new_x, shape["x"] + shape["width"])
    )

    tag["y"] = max(
        shape["y"],
        min(new_y, shape["y"] + shape["height"])
    )


async def main():
    scanners = load_json("scanners.json")
    zones = load_json("zones.json")
    mappings = load_json("scanner_zone_mapping.json")

    tags = create_tags(zones, mappings)

    print("Эмулятор запущен")
    print(f"Сканеров: {len(scanners)}")
    print(f"Зон: {len(zones)}")
    print(f"Меток: {len(tags)}")

    async with aiomqtt.Client(
        MQTT_HOST,
        port=MQTT_PORT
    ) as client:

        while True:
            for tag in tags:
                # Координаты используются только внутри эмулятора.
                move_tag(tag)

                # Каждый сканер независимо проверяет обнаружение метки.
                for scanner in scanners:
                    distance = distance_between(
                        scanner["position"]["x"],
                        scanner["position"]["y"],
                        tag["x"],
                        tag["y"]
                    )

                    # Сканер не обнаруживает метку за пределами радиуса.
                    if distance > scanner["range"]:
                        continue

                    rssi = round(calculate_rssi(distance), 2)

                    # В MQTT передаём только RSSI.
                    payload = {
                        "schema_version": 1,
                        "tag_id": tag["id"],
                        "timestamp": time.time(),
                        "measurements": {
                            "rssi": rssi
                        }
                    }

                    topic = (
                        f"ips/scanners/{scanner['id']}/detections"
                    )

                    await client.publish(
                        topic,
                        json.dumps(payload)
                    )

                    print(
                        f"{tag['id']} | "
                        f"{scanner['id']} | "
                        f"RSSI {rssi:.1f} dBm"
                    )

            await asyncio.sleep(UPDATE_INTERVAL)


if __name__ == "__main__":
    if hasattr(asyncio, "WindowsSelectorEventLoopPolicy"):
        asyncio.set_event_loop_policy(
            asyncio.WindowsSelectorEventLoopPolicy()
        )

    asyncio.run(main())
