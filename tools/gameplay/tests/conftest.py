"""Shared fixtures: the loaded corpus (session-scoped — PNG decode is the cost)."""

from __future__ import annotations

import pytest

from gameplay.corpus import (
    load_amp2p_corpus,
    load_corpus,
    load_score_corpus,
    load_streak_corpus,
)


@pytest.fixture(scope="session")
def corpus():
    return load_corpus()


@pytest.fixture(scope="session")
def amp2p_corpus():
    return load_amp2p_corpus()


@pytest.fixture(scope="session")
def score_corpus():
    return load_score_corpus()


@pytest.fixture(scope="session")
def streak_corpus():
    return load_streak_corpus()


