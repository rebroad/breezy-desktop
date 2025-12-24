#!/usr/bin/env python3
"""
XFCE4 Backend for Breezy Desktop

This module provides virtual display creation and management for XFCE4
using the Xorg modesetting driver's virtual XR connector API.
"""

import logging
import os
import subprocess
import time
from typing import List, Dict, Optional, Tuple

logger = logging.getLogger('breezy_xfce4')

class XFCE4Backend:
    """Backend for creating and managing virtual displays on XFCE4 via XR-Manager."""
    
    def __init__(self):
        self.virtual_displays: Dict[str, Dict] = {}
        self.xr_manager_available = False
        
        # Check if XR-Manager is available
        self._check_xr_manager_availability()
        
    def _check_xr_manager_availability(self) -> bool:
        """Check if XR-Manager output exists in xrandr."""
        try:
            result = subprocess.run(['xrandr', '--listoutputs'], 
                                  capture_output=True, text=True, check=True)
            self.xr_manager_available = 'XR-Manager' in result.stdout
            if not self.xr_manager_available:
                logger.warning("XR-Manager output not found. Virtual XR connector may not be available.")
            return self.xr_manager_available
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            logger.error(f"Failed to check for XR-Manager: {e}")
            self.xr_manager_available = False
            return False
    
    def is_available(self) -> bool:
        """
        Check if the XFCE4 backend is available.
        
        Returns:
            True if xrandr is available and XR-Manager exists
        """
        if not self.xr_manager_available:
            self._check_xr_manager_availability()
        return self.xr_manager_available
    
    def create_virtual_display(self, width: int, height: int, framerate: int = 60, 
                               name: str = "XR-0") -> Optional[str]:
        """
        Create a virtual XR display using XR-Manager.
        
        Args:
            width: Display width in pixels
            height: Display height in pixels
            framerate: Display framerate (default: 60)
            name: Virtual output name (default: "XR-0")
            
        Returns:
            Display ID (output name) if successful, None otherwise
        """
        if not self.is_available():
            logger.error("XR-Manager not available, cannot create virtual display")
            return None
        
        try:
            # Create virtual output via XR-Manager CREATE_XR_OUTPUT property
            # Format: "NAME:WIDTH:HEIGHT:REFRESH"
            create_cmd = f"{name}:{width}:{height}:{framerate}"
            result = subprocess.run(
                ['xrandr', '--output', 'XR-Manager', 
                 '--set', 'CREATE_XR_OUTPUT', create_cmd],
                capture_output=True, text=True, check=True
            )
            
            # Verify the output was created
            xrandr_output = subprocess.run(['xrandr', '--listoutputs'], 
                                          capture_output=True, text=True, check=True)
            if name not in xrandr_output.stdout:
                logger.error(f"Virtual output {name} was not created")
                return None
            
            # Store display info
            self.virtual_displays[name] = {
                'id': name,
                'width': width,
                'height': height,
                'framerate': framerate
            }
            
            logger.info(f"Created virtual XR display {name}: {width}x{height}@{framerate}Hz")
            return name
            
        except subprocess.CalledProcessError as e:
            logger.error(f"Failed to create virtual display: {e}")
            if e.stderr:
                logger.error(f"Error output: {e.stderr}")
            return None
        except Exception as e:
            logger.error(f"Error creating virtual display: {e}")
            return None
    
    def remove_virtual_display(self, display_id: str) -> bool:
        """
        Remove a virtual XR display.
        
        Args:
            display_id: ID (output name) of the display to remove (e.g., "XR-0")
            
        Returns:
            True if successful, False otherwise
        """
        if display_id not in self.virtual_displays:
            logger.warning(f"Display {display_id} not found in tracked displays")
        
        if not self.is_available():
            logger.error("XR-Manager not available, cannot remove virtual display")
            return False
        
        try:
            # Delete virtual output via XR-Manager DELETE_XR_OUTPUT property
            result = subprocess.run(
                ['xrandr', '--output', 'XR-Manager',
                 '--set', 'DELETE_XR_OUTPUT', display_id],
                capture_output=True, text=True, check=True
            )
            
            # Verify the output was removed
            xrandr_output = subprocess.run(['xrandr', '--listoutputs'], 
                                          capture_output=True, text=True, check=True)
            if display_id in xrandr_output.stdout:
                logger.warning(f"Virtual output {display_id} still exists after deletion attempt")
                return False
            
            if display_id in self.virtual_displays:
                del self.virtual_displays[display_id]
            
            logger.info(f"Removed virtual XR display {display_id}")
            return True
            
        except subprocess.CalledProcessError as e:
            logger.error(f"Failed to remove virtual display: {e}")
            if e.stderr:
                logger.error(f"Error output: {e.stderr}")
            return False
        except Exception as e:
            logger.error(f"Error removing virtual display: {e}")
            return False
    
    def enable_ar_mode(self) -> bool:
        """
        Enable AR mode (hides physical XR connector, shows virtual XR connectors).
        
        Returns:
            True if successful, False otherwise
        """
        if not self.is_available():
            logger.error("XR-Manager not available, cannot enable AR mode")
            return False
        
        try:
            # Set AR_MODE property on XR-Manager to 1 (enabled)
            result = subprocess.run(
                ['xrandr', '--output', 'XR-Manager',
                 '--set', 'AR_MODE', '1'],
                capture_output=True, text=True, check=True
            )
            
            logger.info("AR mode enabled (physical XR hidden, virtual XR shown)")
            return True
            
        except subprocess.CalledProcessError as e:
            logger.error(f"Failed to enable AR mode: {e}")
            if e.stderr:
                logger.error(f"Error output: {e.stderr}")
            return False
        except Exception as e:
            logger.error(f"Error enabling AR mode: {e}")
            return False
    
    def disable_ar_mode(self) -> bool:
        """
        Disable AR mode (shows physical XR connector, hides virtual XR connectors).
        
        Returns:
            True if successful, False otherwise
        """
        if not self.is_available():
            logger.error("XR-Manager not available, cannot disable AR mode")
            return False
        
        try:
            # Set AR_MODE property on XR-Manager to 0 (disabled)
            result = subprocess.run(
                ['xrandr', '--output', 'XR-Manager',
                 '--set', 'AR_MODE', '0'],
                capture_output=True, text=True, check=True
            )
            
            logger.info("AR mode disabled (physical XR shown, virtual XR hidden)")
            return True
            
        except subprocess.CalledProcessError as e:
            logger.error(f"Failed to disable AR mode: {e}")
            if e.stderr:
                logger.error(f"Error output: {e.stderr}")
            return False
        except Exception as e:
            logger.error(f"Error disabling AR mode: {e}")
            return False
    
    def list_virtual_displays(self) -> List[Dict]:
        """
        List all virtual XR displays.
        
        Returns:
            List of display information dictionaries
        """
        # Query xrandr to get current XR outputs
        try:
            result = subprocess.run(['xrandr', '--listoutputs'], 
                                  capture_output=True, text=True, check=True)
            xr_outputs = []
            for line in result.stdout.splitlines():
                if line.strip().startswith('XR-') and 'XR-Manager' not in line:
                    # Extract output name (first word)
                    parts = line.split()
                    if parts:
                        output_name = parts[0]
                        # Get details if we have it tracked
                        if output_name in self.virtual_displays:
                            xr_outputs.append(self.virtual_displays[output_name])
                        else:
                            # Output exists but not tracked (maybe created externally)
                            xr_outputs.append({'id': output_name})
            
            return xr_outputs
        except Exception as e:
            logger.error(f"Error listing virtual displays: {e}")
            return list(self.virtual_displays.values())
