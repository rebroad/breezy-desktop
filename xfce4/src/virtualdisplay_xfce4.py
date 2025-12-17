#!/usr/bin/env python3
"""
Virtual Display Implementation for XFCE4

This is the XFCE4-specific implementation of virtual display creation.
It provides a compatible interface with the GNOME version but uses
X11/xrandr methods instead of Mutter APIs.
"""

import logging
import argparse
import signal
import sys
import time
import subprocess
from pathlib import Path

logger = logging.getLogger('breezy_xfce4_virtualdisplay')

class VirtualDisplayXFCE4:
    """Virtual display implementation for XFCE4 using xrandr."""
    
    def __init__(self, width, height, framerate, on_closed_cb, output_name: str | None = None):
        """
        Args:
            width: desired mode width in pixels
            height: desired mode height in pixels
            framerate: refresh rate in Hz
            on_closed_cb: callback invoked when the display is torn down
            output_name: optional XRandR output name to target (e.g. 'VIRTUAL1').
                         If None, the first output whose name contains 'VIRTUAL'
                         (case-insensitive) will be used.
        """
        self.width = width
        self.height = height
        self.framerate = framerate
        self.on_closed_cb = on_closed_cb
        self.output_name = output_name  # preferred output, e.g. 'VIRTUAL1'
        self.mode_name = None
        self.virtual_output = None
        self.running = True
        
    def _find_virtual_output(self) -> str | None:
        """Find the target output for the virtual display, preferring a configured name."""
        try:
            xrandr_output = subprocess.run(['xrandr'], capture_output=True, text=True, check=True)
        except subprocess.CalledProcessError as e:
            logger.error(f"Failed to query xrandr outputs: {e}")
            return None

        lines = xrandr_output.stdout.splitlines()

        # If a specific output name was requested, prefer that.
        if self.output_name:
            for line in lines:
                parts = line.split()
                if not parts:
                    continue
                if parts[0] == self.output_name:
                    logger.info(f"Using configured virtual output: {self.output_name}")
                    return self.output_name

        # Fallback: first output whose name contains 'VIRTUAL'
        for line in lines:
            if 'VIRTUAL' in line.upper():
                parts = line.split()
                if parts:
                    logger.info(f"Using detected virtual output: {parts[0]}")
                    return parts[0]

        return None

    def create(self):
        """Create the virtual display."""
        try:
            # Generate modeline for the requested mode (e.g. 3840x2160@60)
            modeline_cmd = ['cvt', str(self.width), str(self.height), str(self.framerate)]
            result = subprocess.run(modeline_cmd, capture_output=True, text=True, check=True)
            
            # Parse modeline
            for line in result.stdout.split('\n'):
                if 'Modeline' in line:
                    parts = line.split()
                    if len(parts) < 2:
                        continue
                    self.mode_name = parts[1].strip('"')
                    modeline = ' '.join(parts[1:])
                    break

            if not self.mode_name:
                logger.error("Could not parse modeline from cvt output.")
                return

            # Try to find a suitable virtual output
            self.virtual_output = self._find_virtual_output()
            if not self.virtual_output:
                logger.warning("No virtual output available. Virtual display creation on XFCE4/X11 is limited.")
                logger.warning("Ensure a dummy output (e.g. VIRTUAL1) is configured with the xf86-video-dummy driver.")
                return

            # Check whether the mode already exists
            try:
                xrandr_modes = subprocess.run(['xrandr'], capture_output=True, text=True, check=True).stdout
            except subprocess.CalledProcessError as e:
                logger.error(f"Failed to query xrandr for existing modes: {e}")
                return

            if self.mode_name not in xrandr_modes:
                # Create the mode if it doesn't already exist
                subprocess.run(['xrandr', '--newmode', modeline], check=True)
                logger.info(f"Created mode: {self.mode_name}")
            else:
                logger.info(f"Mode {self.mode_name} already exists, reusing it")

            # Ensure the mode is attached to the virtual output
            if self.mode_name not in xrandr_modes or self.virtual_output not in xrandr_modes:
                # We may not be able to reliably detect attachment from the raw text;
                # safest is to unconditionally attempt addmode and ignore failures.
                try:
                    subprocess.run(['xrandr', '--addmode', self.virtual_output, self.mode_name], check=True)
                    logger.info(f"Attached mode {self.mode_name} to {self.virtual_output}")
                except subprocess.CalledProcessError as e:
                    logger.warning(f"Failed to attach mode {self.mode_name} to {self.virtual_output}: {e}")

            # Activate the mode on the virtual output
            subprocess.run(['xrandr', '--output', self.virtual_output, '--mode', self.mode_name], check=True)
            logger.info(f"Virtual display created: {self.width}x{self.height}@{self.framerate}Hz on {self.virtual_output}")
            return
                    
        except subprocess.CalledProcessError as e:
            logger.error(f"Failed to create virtual display: {e}")
            self.terminate()
        except Exception as e:
            logger.error(f"Error creating virtual display: {e}")
            self.terminate()
    
    def terminate(self):
        """Clean up and terminate the virtual display."""
        if not self.running:
            return
        
        self.running = False
        
        try:
            if self.virtual_output and self.mode_name:
                # Remove mode from output
                subprocess.run(['xrandr', '--output', self.virtual_output, '--off'], check=False)
                subprocess.run(['xrandr', '--delmode', self.virtual_output, self.mode_name], check=False)
                subprocess.run(['xrandr', '--rmmode', self.mode_name], check=False)
                logger.info("Virtual display removed")
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

if __name__ == "__main__":
    # Set up logging
    log_dir = Path.home() / '.local' / 'state' / 'breezy_xfce4' / 'logs'
    log_dir.mkdir(parents=True, exist_ok=True)
    
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
        handlers=[
            logging.FileHandler(log_dir / 'virtualdisplay.log'),
            logging.StreamHandler()
        ]
    )
    
    parser = argparse.ArgumentParser(description="XFCE4 Virtual Display")
    parser.add_argument("--width", type=int, required=True, help="Display width")
    parser.add_argument("--height", type=int, required=True, help="Display height")
    parser.add_argument("--framerate", type=int, default=60, help="Display framerate")
    args = parser.parse_args()
    
    signal.signal(signal.SIGTERM, graceful_shutdown)
    signal.signal(signal.SIGINT, graceful_shutdown)
    
    global virtual_display_instance
    virtual_display_instance = VirtualDisplayXFCE4(
        args.width, args.height, args.framerate, _on_display_closed
    )
    
    try:
        virtual_display_instance.create()
        # Keep running until terminated
        while virtual_display_instance.running:
            time.sleep(1)
    except KeyboardInterrupt:
        virtual_display_instance.terminate()
    except Exception as e:
        logger.error(f"Error: {e}")
        virtual_display_instance.terminate()
        sys.exit(1)

