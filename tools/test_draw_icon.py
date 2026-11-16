"""Tests for draw_icon.py: the drawings' shape, and the committed README icon's size."""

import draw_icon
import pytest
from PIL import Image


@pytest.mark.parametrize("simple", [False, True], ids=["detailed", "simple"])
def test_drawing_is_a_round_badge(simple: bool) -> None:
    image = draw_icon.icon(simple=simple)
    assert image.size == (draw_icon.S, draw_icon.S) == (1024, 1024)
    assert image.mode == "RGBA"
    last = draw_icon.S - 1
    for corner in ((0, 0), (last, 0), (0, last), (last, last)):
        assert alpha(image, corner) == 0, corner
    assert alpha(image, draw_icon.CENTER) == 255


def alpha(image: Image.Image, point: tuple[int, int]) -> int:
    """The alpha of one pixel of an RGBA image."""
    pixel = image.getpixel(point)
    assert isinstance(pixel, tuple)
    return int(pixel[3])


def test_committed_icon_is_256_rgba() -> None:
    with Image.open(draw_icon.README_ICON) as image:
        assert image.size == (256, 256)
        assert image.mode == "RGBA"
