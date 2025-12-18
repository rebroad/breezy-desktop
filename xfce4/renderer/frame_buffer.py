"""
Thread-safe frame buffer for Breezy XFCE4 renderer.

This provides a minimal mutex-protected container for the latest frame.
The capture thread writes frames, the render thread reads the most recent
one without blocking capture.
"""

import threading
from typing import Any, Optional


class FrameBuffer:
    """Simple mutex-protected single-frame buffer."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._latest_frame: Optional[Any] = None

    def write_frame(self, frame: Any) -> None:
        """Store the latest frame, replacing any existing one."""
        with self._lock:
            self._latest_frame = frame

    def read_latest_frame(self) -> Optional[Any]:
        """
        Return the most recent frame.

        The returned object is the same reference that was stored; callers
        should treat it as immutable.
        """
        with self._lock:
            return self._latest_frame


