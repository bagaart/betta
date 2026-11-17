import pytest

from backend.models import TagPosition
from backend.storage.tag_storage import TagStore


class FakeClock:
    def __init__(self):
        self.value = 100.0

    def monotonic(self):
        return self.value

    def advance(self, seconds):
        self.value += seconds


@pytest.fixture
def clock(monkeypatch):
    import backend.storage.tag_storage as module

    fake = FakeClock()

    monkeypatch.setattr(
        module.time,
        "monotonic",
        fake.monotonic,
    )

    return fake


@pytest.fixture
def store():
    return TagStore(
        detection_timeout=3.0,
        lost_retention=30.0,
    )


def make_position(tag_id="tag-001"):
    return TagPosition(
        tag_id=tag_id,
        mode="zone",
        zone_id="zone-1",
        zone_name="Стол №1",
        x=None,
        y=None,
        last_seen=0,
        status="active",
    )


def test_add_new_tag(store, clock):
    store.update_detection(make_position())

    tag = store.get_tag("tag-001")

    assert tag is not None
    assert tag.status == "active"
    assert tag.zone_id == "zone-1"


def test_update_detection(store, clock):
    store.update_detection(make_position())

    clock.advance(2)

    store.update_detection(make_position())

    clock.advance(2)
    store.cleanup_expired()

    tag = store.get_tag("tag-001")

    assert tag.status == "active"


def test_tag_becomes_lost(store, clock):
    store.update_detection(make_position())

    clock.advance(3)
    store.cleanup_expired()

    tag = store.get_tag("tag-001")

    assert tag is not None
    assert tag.status == "lost"


def test_tag_restored_after_detection(store, clock):
    store.update_detection(make_position())

    clock.advance(3)
    store.cleanup_expired()

    assert store.get_tag("tag-001").status == "lost"

    clock.advance(5)
    store.update_detection(make_position())

    tag = store.get_tag("tag-001")

    assert tag.status == "active"


def test_lost_tag_deleted_after_retention(store, clock):
    store.update_detection(make_position())

    clock.advance(3)
    store.cleanup_expired()

    assert store.get_tag("tag-001") is not None

    clock.advance(29)
    store.cleanup_expired()

    assert store.get_tag("tag-001") is not None

    clock.advance(1)
    store.cleanup_expired()

    assert store.get_tag("tag-001") is None


def test_multiple_tags(store, clock):
    store.update_detection(make_position("tag-001"))
    store.update_detection(make_position("tag-002"))

    assert len(store.get_all_tags()) == 2

    clock.advance(3)
    store.cleanup_expired()

    assert all(
        tag.status == "lost"
        for tag in store.get_all_tags()
    )
