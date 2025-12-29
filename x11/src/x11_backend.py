#!/usr/bin/env python3
"""
X11 Backend for Breezy Desktop

This module provides virtual display creation and management for Xorg-based
desktop environments/WMs (XFCE4, i3, Openbox, etc.) using the Xorg modesetting
driver's virtual XR connector API.
"""

import logging
import os
import subprocess
import time
from typing import List, Dict, Optional, Tuple

logger = logging.getLogger('breezy_x11')

class X11Backend:
    """Backend for creating and managing virtual displays on Xorg-based desktops via XR-Manager."""
    
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
        Check if the X11 backend is available.
        
        Returns:
            True if xrandr is available and XR-Manager exists
        """
        if not self.xr_manager_available:
            self._check_xr_manager_availability()
        return self.xr_manager_available
    
    def get_physical_xr_connector_refresh_rates(self, connector_name: Optional[str] = None) -> List[int]:
        """
        Query physical XR connector for supported refresh rates.

        Args:
            connector_name: Name of physical XR connector (e.g., "DisplayPort-0").
                           If None, attempts to auto-detect the XR connector.

        Returns:
            List of supported refresh rates in Hz (e.g., [60, 72, 90])
        """
        try:
            # If connector name not provided, try to find XR connector
            if not connector_name:
                # Query all outputs to find XR connector
                result = subprocess.run(['xrandr', '--listoutputs'],
                                      capture_output=True, text=True, check=True)
                for line in result.stdout.splitlines():
                    # Look for connected outputs that might be XR devices
                    # Common XR connectors: DisplayPort-*, HDMI-*
                    if 'connected' in line.lower():
                        parts = line.split()
                        if parts:
                            potential_connector = parts[0]
                            # Check if it's likely an XR connector (not XR-Manager or XR-* virtual)
                            if not potential_connector.startswith('XR-') and potential_connector != 'XR-Manager':
                                # Query this connector for modes
                                try:
                                    modes_result = subprocess.run(
                                        ['xrandr', '--output', potential_connector, '--query'],
                                        capture_output=True, text=True, check=True
                                    )
                                    # Check if it has non-desktop property (XR devices are often marked as such)
                                    props_result = subprocess.run(
                                        ['xrandr', '--output', potential_connector, '--props'],
                                        capture_output=True, text=True, check=False
                                    )
                                    if 'non-desktop' in props_result.stdout.lower() or \
                                       'xreal' in modes_result.stdout.lower() or \
                                       'viture' in modes_result.stdout.lower() or \
                                       'nreal' in modes_result.stdout.lower():
                                        connector_name = potential_connector
                                        logger.info(f"Auto-detected XR connector: {connector_name}")
                                        break
                                except subprocess.CalledProcessError:
                                    continue

            if not connector_name:
                logger.warning("No XR connector found, using default refresh rate")
                return [60]  # Default fallback

            # Query connector modes via xrandr
            result = subprocess.run(
                ['xrandr', '--output', connector_name, '--query'],
                capture_output=True, text=True, check=True
            )

            # Parse output to extract refresh rates
            # Format: "   1920x1080     60.00*+  75.00    50.00"
            import re
            refresh_rates = []
            for line in result.stdout.splitlines():
                # Look for mode lines with refresh rates
                if 'x' in line and ('Hz' in line or re.search(r'\d+\.\d+', line)):
                    # Extract refresh rates (numbers followed by Hz, *, +, or space)
                    rates = re.findall(r'(\d+\.?\d*)\s*[*+]?', line)
                    for rate_str in rates:
                        try:
                            rate = int(float(rate_str))
                            # Filter reasonable refresh rates (1-1000 Hz)
                            if 1 <= rate <= 1000 and rate not in refresh_rates:
                                refresh_rates.append(rate)
                        except ValueError:
                            pass

            # Sort and return
            refresh_rates = sorted(refresh_rates) if refresh_rates else [60]
            logger.info(f"Found refresh rates for {connector_name}: {refresh_rates}")
            return refresh_rates

        except subprocess.CalledProcessError as e:
            logger.warning(f"Failed to query refresh rates for {connector_name}: {e}")
            return [60]  # Default fallback
        except Exception as e:
            logger.warning(f"Error querying refresh rates: {e}")
            return [60]  # Default fallback

    def create_virtual_display(self, width: int, height: int, framerate: int = 60, 
                               name: str = "XR-0", refresh_rates: Optional[List[int]] = None,
                               physical_connector: Optional[str] = None) -> Optional[str]:
        """
        Create a virtual XR display using XR-Manager.
        
        Args:
            width: Display width in pixels
            height: Display height in pixels
            framerate: Display framerate (default: 60) - used for initial creation
            name: Virtual output name (default: "XR-0")
            refresh_rates: List of supported refresh rates (e.g., [60, 72, 90]).
                          If None, attempts to query from physical XR connector.
            physical_connector: Name of physical XR connector to query for refresh rates.
                              If None, attempts to auto-detect.

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
            
            # Query refresh rates if not provided
            if refresh_rates is None:
                refresh_rates = self.get_physical_xr_connector_refresh_rates(physical_connector)

            # Set multiple refresh rates if we have more than one
            if len(refresh_rates) > 1:
                # Build modes string: "WIDTH:HEIGHT:REFRESH|WIDTH:HEIGHT:REFRESH|..."
                modes_str = "|".join([f"{width}:{height}:{rate}" for rate in refresh_rates])

                # Set XR_MODES property on the virtual output
                try:
                    subprocess.run(
                        ['xrandr', '--output', name,
                         '--set', 'XR_MODES', modes_str],
                        capture_output=True, text=True, check=True
                    )
                    logger.info(f"Set {len(refresh_rates)} refresh rates for {name}: {refresh_rates}")
                except subprocess.CalledProcessError as e:
                    logger.warning(f"Failed to set XR_MODES property (may not be supported): {e}")
                    # Continue anyway - backward compatible

            # Store display info
            self.virtual_displays[name] = {
                'id': name,
                'width': width,
                'height': height,
                'framerate': framerate,
                'refresh_rates': refresh_rates or [framerate]
            }
            
            logger.info(f"Created virtual XR display {name}: {width}x{height}@{framerate}Hz (refresh rates: {refresh_rates})")
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
