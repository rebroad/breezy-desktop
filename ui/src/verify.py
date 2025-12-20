import logging
import os
import subprocess

from .files import get_bin_home

logger = logging.getLogger('breezy_ui')

def verify_installation():
    verify_installation_path = os.path.join(get_bin_home(), 'breezy_gnome_verify')

    if not os.path.exists(verify_installation_path):
        logger.warning(f"Could not verify your Breezy GNOME installation. Verification script not found at {verify_installation_path}. Skipping verification.")
        return True  # Return True to allow app to continue, but log the warning

    try:
        verify_output = subprocess.check_output([verify_installation_path], stderr=subprocess.STDOUT).strip()
        success = verify_output == b"Verification succeeded"
        if not success:
            logger.error(f"Could not verify your Breezy GNOME installation. Please ensure that Breezy GNOME is installed.")
            logger.error(verify_output)
        
        return success
    except subprocess.CalledProcessError as e:
        logger.error(f"Could not verify your Breezy GNOME installation. Please ensure that Breezy GNOME is installed.")
        logger.error(e.output.decode().strip())

    return False