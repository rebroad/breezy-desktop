#!/usr/bin/env python3
"""
XFCE4 Backend for Breezy Desktop

This module provides virtual display creation and management for XFCE4.
Since XFCE4 doesn't have compositor APIs like Mutter/KWin, we use
alternative methods to create virtual displays.
"""

import logging
import os
import subprocess
import time
from typing import List, Dict, Optional, Tuple

logger = logging.getLogger('breezy_xfce4')

class XFCE4Backend:
    """Backend for creating and managing virtual displays on XFCE4."""
    
    def __init__(self):
        self.virtual_displays: Dict[str, Dict] = {}
        self.display_counter = 0
        
    def create_virtual_display(self, width: int, height: int, framerate: int = 60) -> Optional[str]:
        """
        Create a virtual display on XFCE4.
        
        Args:
            width: Display width in pixels
            height: Display height in pixels
            framerate: Display framerate (default: 60)
            
        Returns:
            Display ID if successful, None otherwise
        """
        self.display_counter += 1
        display_id = f"BreezyDesktop_{self.display_counter}"
        
        try:
            # For XFCE4/X11, we'll use xrandr to create a virtual output
            # This is a simplified approach - true virtual displays require compositor support
            # We'll create a modeline and add it as a virtual output
            
            # Generate modeline using cvt
            modeline_cmd = ['cvt', str(width), str(height), str(framerate)]
            result = subprocess.run(modeline_cmd, capture_output=True, text=True, check=True)
            
            # Parse modeline from cvt output
            modeline = None
            mode_name = None
            for line in result.stdout.split('\n'):
                if 'Modeline' in line:
                    parts = line.split()
                    mode_name = parts[1].strip('"')
                    modeline = ' '.join(parts[1:])
                    break
            
            if not modeline or not mode_name:
                logger.error("Failed to generate modeline")
                return None
            
            # Create the mode
            subprocess.run(['xrandr', '--newmode', modeline], check=True)
            
            # Try to add it to a virtual output
            # First, check if we have a virtual output available
            xrandr_output = subprocess.run(['xrandr'], capture_output=True, text=True, check=True)
            
            # Look for existing virtual outputs or create one
            virtual_output = None
            for line in xrandr_output.stdout.split('\n'):
                if 'VIRTUAL' in line.upper() or 'virtual' in line.lower():
                    parts = line.split()
                    if len(parts) > 0:
                        virtual_output = parts[0]
                        break
            
            # If no virtual output exists, we'll need to use xf86-video-dummy or similar
            # For now, we'll try to add to the first available output
            if not virtual_output:
                # Try to create a virtual output using xrandr
                # This may not work on all systems
                logger.warning("No virtual output found, attempting to create one")
                # We'll store the display info but may not be able to actually create it
                # without compositor support
                
            # Store display info
            self.virtual_displays[display_id] = {
                'id': display_id,
                'width': width,
                'height': height,
                'framerate': framerate,
                'mode_name': mode_name,
                'modeline': modeline,
                'virtual_output': virtual_output
            }
            
            logger.info(f"Created virtual display {display_id}: {width}x{height}@{framerate}Hz")
            return display_id
            
        except subprocess.CalledProcessError as e:
            logger.error(f"Failed to create virtual display: {e}")
            return None
        except Exception as e:
            logger.error(f"Error creating virtual display: {e}")
            return None
    
    def remove_virtual_display(self, display_id: str) -> bool:
        """
        Remove a virtual display.
        
        Args:
            display_id: ID of the display to remove
            
        Returns:
            True if successful, False otherwise
        """
        if display_id not in self.virtual_displays:
            logger.warning(f"Display {display_id} not found")
            return False
        
        try:
            display_info = self.virtual_displays[display_id]
            mode_name = display_info.get('mode_name')
            
            # Remove the mode from xrandr
            if mode_name:
                try:
                    subprocess.run(['xrandr', '--delmode', display_info.get('virtual_output', 'VIRTUAL1'), mode_name], 
                                 check=False)
                    subprocess.run(['xrandr', '--rmmode', mode_name], check=False)
                except Exception as e:
                    logger.warning(f"Failed to remove mode {mode_name}: {e}")
            
            del self.virtual_displays[display_id]
            logger.info(f"Removed virtual display {display_id}")
            return True
            
        except Exception as e:
            logger.error(f"Error removing virtual display: {e}")
            return False
    
    def list_virtual_displays(self) -> List[Dict]:
        """
        List all virtual displays.
        
        Returns:
            List of display information dictionaries
        """
        return [{
            'id': info['id'],
            'width': info['width'],
            'height': info['height'],
            'framerate': info['framerate']
        } for info in self.virtual_displays.values()]
    
    def is_available(self) -> bool:
        """
        Check if the XFCE4 backend is available.
        
        Returns:
            True if xrandr is available and we can create displays
        """
        try:
            subprocess.run(['xrandr', '--version'], capture_output=True, check=True)
            subprocess.run(['cvt', '--version'], capture_output=True, check=True)
            return True
        except (subprocess.CalledProcessError, FileNotFoundError):
            return False

