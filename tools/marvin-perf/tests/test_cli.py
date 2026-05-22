"""CLI argument parsing — record-type mask spec resolution."""

from __future__ import annotations

import argparse

import pytest

from marvin_perf.cli import parse_types_arg
from marvin_perf.records import RecordType


def test_types_none_returns_none() -> None:
    assert parse_types_arg(None) is None


def test_types_flag_parses() -> None:
    mask = parse_types_arg("STAMP,SESSION")
    assert mask == (1 << RecordType.SESSION) | (1 << RecordType.STAMP)


def test_types_case_insensitive_and_whitespace() -> None:
    assert parse_types_arg("stamp, session") == parse_types_arg("STAMP,SESSION")


def test_types_all_token() -> None:
    assert parse_types_arg("ALL") == 0xFFFFFFFF


def test_types_min_token() -> None:
    expected = (1 << RecordType.SESSION) | (1 << RecordType.DROP)
    assert parse_types_arg("MIN") == expected


def test_types_min_combines_with_more() -> None:
    expected = (1 << RecordType.SESSION) | (1 << RecordType.DROP) | (1 << RecordType.STAMP)
    assert parse_types_arg("MIN,STAMP") == expected


def test_types_unknown_raises() -> None:
    with pytest.raises(argparse.ArgumentTypeError):
        parse_types_arg("BOGUS")
