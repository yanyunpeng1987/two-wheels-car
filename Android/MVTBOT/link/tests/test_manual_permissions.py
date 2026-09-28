#!/usr/bin/env python3
"""Replay the actual Mini permission gate and scanner together, without a device."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import tempfile

TEST = r'''package com.mvtbot.link;
import android.app.*;import android.os.*;import android.bluetooth.*;import android.widget.*;import android.view.*;import java.util.*;
public final class ManualPermissionsTest {
 static int checks;
 static final String SCAN="android.permission.BLUETOOTH_SCAN",CONNECT="android.permission.BLUETOOTH_CONNECT",ADVERTISE="android.permission.BLUETOOTH_ADVERTISE",FINE="android.permission.ACCESS_FINE_LOCATION",COARSE="android.permission.ACCESS_COARSE_LOCATION";
 static void check(boolean value,String reason){checks++;if(!value)throw new AssertionError(reason);}
 static Activity activity(String...permissions){Activity a=new Activity();a.granted.addAll(Arrays.asList(permissions));return a;}
 static void matrix(){
  for(int sdk:new int[]{31,33,36})for(int granted=0;granted<4;granted++){
   Build.VERSION.SDK_INT=sdk;Activity a=activity();if((granted&1)!=0)a.granted.add(SCAN);if((granted&2)!=0)a.granted.add(CONNECT);
   boolean allowed=ManualBlePermissions.ensure(a);check(allowed==(granted==3),"all Nearby permissions required before picker on API"+sdk);
   check(a.checked.equals(Arrays.asList(SCAN,CONNECT)),"Android12+ never checks location/advertise");
   if(granted==3)check(a.requested.isEmpty(),"already granted does not launch GrantPermissionsActivity");
   else{List<String> missing=new ArrayList<>();if((granted&1)==0)missing.add(SCAN);if((granted&2)==0)missing.add(CONNECT);check(a.requested.size()==1&&Arrays.asList(a.requested.get(0)).equals(missing)&&a.requestCode==1,"request only missing items with existing callback code");}
  }
  for(int sdk:new int[]{23,28,30}){
   Build.VERSION.SDK_INT=sdk;Activity a=activity(COARSE);check(!ManualBlePermissions.ensure(a),"coarse-only does not grant precise BLE scan on old Android");
   check(a.checked.equals(Arrays.asList(FINE))&&a.requested.size()==1&&Arrays.equals(a.requested.get(0),new String[]{FINE}),"legacy Android requests Fine only");
   a=activity(FINE);check(ManualBlePermissions.ensure(a)&&a.requested.isEmpty(),"Fine grant sufficient without redundant Coarse request");
  }
  Build.VERSION.SDK_INT=22;Activity a=activity();check(ManualBlePermissions.ensure(a)&&a.checked.isEmpty()&&a.requested.isEmpty(),"pre-runtime Android does not invoke runtime permission API");
  check(!ManualBlePermissions.ensure(null),"missing Activity fails closed");
 }
 static void transactions(){
  Build.VERSION.SDK_INT=36;Activity a=activity(SCAN);a.grantOnRequest=true;
  check(!ManualBlePermissions.ensure(a)&&a.granted.contains(CONNECT),"even immediate grant during request cannot open picker this transaction");
  check(ManualBlePermissions.ensure(a)&&a.requested.size()==1,"next explicit click proceeds without another permission request");
  a=activity();a.checkDenied=true;check(!ManualBlePermissions.ensure(a)&&a.requested.isEmpty(),"permission check exception fails closed");
  a=activity();a.requestDenied=true;check(!ManualBlePermissions.ensure(a),"request exception fails closed");
  a=activity();Toast.denied=true;check(!ManualBlePermissions.ensure(a)&&a.requested.size()==1,"hint failure cannot bypass permission gate");Toast.denied=false;
  Locale.setDefault(Locale.SIMPLIFIED_CHINESE);a=activity();ManualBlePermissions.ensure(a);check(Toast.shown.get(Toast.shown.size()-1).contains("再次点击蓝牙"),"missing permission explains explicit next click");
 }
 static void joined(){
  Build.VERSION.SDK_INT=36;Activity granted=activity(SCAN,CONNECT);BluetoothAdapter adapter=granted.manager.adapter;
  PopupWindow popup=new PopupWindow();TextView title=new TextView();View progress=new View();
  int[] pauses={0},results={0};granted.onRequest=()->{pauses[0]++;MiniBalanLink.foreground(false);};
  BluetoothAdapter.LeScanCallback sink=(d,r,b)->results[0]++;
  for(int i=0;i<3;i++)if(ManualBlePermissions.ensure(granted))ManualBleScanner.start(popup,adapter,sink,title,progress);
  check(granted.requested.isEmpty()&&pauses[0]==0&&adapter.scanner.starts==1&&adapter.scanner.stops==0,"granted Nearby with denied location preserves one live picker without permission pause");
  MiniBalanLink.foreground(false);check(adapter.scanner.stops==1,"genuine background still cancels scanner");
  Handler.advance(31000);Activity missing=activity(SCAN);missing.grantOnRequest=true;missing.onRequest=()->{pauses[0]++;MiniBalanLink.foreground(false);};
  if(ManualBlePermissions.ensure(missing))ManualBleScanner.start(popup,missing.manager.adapter,sink,title,progress);
  check(pauses[0]==1&&missing.manager.adapter.scanner.starts==0,"missing permission request never races scanner creation");
  Handler.advance(1000);check(missing.manager.adapter.scanner.starts==0,"permission completion/resume has no automatic scan");
  if(ManualBlePermissions.ensure(missing))ManualBleScanner.start(popup,missing.manager.adapter,sink,title,progress);
  check(missing.requested.size()==1&&missing.manager.adapter.scanner.starts==1,"new user click after grant starts scan once");ManualBleScanner.cancel("test finished");
 }
 public static void main(String[] args){Locale.setDefault(Locale.ENGLISH);matrix();transactions();joined();System.out.println("Manual BLE permissions: PASS ("+checks+" checks)");}
}'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path, required=True)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location('permissions_android_fakes', here / 'test_ble_session.py')
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    suffix = '.exe' if (args.java_home / 'bin/javac.exe').exists() else ''
    with tempfile.TemporaryDirectory(prefix='mvtbot-permissions-') as temp:
        root = Path(temp)
        for name, source in fixture.STUBS.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding='utf-8')
        test = root / 'com/mvtbot/link/ManualPermissionsTest.java'
        test.parent.mkdir(parents=True, exist_ok=True)
        test.write_text(TEST, encoding='utf-8')
        classes = root / 'classes'
        classes.mkdir()
        sources = [*root.rglob('*.java'), *fixture.transport_sources(here.parent)]
        subprocess.run([str(args.java_home / ('bin/javac' + suffix)), '-encoding', 'UTF-8', '-d', str(classes), *map(str, sources)], check=True)
        subprocess.run([str(args.java_home / ('bin/java' + suffix)), '-cp', str(classes), 'com.mvtbot.link.ManualPermissionsTest'], check=True)


if __name__ == '__main__':
    main()
