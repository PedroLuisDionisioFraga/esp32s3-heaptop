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

JSON_LINE = re.compile(rb'(\{"ht":1,[^\r\n]*\})')


def _ready(dut: Dut) -> None:
    dut.expect(dut.target + '> ', timeout=30)
    time.sleep(2.5)  # at least two samples, so CPU % and rates exist


def _run(dut: Dut, line: str) -> None:
    dut.write(line)
    time.sleep(0.3)


@pytest.mark.generic
@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_heaptop_views(dut: Dut) -> None:
    _ready(dut)
    _run(dut, 'ht help')
    dut.expect('ht leaks')
    _run(dut, 'ht heap')
    dut.expect(re.compile(rb'internal\s+[\d.]+[KM]'))
    dut.expect(re.compile(rb'psram\s+[\d.]+[KM]'))
    _run(dut, 'ht tasks')
    dut.expect('NAME')
    dut.expect('heaptop')
    _run(dut, 'ht frag')
    dut.expect('free blocks')
    _run(dut, 'ht allocs')
    dut.expect('allocs/s')
    _run(dut, 'ht alerts')
    dut.expect('hysteresis')


@pytest.mark.generic
@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_heaptop_leak_capture_groups_by_call_stack(dut: Dut) -> None:
    _ready(dut)
    _run(dut, 'ht leaks start')
    dut.expect('leak capture running')
    _run(dut, 'stress leak 256 50')
    time.sleep(3)
    _run(dut, 'ht leaks stop')
    match = dut.expect(re.compile(rb'stopped after [\d.]+s: (\d+) surviving'))
    assert int(match.group(1)) >= 20
    dut.expect(re.compile(rb'\s+0x4[0-9a-f]{7}'))
    _run(dut, 'stress stop')
    dut.expect('stopped; freed')


@pytest.mark.generic
@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_heaptop_flags_leaking_task(dut: Dut) -> None:
    _ready(dut)
    _run(dut, 'stress leak 512 100')
    time.sleep(24)  # a task needs >= 20 samples of history, and > 4 KB of growth, to be flagged
    _run(dut, 'ht tasks heap')
    dut.expect(re.compile(rb'stress_leak[^\r\n]*LEAK\?'))
    _run(dut, 'stress stop')
    dut.expect('stopped; freed')


@pytest.mark.generic
@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_heaptop_logs_allocation_failure(dut: Dut) -> None:
    _ready(dut)
    _run(dut, 'stress fail 100000000')
    dut.expect('failed as intended')
    time.sleep(1.5)
    _run(dut, 'ht allocs')
    dut.expect(re.compile(rb'failures [1-9]\d* since boot'))
    dut.expect('heap_caps_malloc')


@pytest.mark.generic
@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_heaptop_stream_is_json_lines(dut: Dut) -> None:
    _ready(dut)
    dut.write('ht stream')
    types = set()
    for _ in range(40):
        line = dut.expect(JSON_LINE, timeout=10).group(1)
        record = json.loads(line)
        assert record['ht'] == 1
        types.add(record['type'])
        if {'sample', 'task'} <= types:
            break
    dut.write('q')
    assert {'sample', 'task'} <= types
