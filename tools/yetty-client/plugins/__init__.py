"""Plugin discovery and management for yetty-client."""

import importlib
import pkgutil
from pathlib import Path
from typing import Dict, Any
import click


def discover_plugins() -> Dict[str, click.Command]:
    """Discover all plugins in the plugins directory.
    
    Returns:
        Dictionary mapping plugin names to their Click commands
    """
    plugins = {}
    package_path = Path(__file__).parent
    
    for _, name, _ in pkgutil.iter_modules([str(package_path)]):
        if name.startswith('_'):
            continue
        try:
            module = importlib.import_module(f'.{name}', package=__package__)
            # Look for a click command with the same name as the module
            if hasattr(module, name):
                cmd = getattr(module, name)
                if isinstance(cmd, click.Command):
                    plugins[name] = cmd
            # Also check for 'command' attribute
            elif hasattr(module, 'command'):
                cmd = getattr(module, 'command')
                if isinstance(cmd, click.Command):
                    plugins[name] = cmd
        except Exception as e:
            click.echo(f"Warning: Failed to load plugin '{name}': {e}", err=True)
    
    return plugins


def get_plugin(name: str) -> click.Command:
    """Get a specific plugin by name.
    
    Args:
        name: Plugin name
        
    Returns:
        Click command for the plugin
        
    Raises:
        KeyError: If plugin not found
    """
    plugins = discover_plugins()
    if name not in plugins:
        raise KeyError(f"Plugin '{name}' not found. Available: {', '.join(plugins.keys())}")
    return plugins[name]


def list_plugins() -> list:
    """List all available plugin names.
    
    Returns:
        List of plugin names
    """
    return list(discover_plugins().keys())
