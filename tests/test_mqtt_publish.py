
import asyncio
import json
import aiomqtt


async def main():
    async with aiomqtt.Client("localhost", port=1883) as client:
        topic = "ips/scanners/scanner-1/detections"

        payload = {
            "schema_version": 1,
            "tag_id": "tag-test-001",
            "timestamp": 1794481200.0,
            "measurements": {
                "rssi": -65
            }
        }

        await client.publish(
            topic,
            json.dumps(payload),
        )

        print("Test MQTT message published")


if __name__ == "__main__":
    asyncio.set_event_loop_policy(
        asyncio.WindowsSelectorEventLoopPolicy()
    )
    asyncio.run(main())
