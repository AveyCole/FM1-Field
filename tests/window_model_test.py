#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Bandwidth-delay playback model; not a real TCP/WiFi integration test.
Run after a host build: python3 tests/window_model_test.py PATH_TO_320KBPS_MP3
"""
from pathlib import Path
import os,re,subprocess,sys
r=Path(__file__).resolve().parents[1]
opts=(r/'base/ble/lwip/lwipopts_sfc.h').read_text()
segments=int(re.search(r'#define TCP_WND\s+\((\d+)\*TCP_MSS\)',opts)[1])
pool=int(re.search(r'#define PBUF_POOL_SIZE\s+(\d+)',opts)[1])
assert re.search(r'#define NETIF_MTU 1240',opts)
assert re.search(r'#define TCP_MSS\s+\(NETIF_MTU - 40\)',opts)
assert pool>=segments+4, 'retain packet-pool headroom beyond the advertised window'
for window,case in [(6*1200,'aporee_window_old.host'),(segments*1200,'aporee_window.host')]:
    env=dict(os.environ,APOREE_TEST_STREAM=str(Path(sys.argv[1]).resolve()),APOREE_TEST_WINDOW=str(window),APOREE_TEST_RTT='200')
    for key in ['APOREE_TEST_STALL','APOREE_TEST_MP3']:env.pop(key,None)
    print(f'Window/RTT model: {window} bytes / 200 ms, ceiling {window*8/200:.0f} kbps',flush=True)
    subprocess.run([str(r/'app/build/host/field_host'),str(r/'tests'/case),str(r/'app/build/tests')],env=env,check=True,cwd=r)
