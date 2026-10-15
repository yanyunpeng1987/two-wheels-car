#!/usr/bin/env python3
"""Replay the real manual scanner with Android fakes; no radio or device access."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import tempfile

TEST = r'''package com.mvtbot.link;
import android.bluetooth.*;import android.bluetooth.le.*;import android.os.*;import android.view.*;import android.widget.*;import java.util.*;
public final class ManualScannerTest {
 static int checks,results;static final BluetoothAdapter adapter=new BluetoothAdapter();
 static final BluetoothLeScanner scan=adapter.scanner;static final PopupWindow owner=new PopupWindow();
 static final TextView title=new TextView();static final View progress=new View();
 static final BluetoothDevice device=new BluetoothDevice("PRIVATE-NAME",new BluetoothGatt());
 static final BluetoothAdapter.LeScanCallback sink=(d,r,b)->{check(d==device,"result identity preserved without name filter");check(r==-60,"RSSI forwarded");results++;};
 static void check(boolean value,String why){checks++;if(!value)throw new AssertionError(why);}
 static void start(){ManualBleScanner.start(owner,adapter,sink,title,progress);}
 static void stop(){ManualBleScanner.stop(owner);}
 static ScanResult result(){return new ScanResult(device);}
 static void lifetime(){
  owner.showing=false;start();check(scan.starts==0,"constructor/hidden picker never scans");owner.showing=true;
  start();ScanCallback old=scan.callback;check(scan.starts==1&&title.resource==0x7f0f01fe&&progress.visibility==0,"one shown picker scan and visible progress");
  start();check(scan.starts==1,"duplicate show/start is idempotent");check(scan.filters==null,"no remembered target or fixed address filter");
  old.onScanResult(1,result());old.onBatchScanResults(Arrays.asList(null,result(),new ScanResult(null)));Handler.advance(0);check(results==2,"single/batch/null callbacks");
  stop();stop();check(scan.stops==1&&progress.visibility==8&&title.resource==0x7f0f01fa,"idempotent stop clears UI");
  old.onScanResult(1,result());old.onScanFailed(6);Handler.advance(0);check(results==2,"late result/failure ignored");
  Handler.advance(6500);PopupWindow second=new PopupWindow();ManualBleScanner.start(second,adapter,sink,title,progress);ScanCallback current=scan.callback;
  stop();check(scan.stops==1,"old owner cannot stop new picker");current.onScanResult(1,result());Handler.advance(0);check(results==3,"new owner still receives results");
  ManualBleScanner.cancel("background");check(scan.stops==2,"lifecycle cancels current owner");current.onScanResult(1,result());Handler.advance(0);check(results==3,"background invalidates callback");
  Handler.advance(6500);start();scan.stopDenied=true;old=scan.callback;ManualBleScanner.cancel("destroy");old.onScanResult(1,result());Handler.advance(0);check(results==3&&progress.visibility==8,"cleanup exception cannot revive scanner");
 }
 static void timeouts()throws Exception{
  start();ScanCallback first=scan.callback;Handler.advance(6000);stop();Handler.advance(500);start();check(scan.starts==2,"explicit restart accepted after minimum interval");
  Handler.advance(3500);check(scan.stops==1,"cancelled first timer cannot stop restarted scan");first.onScanResult(1,result());Handler.advance(0);check(results==0,"old callback cannot populate new scan");
  Handler.advance(6499);check(scan.stops==1,"new scan keeps its own full duration");Handler.advance(1);check(scan.stops==2,"new bounded scan stops at own deadline");
  start();check(scan.starts==3,"timeout permits a new explicit scan");
  java.lang.reflect.Field f=ManualBleScanner.class.getDeclaredField("active");f.setAccessible(true);Object session=f.get(null);
  java.lang.reflect.Method found=ManualBleScanner.class.getDeclaredMethod("found",session.getClass(),ScanResult.class);found.setAccessible(true);
  Handler.now+=10000;found.invoke(null,session,result());check(scan.stops==3&&results==0,"absolute deadline survives a stalled main loop");
 }
 static void throttle(){
  for(int i=0;i<4;i++){if(i!=0)Handler.advance(6500);start();stop();}check(scan.starts==4,"four spaced explicit requests allowed");
  Handler.advance(6500);start();check(scan.starts==4&&title.text.contains("Wait 4s"),"rolling quota reports bounded cooldown without platform request");
  Handler.advance(4000);check(scan.starts==4,"cooldown never automatically restarts");start();check(scan.starts==5,"new user request succeeds after rolling window");stop();
  start();check(scan.starts==5&&title.text.contains("Wait 7s"),"rapid repeated search is throttled visibly");Handler.advance(100000);check(scan.starts==5,"no retry loop or stale pending timeout");
 }
 static void failures(){
  start();ScanCallback old=scan.callback;old.onScanFailed(2);Handler.advance(0);check(scan.stops==1&&title.text.contains("(2)")&&progress.visibility==8,"registration error stops and is visible");
  start();check(scan.starts==1&&title.text.contains("Wait"),"failure cooldown refuses immediate retry");Handler.advance(6500);start();old=scan.callback;old.onScanFailed(6);Handler.advance(0);
  check(title.text.contains("(6)")&&title.text.contains("30s"),"platform too-frequent error has explicit 30-second cooldown");Handler.advance(29999);start();check(scan.starts==2,"no early retry after platform throttling");Handler.advance(1);start();check(scan.starts==3,"cooldown expires without process restart");stop();
  Handler.advance(6500);adapter.enabled=false;start();check(scan.starts==3&&title.text.contains("Bluetooth unavailable"),"disabled adapter visible without platform scan");adapter.enabled=true;
  scan.denied=true;start();check(scan.starts==4&&title.text.contains("permissions")&&progress.visibility==8,"start permission exception closes session and reports error");scan.denied=false;
  Handler.advance(6500);ManualBleScanner.start(owner,adapter,(d,r,b)->{throw new IllegalStateException();},title,progress);scan.callback.onScanResult(1,result());Handler.advance(0);check(title.text.contains("Device list unavailable"),"bad UI sink cannot crash callback or leave scan active");
  Handler.advance(6500);Locale.setDefault(Locale.SIMPLIFIED_CHINESE);start();scan.callback.onScanFailed(5);Handler.advance(0);check(title.text.contains("扫描失败")&&title.text.contains("（5）"),"Chinese failure is visible");
 }
 static void marshal(){
  Looper.worker=true;start();check(scan.starts==0,"worker request marshalled to main");Looper.worker=false;Handler.advance(0);check(scan.starts==1,"main executes queued start");
  ScanCallback old=scan.callback;Looper.worker=true;ManualBleScanner.cancel("worker cancel");check(scan.stops==0,"worker cancellation queued");Looper.worker=false;Handler.advance(0);check(scan.stops==1,"main processes cancellation");
  old.onScanResult(1,result());Handler.advance(0);check(results==0,"cancelled worker scan cannot deliver later");
  Handler.advance(6500);start();owner.showing=false;scan.callback.onScanResult(1,result());Handler.advance(0);check(scan.stops==2&&results==0,"hidden picker rejected even if dismissal hook is delayed");
 }
 public static void main(String[] args)throws Exception{Locale.setDefault(Locale.ENGLISH);switch(args[0]){case "lifetime":lifetime();break;case "timeouts":timeouts();break;case "throttle":throttle();break;case "failures":failures();break;case "marshal":marshal();break;default:throw new AssertionError();}for(String s:android.util.Log.messages)check(!s.contains("PRIVATE-NAME")&&!s.contains(device.address),"logs omit names/addresses");System.out.println("Manual scanner "+args[0]+": PASS ("+checks+" checks)");}
}'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--java-home', type=Path, required=True)
    args = parser.parse_args()
    module = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location('ble_session_fakes', Path(__file__).with_name('test_ble_session.py'))
    base = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(base)
    stubs = dict(base.STUBS)
    stubs['android/os/Looper.java'] = 'package android.os; public class Looper {static final Looper MAIN=new Looper();public static boolean worker;public static Looper getMainLooper(){return MAIN;}public static Looper myLooper(){return worker?null:MAIN;}}'
    stubs['android/view/View.java'] = 'package android.view;public class View {public static final int VISIBLE=0,GONE=8;public boolean shown=true;public int visibility;public void setVisibility(int v){visibility=v;}public boolean isShown(){return shown;}public int getWindowVisibility(){return 0;}}'
    stubs['android/widget/TextView.java'] = 'package android.widget;public class TextView extends android.view.View {public String text="";public int resource;public void setText(int r){resource=r;text="";}public void setText(CharSequence s){text=s.toString();resource=0;}}'
    stubs['android/widget/PopupWindow.java'] = 'package android.widget;public class PopupWindow {public boolean showing=true;public boolean isShowing(){return showing;}}'
    stubs['android/bluetooth/BluetoothAdapter.java'] = stubs['android/bluetooth/BluetoothAdapter.java'].replace('public class BluetoothAdapter {', 'public class BluetoothAdapter {public interface LeScanCallback {void onLeScan(BluetoothDevice d,int rssi,byte[] data);}') if 'interface LeScanCallback' not in stubs['android/bluetooth/BluetoothAdapter.java'] else stubs['android/bluetooth/BluetoothAdapter.java']
    stubs['android/bluetooth/le/ScanRecord.java'] = 'package android.bluetooth.le;public class ScanRecord {public byte[] getBytes(){return new byte[]{1};}}'
    stubs['android/bluetooth/le/ScanResult.java'] = 'package android.bluetooth.le;import android.bluetooth.*;public class ScanResult {private BluetoothDevice device;public ScanResult(BluetoothDevice d){device=d;}public BluetoothDevice getDevice(){return device;}public int getRssi(){return -60;}public ScanRecord getScanRecord(){return null;}}'
    suffix = '.exe' if (args.java_home / 'bin/javac.exe').exists() else ''
    with tempfile.TemporaryDirectory(prefix='mvtbot-manual-scanner-') as temp:
        root = Path(temp)
        for relative, source in stubs.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding='utf-8')
        test = root / 'com/mvtbot/link/ManualScannerTest.java'
        test.parent.mkdir(parents=True, exist_ok=True)
        test.write_text(TEST, encoding='utf-8')
        sources = [*root.rglob('*.java'), module / 'src/com/mvtbot/link/ManualBleScanner.java']
        classes = root / 'classes'
        classes.mkdir()
        subprocess.run([str(args.java_home / ('bin/javac' + suffix)), '-encoding', 'UTF-8', '-d', str(classes), *map(str, sources)], check=True)
        for mode in ('lifetime', 'timeouts', 'throttle', 'failures', 'marshal'):
            subprocess.run([str(args.java_home / ('bin/java' + suffix)), '-cp', str(classes), 'com.mvtbot.link.ManualScannerTest', mode], check=True)


if __name__ == '__main__':
    main()
