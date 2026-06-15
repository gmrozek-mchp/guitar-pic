"""Shared fixtures: the loaded corpus (session-scoped — PNG decode is the cost)."""

from __future__ import annotations

import pytest

from gameplay.corpus import load_corpus


@pytest.fixture(scope="session")
def corpus():
    return load_corpus()
