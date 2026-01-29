"""
SDS Logging Configuration.

This module provides logging setup for the SDS library.
By default, the library uses a NullHandler to avoid unwanted output.
Applications can configure logging as needed.

Example:
    # Enable SDS logging to console
    import logging
    from sds import configure_logging
    
    configure_logging(level=logging.DEBUG)
    
    # Or configure manually
    logging.getLogger("sds").setLevel(logging.DEBUG)
    logging.getLogger("sds").addHandler(logging.StreamHandler())
"""
import logging
from typing import Optional

# Library logger - uses NullHandler by default (library best practice)
logger = logging.getLogger("sds")
logger.addHandler(logging.NullHandler())


def configure_logging(
    level: int = logging.INFO,
    format: Optional[str] = None,
    handler: Optional[logging.Handler] = None,
) -> None:
    """
    Configure logging for the SDS library.
    
    This is a convenience function for setting up logging. For more
    complex setups, configure the "sds" logger directly.
    
    Args:
        level: Logging level (default: INFO)
        format: Log format string (default: "[%(levelname)s] sds: %(message)s")
        handler: Custom handler (default: StreamHandler to stderr)
    
    Example:
        >>> from sds import configure_logging
        >>> import logging
        >>> configure_logging(level=logging.DEBUG)
    """
    sds_logger = logging.getLogger("sds")
    sds_logger.setLevel(level)
    
    # Remove existing handlers to avoid duplicates
    for h in sds_logger.handlers[:]:
        if not isinstance(h, logging.NullHandler):
            sds_logger.removeHandler(h)
    
    # Add the specified or default handler
    if handler is None:
        handler = logging.StreamHandler()
        if format is None:
            format = "[%(levelname)s] sds: %(message)s"
        handler.setFormatter(logging.Formatter(format))
    
    sds_logger.addHandler(handler)


def get_logger(name: str) -> logging.Logger:
    """
    Get a logger for an SDS submodule.
    
    Args:
        name: Module name (will be prefixed with "sds.")
    
    Returns:
        Logger instance
    """
    if name.startswith("sds."):
        return logging.getLogger(name)
    return logging.getLogger(f"sds.{name}")
