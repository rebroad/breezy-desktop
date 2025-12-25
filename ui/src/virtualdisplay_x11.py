#!/usr/bin/env python3
"""
Virtual Display Implementation for X11/Xorg

This is the X11-specific implementation of virtual display creation.
It uses the Xorg modesetting driver's virtual XR connector API (XR-Manager)
to create virtual displays.
"""

import argparse
import logging
import signal
import sys
import time
from pathlib import Path

# Add parent directory to path so we can import breezydesktop.x11
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

try:
    from breezydesktop.x11 import X11Backend
except ImportError:
    # Fallback for development
    sys.path.insert(0, str(Path(__file__).parent.parent.parent / 'x11' / 'src'))
    from x11_backend import X11Backend

logger = logging.getLogger('breezy_x11_virtualdisplay')

class VirtualDisplayX11:
    """Virtual display implementation for Xorg-based desktops using XR-Manager."""

    def __init__(self, width, height, framerate, on_closed_cb, output_name: str = "XR-0"):
        """
        Args:
            width: desired mode width in pixels
            height: desired mode height in pixels
            framerate: refresh rate in Hz
            on_closed_cb: callback invoked when the display is torn down
            output_name: optional XRandR output name (default: "XR-0")
        """
        self.width = width
        self.height = height
        self.framerate = framerate
        self.on_closed_cb = on_closed_cb
        self.output_name = output_name
        self.backend = X11Backend()
        self.running = True

    def create(self):
        """Create the virtual display using XR-Manager."""
        if not self.backend.is_available():
            logger.error("XR-Manager not available. Cannot create virtual display.")
            logger.error("Please ensure you are running Xorg (not Xwayland) with the modesetting driver that includes virtual XR connector support.")
            return False

        try:
            display_id = self.backend.create_virtual_display(
                self.width,
                self.height,
                self.framerate,
                name=self.output_name
            )

            if display_id:
                logger.info(f"Virtual display created: {display_id} ({self.width}x{self.height}@{self.framerate}Hz)")
                return True
            else:
                logger.error("Failed to create virtual display")
                return False

        except Exception as e:
            logger.error(f"Error creating virtual display: {e}")
            return False

    def terminate(self):
        """Clean up and terminate the virtual display."""
        if not self.running:
            return

        self.running = False

        try:
            if self.backend.is_available():
                # Remove the virtual display
                if self.backend.remove_virtual_display(self.output_name):
                    logger.info(f"Virtual display {self.output_name} removed")
                else:
                    logger.warning(f"Failed to remove virtual display {self.output_name}")
        except Exception as e:
            logger.warning(f"Error removing virtual display: {e}")

        if self.on_closed_cb:
            self.on_closed_cb()

def graceful_shutdown(signum, frame):
    """Handle shutdown signals."""
    global virtual_display_instance
    if virtual_display_instance:
        virtual_display_instance.terminate()
    sys.exit(0)

def _on_display_closed():
    """Callback when display is closed."""
    sys.exit(0)

def is_available():
    """Check if X11 virtual display support is available."""
    try:
        backend = X11Backend()
        return backend.is_available()
    except Exception as e:
        logger.warning(f"Failed to check X11 backend availability: {e}")
        return False

if __name__ == "__main__":
    # Set up logging
    log_dir = Path.home() / '.local' / 'state' / 'breezy_x11' / 'logs'
    log_dir.mkdir(parents=True, exist_ok=True)

    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
        handlers=[
            logging.FileHandler(log_dir / 'virtualdisplay.log'),
            logging.StreamHandler()
        ]
    )

    parser = argparse.ArgumentParser(description="X11 Virtual Display (XR-Manager)")
    parser.add_argument("--width", type=int, required=True, help="Display width in pixels")
    parser.add_argument("--height", type=int, required=True, help="Display height in pixels")
    parser.add_argument("--framerate", type=int, default=60, help="Display framerate in Hz")
    parser.add_argument("--output", type=str, default="XR-0", help="Virtual output name (default: XR-0)")
    args = parser.parse_args()

    signal.signal(signal.SIGTERM, graceful_shutdown)
    signal.signal(signal.SIGINT, graceful_shutdown)

    global virtual_display_instance
    virtual_display_instance = VirtualDisplayX11(
        args.width,
        args.height,
        args.framerate,
        _on_display_closed,
        output_name=args.output
    )

    try:
        if virtual_display_instance.create():
            # Keep running until terminated
            while virtual_display_instance.running:
                time.sleep(1)
        else:
            logger.error("Failed to create virtual display")
            sys.exit(1)
    except KeyboardInterrupt:
        virtual_display_instance.terminate()
    except Exception as e:
        logger.error(f"Error: {e}")
        virtual_display_instance.terminate()
        sys.exit(1)

