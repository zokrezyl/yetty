"""OSC sequence generation for yetty plugins.

OSC Format: ESC ] 99999;<plugin>;<mode>;<x>;<y>;<w>;<h>;<base94_payload> ST
Where ST = ESC \\ (string terminator)
"""

from . import base94

VENDOR_ID = 99999


def create_sequence(
    plugin: str,
    mode: str,
    x: int,
    y: int,
    w: int,
    h: int,
    payload: str
) -> str:
    """Create an OSC sequence for plugin activation.
    
    Args:
        plugin: Plugin name (e.g., 'ymery', 'shadertoy', 'image')
        mode: Position mode - 'A' (absolute) or 'R' (relative to cursor)
        x, y: Position in cells
        w, h: Size in cells
        payload: Raw payload string (will be base94 encoded)
    
    Returns:
        Complete OSC escape sequence
    """
    encoded_payload = base94.encode_string(payload)
    return f"\033]{VENDOR_ID};{plugin};{mode};{x};{y};{w};{h};{encoded_payload}\033\\"


def create_delete_sequence(plugin: str, instance_id: int) -> str:
    """Create an OSC sequence to delete a plugin instance.
    
    Args:
        plugin: Plugin name
        instance_id: ID of the instance to delete
    
    Returns:
        Complete OSC escape sequence
    """
    return f"\033]{VENDOR_ID};{plugin};D;{instance_id}\033\\"


def create_update_sequence(plugin: str, instance_id: int, payload: str) -> str:
    """Create an OSC sequence to update a plugin instance.
    
    Args:
        plugin: Plugin name
        instance_id: ID of the instance to update
        payload: New payload string (will be base94 encoded)
    
    Returns:
        Complete OSC escape sequence
    """
    encoded_payload = base94.encode_string(payload)
    return f"\033]{VENDOR_ID};{plugin};U;{instance_id};{encoded_payload}\033\\"


def create_query_sequence() -> str:
    """Create an OSC sequence to query active plugins.
    
    Returns:
        Complete OSC escape sequence
    """
    return f"\033]{VENDOR_ID};?;Q\033\\"
