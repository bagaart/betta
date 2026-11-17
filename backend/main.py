import asyncio
import json
import logging
import time
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse

from backend.config import AppConfig
from backend.models import Scanner
from backend.mqtt import MQTTAdapter
from backend.positioning.zone_positioner import ZonePositioner
from backend.services.detection_service import DetectionService
from backend.storage.tag_storage import TagStore

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s | %(levelname)s | %(name)s | %(message)s",
    force=True,
)

logger = logging.getLogger(__name__)

BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / 'data'
FRONTEND_DIR = BASE_DIR.parent / "frontend"

print("========== PATH CHECK ==========")
print("MAIN FILE:", Path(__file__).resolve())
print("FRONTEND DIR:", FRONTEND_DIR.resolve())
print("CSS FILE:", (FRONTEND_DIR / "css" / "style.css").resolve())
print("CSS EXISTS:", (FRONTEND_DIR / "css" / "style.css").exists())
print("================================")

def load_config() -> AppConfig:
    with open(DATA_DIR / 'scanners.json', encoding="utf-8") as f:
        scanners = json.load(f)

    with open(DATA_DIR / 'zones.json', encoding="utf-8") as f:
        zones = json.load(f)

    with open(DATA_DIR / 'scanner_zone_mapping.json', encoding="utf-8") as f:
        mappings = json.load(f)

    return AppConfig(
        scanners=scanners,
        zones=zones,
        mappings=mappings,
    )

async def cleanup_loop(tag_store: TagStore):
    while True:
        tag_store.cleanup_expired()
        await asyncio.sleep(0.5)

@asynccontextmanager
async def lifespan(app: FastAPI):
    config = load_config()

    positioner = ZonePositioner(
        zones=config.zones,
        mappings=config.mappings,
    )

    tag_store = TagStore()

    detection_service = DetectionService(
        positioner=positioner,
        tag_store=tag_store,
    )

    mqtt_adapter = MQTTAdapter(
        handler=detection_service.handle_detection,
    )

    app.state.config = config
    app.state.tag_store = tag_store

    mqtt_task = asyncio.create_task(mqtt_adapter.run())
    cleanup_task = asyncio.create_task(cleanup_loop(tag_store))

    logger.info("Backend started")

    try:
        yield
    finally:
        logger.info("Stopping backend")

        mqtt_task.cancel()
        cleanup_task.cancel()

        await asyncio.gather(
            mqtt_task,
            cleanup_task,
            return_exceptions=True,
        )

        logger.info("Backend stopped")


app = FastAPI(lifespan=lifespan)

@app.get("/api/scanners", response_model=list[Scanner])
async def get_scanners():
    return app.state.config.scanners

@app.get("/api/map")
async def get_map():
    return {
        "id": "main-map",
        "name": "Main map",
        "width": 20,
        "height": 10,
        "file": "/api/map/file",
    }

@app.get("/api/zones")
async def get_zones():
    return app.state.config.zones


@app.get("/api/map/file")
async def get_map_file():
    map_path = DATA_DIR / "map.svg"

    logger.info(f"Map path: {map_path}")
    logger.info(f"Map exists: {map_path.exists()}")

    return FileResponse(
        map_path,
        media_type="image/svg+xml",
    )

@app.websocket("/ws/tags")
async def tags_websocket(websocket: WebSocket):
    await websocket.accept()
    logger.info("WebSocket client connected")

    try:
        while True:
            tag_store: TagStore = app.state.tag_store

            tags = [
                tag.model_dump()
                for tag in tag_store.get_all_tags()
            ]

            message = {
                "type": "tags_update",
                "timestamp": time.time(),
                "tags": tags,
            }

            await websocket.send_json(message)
            await asyncio.sleep(0.2)

    except WebSocketDisconnect:
        logger.info("WebSocket client disconnected")


@app.get("/")
async def index():
    return FileResponse(FRONTEND_DIR / "index.html")


@app.get("/style.css")
async def style():
    return FileResponse(
        FRONTEND_DIR / "css" / "style.css",
    )


@app.get("/js/{filename}")
async def javascript(filename: str):
    return FileResponse(
        FRONTEND_DIR / "js" / filename,
    )