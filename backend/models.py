from typing import Literal

from pydantic import BaseModel, Field, ConfigDict

class Position(BaseModel):
    x: float
    y: float

class Scanner(BaseModel):
    id: str
    name: str
    position: Position
    range: float = Field(gt=0)

class RectangleShape(BaseModel):
    type: Literal["rectangle"] = 'rectangle'

    x: float
    y: float
    width: float = Field(gt=0)
    height: float = Field(gt=0)

class Zone(BaseModel):
    id: str
    name: str
    shape: RectangleShape

class ScannerZoneMapping(BaseModel):
    scanner_id: str
    zone_id: str

class TagPosition(BaseModel):
    tag_id: str
    mode: Literal["zone", "coordinates"]

    zone_id: str | None = None
    zone_name: str | None = None

    x: float | None = None
    y: float | None = None

    last_seen: float
    status: Literal["active", "lost"]

class DetectionPacket(BaseModel):

    schema_version: Literal[1] = 1

    tag_id: str = Field(min_length=1)
    scanner_id: str = Field(min_length=1)

    timestamp: float | None = None

    measurements: dict[str, float] = Field(default_factory=dict)