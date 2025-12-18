"""
IMUReader skeleton for Breezy XFCE4 renderer.

Phase 1:
- Provide a minimal interface for the render thread to query the latest IMU
  sample, without blocking.
- The initial implementation returns a placeholder sample with identity
  orientation and zero position so that the rest of the renderer can be
  developed and tested.

Future work:
- Parse the binary layout written by XRLinuxDriver to
  `/dev/shm/breezy_desktop_imu` (see breezy-desktop GNOME devicedatastream
  implementation and XRLinuxDriver's breezy_desktop plugin).
"""

class IMUReader:
    """IMU reader stub.

    The real implementation will read and parse `/dev/shm/breezy_desktop_imu`.
    Until that is implemented, this class intentionally returns no data
    rather than synthesising fake samples.
    """

    def __init__(self) -> None:
        self._last_sample = None

    def read_latest(self):
        """Return the latest IMU sample, or None if not yet implemented."""
        return self._last_sample


