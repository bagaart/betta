import time

from backend.models import TagPosition

class StoredTag:
    def __init__(self, position: TagPosition):
        self.position = position
        self.last_detection = time.monotonic()
        self.lost_at: float | None = None

class TagStore:
    def __init__(
        self,
        detection_timeout: float = 3.0,
        lost_retention: float = 30,
    ):
        self.detection_timeout = detection_timeout
        self.lost_retention = lost_retention
        self._tags: dict[str, StoredTag] = {}

    def update_detection(self, position: TagPosition):

        now = time.monotonic()

        tag = self._tags.get(position.tag_id)

        if tag is None:
            tag = StoredTag(position)
            self._tags[position.tag_id] = tag
        else:
            tag.position = position

        tag.last_detection = now
        tag.lost_at = None

        tag.position.last_seen = time.time()
        tag.position.status = "active"

    def cleanup_expired(self):
        now = time.monotonic()
        to_delete =[]

        for tag_id, tag in self._tags.items():

            if tag.position.status == "active":
                elapsed = now - tag.last_detection

                if elapsed >= self.detection_timeout:
                    tag.position.status = "lost"
                    tag.lost_at = now

            elif tag.position.status == "lost":
                if tag.lost_at is not None:
                    elapsed = now - tag.lost_at
                    if elapsed >= self.lost_retention:
                        to_delete.append(tag_id)

        for tag_id in to_delete:
            del self._tags[tag_id]

    def get_tag(self, tag_id: str):
        tag = self._tags.get(tag_id)
        return tag.position if tag else None

    def get_all_tags(self):
        return [
            tag.position
            for tag in self._tags.values()
        ]
