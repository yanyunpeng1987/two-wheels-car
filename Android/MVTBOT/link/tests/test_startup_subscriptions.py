#!/usr/bin/env python3
"""Replay real Android startup writes through real MCU epoch filtering.

Reporting starts [0,0,1], as after a previous wave page. Both main-loop orders,
UART delivery offsets, and a 10ms receive-to-dispatch delay are exercised.
The legacy-boundary negative fixture is an explicit possible schedule, not a
claim to reconstruct the exact device incident. No radio/vehicle is exercised.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

JAVA = r'''package com.mvtbot.link;
import android.bluetooth.*;import android.os.*;import android.content.*;import android.view.*;import java.util.*;import java.nio.charset.StandardCharsets;
public final class StartupSubscriptionTrace {
 static int checks; static long readyAt;
 static void check(boolean value,String why){checks++;if(!value)throw new AssertionError(why);}
 static UUID u(String s){return UUID.fromString("0000"+s+"-0000-1000-8000-00805f9b34fb");}
 static final Handler ui=new Handler(m->{if(!MiniBalanLink.acceptUiEvent(m))return true;if(m.what==3){readyAt=Handler.now;Handler h=new Handler(Looper.getMainLooper());h.postDelayed(()->MiniBalanLink.send("CMD|4|1|$"),100);h.postDelayed(()->MiniBalanLink.send("CMD|5|1|$"),200);h.postDelayed(()->MiniBalanLink.send("CMD|6|0|$"),300);}return true;});
 public static void main(String[] args){
  for(long delay:new long[]{60,180,450}){
   BluetoothGatt g=new BluetoothGatt();BluetoothGattService s=new BluetoothGattService(u("ffe0"));s.add(new BluetoothGattCharacteristic(u("ffe1"),24,true));g.services.add(s);
   BluetoothGattService dis=new BluetoothGattService(u("180a"));String[] ids={"2a29","2a24","2a26"},values={"QUALCOMM","HC-05D","1.1.4"};for(int i=0;i<3;i++){BluetoothGattCharacteristic c=new BluetoothGattCharacteristic(u(ids[i]),2,false);c.setValue(values[i].getBytes(StandardCharsets.US_ASCII));dis.add(c);}g.services.add(dis);
   g.vendorLengthBug=true;g.autoNotify=true;g.writeDelayMs=delay;g.notifyDelayMs=30;g.readDelayMs=30;
   MiniBalanLink.destroy();MiniBalanLink.bind(new Context(),ui);MiniBalanLink.bindControl(()->{},new View());MiniBalanLink.foreground(true);readyAt=-1;
   MiniBalanLink.connect(new BluetoothDevice("HC-05D",g));Handler.advance(0);g.discover();g.completeDescriptor(0);
   for(int i=0;i<10000&&!g.closed&&!g.writes.contains("CMD|6|0|$");i++)Handler.advance(1);
   Handler.advance(delay+20);check(!g.closed&&readyAt>=0,"startup connection and subscriptions complete");check(g.writes.subList(0,4).equals(Arrays.asList("CMD|3|0|0|$","CMD|7|$","CMD|3|0|0|$","CMD|3|0|0|$")),"read-only proof then double-zero barrier");check(readyAt>=g.writeTimes.get(3)+delay,"ready after last zero completion");
   long origin=g.writeTimes.get(0);for(int i=0;i<g.writes.size();i++)System.out.println("TRACE cb"+delay+" "+(g.writeTimes.get(i)-origin)+" "+g.writes.get(i));
   System.out.println("READY cb"+delay+" "+(readyAt-origin));
  }
  MiniBalanLink.destroy();System.out.println("JAVA_ASSERTIONS "+checks);
 }
}'''


def run(command, cwd, text=None):
    result = subprocess.run(list(map(str, command)), cwd=cwd, input=text, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path, required=True)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[3]
    output = (args.output_dir or repo / 'build/hc05d-validation/startup-subscriptions').resolve()
    output.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('fixture', here / 'test_ble_session.py')
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    inputs = []
    for name, text in fixture.STUBS.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
        inputs.append(path)
    source = output / 'com/mvtbot/link/StartupSubscriptionTrace.java'
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_text(JAVA, encoding='utf-8')
    inputs.append(source)
    actual = fixture.transport_sources(here.parent)
    inputs += actual
    classes = output / 'classes'
    classes.mkdir(exist_ok=True)
    suffix = '.exe' if (args.java_home / 'bin/javac.exe').exists() else ''
    run([args.java_home / ('bin/javac'+suffix), '-encoding', 'UTF-8', '-d', classes, *inputs], repo)
    trace = run([args.java_home / ('bin/java'+suffix), '-cp', classes, 'com.mvtbot.link.StartupSubscriptionTrace'], repo)
    (output / 'java-trace.txt').write_text(trace, encoding='utf-8')
    scenarios = {}
    for line in trace.splitlines():
        if line.startswith('TRACE '):
            _, scenario, at, wire = line.split()
            scenarios.setdefault(scenario, []).append((int(at), wire))
    # Deliberately place CMD4 just before a lease rollover and dispatch it just
    # after. Later CMD5/6 arrive in the fresh epoch. This models the silent-drop
    # branch without claiming the phone clocks directly identify UART arrival.
    scenarios['legacy_boundary'] = [(0, 'CMD|3|0|0|$'), (350, 'CMD|7|$'),
                                     (495, 'CMD|4|1|$'), (620, 'CMD|5|1|$'), (720, 'CMD|6|0|$')]
    fixture_c = (repo / 'tests/bluetooth_adapter_test.c').as_posix()
    actual_c = (repo / 'Firmware/BSP/bluetooth.c').as_posix()
    harness = output / 'startup_mcu.c'
    harness.write_text('#define main baseline_test_main\n#include "'+fixture_c+'"\n#undef main\n#include "'+actual_c+'"\n'+r'''
#include <stdlib.h>
static void background(uint32_t ms) {
    at(ms); bluetooth_control_update(1U); bluetooth_main_loop_task();
    if (tx_active) { tx_complete(); bluetooth_main_loop_task(); }
}
int main(int argc, char **argv) {
    unsigned long ms; char wire[129]; uint32_t last = 0U;
    const int before = argc > 1 && atoi(argv[1]) != 0;
    const uint32_t delay = argc > 2 ? (uint32_t)atoi(argv[2]) : 0U;
    reset(); reporting[0] = 0U; reporting[1] = 0U; reporting[2] = 1U;
    while (scanf("%lu %128s", &ms, wire) == 2) {
        uint32_t arrival = (uint32_t)ms, i;
        for (i = last + 1U; i < arrival; ++i) background(i);
        at(arrival); bluetooth_control_update(1U);
        if (before) bluetooth_main_loop_task();
        receive(wire);
        at(arrival + delay); bluetooth_control_update(1U); process();
        if (tx_active) { tx_complete(); bluetooth_main_loop_task(); }
        last = arrival + delay;
        if (blue_front || blue_back || blue_left || blue_right) return 2;
    }
    printf("REPORT %u %u %u STALE %lu INVALID %lu\n", (unsigned)reporting[0],
        (unsigned)reporting[1], (unsigned)reporting[2],
        (unsigned long)bluetooth_link_stats.stale_frames,
        (unsigned long)bluetooth_link_stats.invalid_frames);
    return 0;
}
''', encoding='utf-8')
    compiler = [args.cc] + (['cc'] if Path(args.cc).name.lower() in ('zig','zig.exe') else [])
    exe = output / 'startup_mcu.exe'
    flags = ['-std=c99','-Wall','-Wextra','-Werror','-pedantic']
    if 'clang' in Path(args.cc).name.lower() or Path(args.cc).name.lower() in ('zig','zig.exe'):
        flags += ['-Wno-newline-eof']
    run(compiler + flags + ['-Itests/bluetooth_stubs','-IFirmware/BSP','Firmware/BSP/bluetooth_link.c',harness,'-o',exe], repo)
    results = []
    for name, rows in scenarios.items():
        offsets = (0,) if name == 'legacy_boundary' else (0,20,40)
        delays = (10,) if name == 'legacy_boundary' else (0,10)
        for before in (0,1):
            for offset in offsets:
                for delay in delays:
                    # Vary delivery of the settings relative to the neutral lease,
                    # rather than merely translating every timestamp equally.
                    data = '\n'.join(f'{at + (offset if wire.startswith(("CMD|4|", "CMD|5|", "CMD|6|")) else 0)} {wire}' for at,wire in rows)+'\n'
                    result = run([exe,str(before),str(delay)],repo,data).strip()
                    reporting = list(map(int,result.split()[1:4]))
                    # Safe subscriptions now survive motion epoch changes, including
                    # the former silent-drop regression and slower delivery offsets.
                    expected = [1,1,0]
                    if reporting != expected:
                        raise AssertionError(f'{name}/{before}/{offset}/{delay}: {result}, expected {expected}')
                    results.append({'scenario':name,'mainBeforeRx':bool(before),'uartOffsetMs':offset,
                                    'dispatchDelayMs':delay,'reporting':reporting,'expected':expected,
                                    'subscriptionComplete':reporting==[1,1,0],'result':result})
    report = {'java':trace.splitlines()[-1], 'replays':results,
              'sourceSha256':{str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in actual+[repo/'Firmware/BSP/bluetooth.c',repo/'Firmware/BSP/bluetooth_link.c']},
              'limitations':['Host scheduling model; no radio/vehicle validation','450ms response cases are observations, not continuous-motion acceptance']}
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    incomplete=[r for r in results if r['scenario']!='legacy_boundary' and not r['subscriptionComplete']]
    print(f'Startup subscription replay: PASS ({len(results)} schedules; former epoch-drop and all subscription schedules now [1,1,0]; incomplete={len(incomplete)}; {report["java"]})')
    for result in incomplete:
        print('LIMITATION', result)


if __name__ == '__main__':
    main()
