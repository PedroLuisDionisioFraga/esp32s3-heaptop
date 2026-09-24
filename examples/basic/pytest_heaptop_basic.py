# SPDX-License-Identifier: MIT
"""On-device checks for the heaptop basic example (pytest-embedded).

Run from examples/basic with the board attached:
    pytest pytest_heaptop_basic.py --target esp32s3 --port COMx
"""
import json
import re
import time

import pytest
from pytest_embedded import Dut
from pytest_embedded_idf.utils import idf_parametrize

# Anchored on the line end: a line still arriving can end in an inner '}'.
JSON_LINE = re.compile(rb'(\{"ht":2,[^\r\n]*\})\r?\n')


def _ready(dut: Dut) -> None:
    dut.expect(dut.target + '> ', timeout=30)
    time.sleep(2.5)  # at least two samples, so CPU % exists


def _run(dut: Dut, line: str) -> None:
    dut.write(line)
    time.sleep(0.3)


@pytest.mark.generic
@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_heaptop_views(dut: Dut) -> None:
    _ready(dut)
    _run(dut, 'ht help')
    dut.expect('ht health')
    dut.expect('ht stress')
    _run(dut, 'ht heap')
    dut.expect(re.compile(rb'internal\s+[\d.]+[KM]'))
    dut.expect(re.compile(rb'psram\s+[\d.]+[KM]'))
    _run(dut, 'ht tasks')
    dut.expect('NAME')
    dut.expect('heaptop')
    _run(dut, 'ht health')
    dut.expect(re.compile(rb'health (OK|: \d+ alerts?)'))
    dut.expect('dram_free')
    dut.expect('back past the limit')


@pytest.mark.generic
@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_heaptop_clear_starts_a_fresh_window(dut: Dut) -> None:
    _ready(dut)
    _run(dut, 'ht clear')
    dut.expect('stats cleared')
    _run(dut, 'ht health')
    dut.expect(re.compile(rb'stats since clear \d+s ago'))
    dut.expect(re.compile(rb'failures 0 since clear'))


@pytest.mark.generic
@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_heaptop_stress_loads_every_core(dut: Dut) -> None:
    _ready(dut)
    _run(dut, 'ht stress cpu 50 4')
    dut.expect(re.compile(rb'cpu stress: 50% on 2 cores, \d+ s left'))
    time.sleep(2.5)  # two samples with the load on
    _run(dut, 'ht tasks cpu')
    for core in (0, 1):
        load = float(dut.expect(re.compile(rb'ht_stress%d\s+\S+\s+\d+\s+\d+\s+([\d.]+)' % core)).group(1))
        assert 35.0 <= load <= 65.0, f'ht_stress{core} at {load}%'
    time.sleep(3)
    _run(dut, 'ht stress')
    dut.expect('cpu stress: idle')


@pytest.mark.generic
@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_heaptop_stream_is_json_lines(dut: Dut) -> None:
    _ready(dut)
    dut.write('ht stream')
    types = set()
    for _ in range(40):
        line = dut.expect(JSON_LINE, timeout=10).group(1)
        record = json.loads(line)
        assert record['ht'] == 2
        types.add(record['type'])
        if record['type'] == 'sample':
            assert 'allocs_s' not in record
            assert 'since_ms' in record
        if {'sample', 'task'} <= types:
            break
    dut.write('q')
    assert {'sample', 'task'} <= types
