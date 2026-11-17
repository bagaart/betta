from pydantic import BaseModel, model_validator

from backend.models import Scanner, Zone, ScannerZoneMapping

class AppConfig(BaseModel):
    scanners: list[Scanner]
    zones: list[Zone]
    mappings: list[ScannerZoneMapping]

    @model_validator(mode="after")
    def validate_configuration(self):

        scanner_ids = [s.id for s in self.scanners]
        zone_ids = [z.id for z in self.zones]

        if len(scanner_ids) != len(set(scanner_ids)):
            raise ValueError("Duplicate scanner ID")

        if len(zone_ids) != len(set(zone_ids)):
            raise ValueError("Duplicate zone ID")

        mapped_scanners = set()
        mapped_zones = set()

        for mapping in self.mappings:

            if mapping.scanner_id not in scanner_ids:
                raise ValueError(f"Unknown scanner: {mapping.scanner_id}")

            if mapping.zone_id not in zone_ids:
                raise ValueError(f"Unknown zone: {mapping.zone_id}")

            # tmp
            if mapping.scanner_id in mapped_scanners:
                raise ValueError(
                    f"Scanner already mapped: "
                    f"{mapping.scanner_id}"
                )

            if mapping.zone_id in mapped_zones:
                raise ValueError(
                    f"Zone already mapped: "
                    f"{mapping.zone_id}"
                )

            mapped_scanners.add(mapping.scanner_id)
            mapped_zones.add(mapping.zone_id)

        return self