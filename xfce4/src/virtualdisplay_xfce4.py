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
    
    def __init__(self, width, height, framerate, on_closed_cb):
        self.width = width
        self.height = height
        self.framerate = framerate
        self.on_closed_cb = on_closed_cb
        self.mode_name = None
        self.virtual_output = None
        self.running = True
        
    def create(self):
        """Create the virtual display."""
        try:
            # Generate modeline
            modeline_cmd = ['cvt', str(self.width), str(self.height), str(self.framerate)]
            result = subprocess.run(modeline_cmd, capture_output=True, text=True, check=True)
            
            # Parse modeline
            for line in result.stdout.split('\n'):
                if 'Modeline' in line:
                    parts = line.split()
                    self.mode_name = parts[1].strip('"')
                    modeline = ' '.join(parts[1:])
                    
                    # Create the mode
                    subprocess.run(['xrandr', '--newmode', modeline], check=True)
                    logger.info(f"Created mode: {self.mode_name}")
                    
                    # Try to find or create a virtual output
                    xrandr_output = subprocess.run(['xrandr'], capture_output=True, text=True, check=True)
                    
                    # Look for VIRTUAL output
                    for line in xrandr_output.stdout.split('\n'):
                        if 'VIRTUAL' in line.upper():
                            parts = line.split()
                            if len(parts) > 0:
                                self.virtual_output = parts[0]
                                break
                    
                    # If no virtual output, we can't actually create the display
                    # This is a limitation of X11 without compositor support
                    if not self.virtual_output:
                        logger.warning("No virtual output available. Virtual display creation on XFCE4/X11 is limited.")
                        logger.warning("Consider using xf86-video-dummy driver or switching to Wayland for full support.")
                        # We'll still register the mode but it won't be usable
                        return
                    
                    # Add mode to virtual output
                    subprocess.run(['xrandr', '--addmode', self.virtual_output, self.mode_name], check=True)
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

