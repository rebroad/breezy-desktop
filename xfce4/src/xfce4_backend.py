#!/usr/bin/env python3
"""
XFCE4 Backend for Breezy Desktop

This module provides virtual display creation and management for XFCE4.
Since XFCE4 doesn't have compositor APIs like Mutter/KWin, we use
alternative methods to create virtual displays.
"""

import logging
import subprocess
from typing import List, Dict, Optional

from gi.repository import Gio, GLib

logger = logging.getLogger('breezy_xfce4')

_VD_BUS_NAME = "org.xfce.Xfwm.VirtualDisplay"
_VD_OBJECT_PATH = "/org/xfce/Xfwm/VirtualDisplayManager"
_VD_IFACE = "org.xfce.Xfwm.VirtualDisplayManager"

class XFCE4Backend:
    """Backend for creating and managing virtual displays on XFCE4."""
    
    def __init__(self):
        self.virtual_displays: Dict[int, Dict] = {}
        self.display_counter = 0

        # Connect to the XFCE4 VirtualDisplayManager D-Bus service if available
        try:
            bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
            self._proxy = Gio.DBusProxy.new_sync(
                bus,
                Gio.DBusProxyFlags.NONE,
                None,
                _VD_BUS_NAME,
                _VD_OBJECT_PATH,
                _VD_IFACE,
                None,
            )
            logger.info("Connected to XFCE4 VirtualDisplayManager D-Bus service")
        except Exception as e:
            self._proxy = None
            logger.warning("XFCE4 VirtualDisplayManager D-Bus service not available: %s", e)
        
    def create_virtual_display(self, width: int, height: int, framerate: int = 60) -> Optional[int]:
        """
        Create a virtual display on XFCE4.
        
        Args:
            width: Display width in pixels
            height: Display height in pixels
            framerate: Display framerate (default: 60)
            
        Returns:
            Display ID if successful, None otherwise
        """
        try:
            if self._proxy is None:
                logger.error("XFCE4 VirtualDisplayManager D-Bus service is not available")
                return None

            # Create a virtual display via D-Bus
            res = self._proxy.call_sync(
                "CreateVirtualDisplay",
                GLib.Variant("(uuus)", (width, height, framerate, "desktop")),
                Gio.DBusCallFlags.NO_AUTO_START,
                -1,
                None,
            )
            (display_id,) = res.unpack()

            # Query its surface (window/pixmap) handle
            res2 = self._proxy.call_sync(
                "GetVirtualDisplaySurface",
                GLib.Variant("(u)", (display_id,)),
                Gio.DBusCallFlags.NO_AUTO_START,
                -1,
                None,
            )
            surface_id, surface_type = res2.unpack()

            self.virtual_displays[display_id] = {
                "id": display_id,
                "width": width,
                "height": height,
                "framerate": framerate,
                "surface_id": surface_id,
                "surface_type": surface_type,
            }

            logger.info(
                "Created XFCE4 virtual display id=%d surface_id=%d type=%s (%dx%d@%d)",
                display_id,
                surface_id,
                surface_type,
                width,
                height,
                framerate,
            )
            return display_id
            
        except subprocess.CalledProcessError as e:
            logger.error(f"Failed to create virtual display: {e}")
            return None
        except Exception as e:
            logger.error(f"Error creating virtual display: {e}")
            return None
    
    def remove_virtual_display(self, display_id: int) -> bool:
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
            if self._proxy is not None:
                try:
                    self._proxy.call_sync(
                        "DestroyVirtualDisplay",
                        GLib.Variant("(u)", (display_id,)),
                        Gio.DBusCallFlags.NO_AUTO_START,
                        -1,
                        None,
                    )
                except Exception as e:
                    logger.warning("Error destroying virtual display via D-Bus: %s", e)

            del self.virtual_displays[display_id]
            logger.info("Removed virtual display %d", display_id)
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
        return self._proxy is not None

