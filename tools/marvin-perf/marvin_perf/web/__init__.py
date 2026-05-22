"""Optional viewer surface — gated behind the `viewer` dep group.

This package imports fastapi / pillow at module load. Importing it from a
default install raises ImportError; cli.py's `cmd_serve` catches that and
prints a helpful `uv sync --group viewer` hint.
"""
