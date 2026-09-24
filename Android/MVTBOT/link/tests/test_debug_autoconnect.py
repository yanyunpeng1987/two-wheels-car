#!/usr/bin/env python3
"""Exercise the real diagnostic scanner and integration without any device or radio.

All device addresses below are synthetic fixtures, never local target configuration.
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import tempfile

TEST = r'''package com.mvtbot.link;
import android.bluetooth.*;import android.bluetooth.le.*;import android.os.*;import android.content.*;import android.view.*;import java.util.*;
public final class DebugAutoConnectTest {
 static int checks;static final String TARGET="12:34:56:78:9A:BC",OTHER="12:34:56:78:9A:BD";
 static final String CONFIG="enabled=true\ntargetAddress="+TARGET+"\n";
 static final Handler loop=new Handler(Looper.getMainLooper());
 static void check(boolean value,String why){checks++;if(!value)throw new AssertionError(why);}
 static BluetoothDevice device(String address){BluetoothDevice d=new BluetoothDevice("HC-05D",new BluetoothGatt());d.address=address;return d;}
 static final class Client implements DebugAutoConnect.Listener {boolean available=true;int connections;BluetoothDevice connected;public boolean canConnect(){return available;}public void connect(BluetoothDevice d){connections++;connected=d;}}
 static Context context(String config){Context c=new Context();c.assets.config=config;return c;}
 static DebugAutoConnect auto(Context c,Client client){DebugAutoConnect a=new DebugAutoConnect(loop,client);a.bind(c);a.consider();Handler.advance(500);return a;}
 static void lateResultWithoutPumpingTimeout(DebugAutoConnect a,ScanResult result){try{java.lang.reflect.Field token=DebugAutoConnect.class.getDeclaredField("token");token.setAccessible(true);java.lang.reflect.Method found=DebugAutoConnect.class.getDeclaredMethod("found",long.class,ScanResult.class);found.setAccessible(true);found.invoke(a,token.getLong(a),result);}catch(ReflectiveOperationException error){throw new AssertionError(error);}}
 static void unit(){
  for(String config:new String[]{null,"enabled=false\ntargetAddress="+TARGET,"enabled=true\ntargetAddress=bogus","enabled=true\ntargetAddress="+TARGET+"\nextra=oops","enabled=true"}){Context c=context(config);auto(c,new Client());check(c.manager.adapter.scanner.starts==0,"missing/disabled/invalid config never scans");}
  Context c=context(CONFIG);Client client=new Client();client.available=false;DebugAutoConnect a=auto(c,client);check(c.manager.adapter.scanner.starts==0,"wait for all UI conditions without consuming attempt");a.background();client.available=true;a.consider();Handler.advance(500);BluetoothLeScanner scan=c.manager.adapter.scanner;check(scan.starts==1&&scan.filters.size()==1&&scan.filters.get(0).address.equals(TARGET),"one exact platform address filter");ScanCallback old=scan.callback;
  old.onScanResult(1,new ScanResult(device(OTHER)));Handler.advance(0);check(client.connections==0,"same-name wrong-address result rejected even if OS filter returns it");
  BluetoothDevice target=device(TARGET);old.onBatchScanResults(Arrays.asList(null,new ScanResult(device(OTHER)),new ScanResult(target),new ScanResult(target)));Handler.advance(0);check(client.connections==1&&client.connected==target&&scan.stops==1,"batch exact match connects only once after stopping scan");a.consider();Handler.advance(11000);old.onScanResult(1,new ScanResult(target));Handler.advance(0);check(scan.starts==1&&client.connections==1,"late results/timeout/resume do not repeat success");
  c=context(CONFIG);client=new Client();a=auto(c,client);scan=c.manager.adapter.scanner;old=scan.callback;Handler.advance(9999);check(scan.stops==0,"scan stays bounded until deadline");Handler.advance(1);check(scan.stops==1,"10-second scan timeout");old.onScanResult(1,new ScanResult(target));Handler.advance(0);a.consider();check(client.connections==0&&scan.starts==1,"timeout cannot auto-retry or accept stale result");
  c=context(CONFIG);client=new Client();a=auto(c,client);scan=c.manager.adapter.scanner;Handler.now+=10000;check(scan.stops==0,"timeout Runnable has not executed during simulated main-loop stall");lateResultWithoutPumpingTimeout(a,new ScanResult(target));check(client.connections==0&&scan.stops==1&&android.util.Log.lastWarning.contains("after scan deadline"),"absolute deadline rejects late result before timeout Runnable executes");
  for(String cause:new String[]{"manual","destroy","legacy"}){c=context(CONFIG);client=new Client();a=auto(c,client);scan=c.manager.adapter.scanner;old=scan.callback;a.cancel(cause);a.consider();old.onScanResult(1,new ScanResult(target));old.onScanFailed(3);Handler.advance(0);check(scan.starts==1&&scan.stops==1&&client.connections==0,"cancel invalidates result and scan-failure callbacks");}
  c=context(CONFIG);client=new Client();a=auto(c,client);scan=c.manager.adapter.scanner;old=scan.callback;a.background();a.consider();old.onScanResult(1,new ScanResult(target));Handler.advance(0);check(scan.starts==1&&client.connections==0,"background cancels without resume retry");
  c=context(CONFIG);client=new Client();a=auto(c,client);scan=c.manager.adapter.scanner;client.available=false;scan.callback.onScanResult(1,new ScanResult(target));Handler.advance(0);client.available=true;a.consider();check(client.connections==0&&scan.starts==1,"UI readiness checked again at actual result handling");
  for(int failure=0;failure<5;failure++){c=context(CONFIG);client=new Client();scan=c.manager.adapter.scanner;if(failure==0)scan.denied=true;if(failure==1)c.manager.adapter.enabled=false;if(failure==2)c.manager.adapter.scanner=null;if(failure==3)c.manager.adapter=null;if(failure==4)c.manager=null;a=auto(c,client);a.consider();check(client.connections==0&&scan.starts<2,"permission/radio/manager failures cannot retry");}
  c=context(CONFIG);client=new Client();a=auto(c,client);scan=c.manager.adapter.scanner;old=scan.callback;old.onScanFailed(2);Handler.advance(0);a.consider();check(scan.starts==1&&scan.stops==1&&client.connections==0,"asynchronous scan failure consumes attempt");
  c=context(CONFIG);client=new Client();a=auto(c,client);scan=c.manager.adapter.scanner;old=scan.callback;scan.stopDenied=true;a.cancel("cleanup denied");old.onScanResult(1,new ScanResult(target));Handler.advance(0);check(client.connections==0,"failed platform cleanup still rejects callbacks");
  c=context(CONFIG);client=new Client();a=new DebugAutoConnect(loop,client);a.cancel("user disconnect before scan");a.bind(c);a.consider();check(c.manager.adapter.scanner.starts==0,"manual disconnect before readiness suppresses scan");
  for(String cause:new String[]{"manual","destroy","legacy"}){c=context(CONFIG);client=new Client();a=new DebugAutoConnect(loop,client);a.bind(c);a.consider();Handler.advance(100);a.cancel(cause);a.consider();Handler.advance(1000);check(c.manager.adapter.scanner.starts==0,"explicit cancel during foreground wait consumes attempt");}
  c=context(CONFIG);client=new Client();a=new DebugAutoConnect(loop,client);a.bind(c);a.consider();Handler.advance(100);a.consider();Handler.advance(399);check(c.manager.adapter.scanner.starts==0,"repeated readiness cannot start early");Handler.advance(1);check(c.manager.adapter.scanner.starts==1,"repeated readiness does not postpone stable scan");a.cancel("test finished");
  for(String message:android.util.Log.messages)check(!message.contains(TARGET)&&!message.contains(OTHER),"logs omit target addresses");
 }
 static UUID u(String s){return UUID.fromString("0000"+s+"-0000-1000-8000-00805f9b34fb");}
 static void integration(String mode){
  Context c=context(mode.equals("normal")?null:CONFIG);Handler ui=new Handler(m->{MiniBalanLink.acceptUiEvent(m);return true;});
  MiniBalanLink.selectMini();MiniBalanLink.foreground(true);check(c.manager.adapter.scanner.starts==0,"selection/resume before binding cannot scan");MiniBalanLink.bind(c,ui);
  BluetoothLeScanner scan=c.manager.adapter.scanner;
  if(mode.equals("normal")){Handler.advance(1000);check(scan.starts==0,"normal build has no auto-connect");return;}
  if(mode.equals("rotation")){
   Handler.advance(100);MiniBalanLink.foreground(false);Handler.advance(120);MiniBalanLink.foreground(true);Handler.advance(280);check(scan.starts==0,"original500ms timer cannot start after100ms pause/220ms resume");Handler.advance(219);check(scan.starts==0,"new foreground must remain stable for full500ms");Handler.advance(1);check(scan.starts==1,"coldlaunch rotation preserves attempt and starts at720ms");MiniBalanLink.foreground(true);Handler.advance(100);check(scan.starts==1,"further resume does not duplicate active scan");MiniBalanLink.destroy();return;
  }
  if(mode.equals("pending-stop")){Handler.advance(100);MiniBalanLink.stop();MiniBalanLink.foreground(false);MiniBalanLink.foreground(true);Handler.advance(1000);check(scan.starts==0,"manual disconnect during stable-wait consumes attempt");return;}
  if(mode.equals("pending-manual")){Handler.advance(100);BluetoothDevice manual=device(OTHER);MiniBalanLink.connect(manual);Handler.advance(1000);check(manual.connects==1&&scan.starts==0,"manual connection during stable-wait wins without scanning");MiniBalanLink.destroy();return;}
  Handler.advance(499);check(scan.starts==0,"no scan before500ms stable foreground");Handler.advance(1);
  check(scan.starts==1,"existing bind/select/foreground hooks start debug scan");ScanCallback old=scan.callback;BluetoothDevice target=device(TARGET);
  if(mode.equals("manual")){BluetoothDevice manual=device(OTHER);MiniBalanLink.connect(manual);old.onScanResult(1,new ScanResult(target));Handler.advance(0);check(manual.connects==1&&target.connects==0&&scan.stops==1,"manual connection wins over automatic scan");}
  else if(mode.equals("stop")){MiniBalanLink.stop();old.onScanResult(1,new ScanResult(target));Handler.advance(0);MiniBalanLink.foreground(true);MiniBalanLink.selectMini();check(target.connects==0&&scan.starts==1,"manual disconnect prevents later automatic connect");}
  else if(mode.equals("pause")){MiniBalanLink.foreground(false);old.onScanResult(1,new ScanResult(target));Handler.advance(0);MiniBalanLink.foreground(true);check(target.connects==0&&scan.starts==1,"pause cancels pending scan and does not retry");}
  else if(mode.equals("destroy")){MiniBalanLink.destroy();old.onScanResult(1,new ScanResult(target));Handler.advance(0);MiniBalanLink.bind(c,ui);MiniBalanLink.selectMini();MiniBalanLink.foreground(true);check(target.connects==0&&scan.starts==1,"Activity recreation cannot restart process attempt");}
  else {
   BluetoothGatt g=target.fake;BluetoothGattService s=new BluetoothGattService(u("ffe0"));s.add(new BluetoothGattCharacteristic(u("ffe1"),24,true));g.services.add(s);g.vendorLengthBug=true;g.autoNotify=true;
   BluetoothGattService dis=new BluetoothGattService(u("180a"));String[] ids={"2a29","2a24","2a26"},values={"QUALCOMM","HC-05D","1.1.4"};for(int i=0;i<3;i++){BluetoothGattCharacteristic d=new BluetoothGattCharacteristic(u(ids[i]),2,false);d.setValue(values[i].getBytes(java.nio.charset.StandardCharsets.US_ASCII));dis.add(d);}g.services.add(dis);
   old.onScanResult(1,new ScanResult(target));old.onScanResult(1,new ScanResult(target));Handler.advance(0);check(target.connects==1&&scan.stops==1&&!MiniBalanLink.isReady(),"actual scan observation enters GATT once without claiming ready");g.discover();g.completeDescriptor(0);Handler.advance(400);
   check(MiniBalanLink.isReady()&&g.writes.subList(0,4).equals(Arrays.asList("CMD|3|0|0|$","CMD|7|$","CMD|3|0|0|$","CMD|3|0|0|$")),"debug path preserves complete qualified handshake and startup zeros");
   check(!g.writes.contains("CMD|3|1|0|$"),"no previous motion replay on automatic connection");MiniBalanLink.stop();Handler.advance(250);old.onScanResult(1,new ScanResult(target));Handler.advance(0);MiniBalanLink.foreground(true);check(target.connects==1&&scan.starts==1&&!MiniBalanLink.isReady(),"disconnect does not trigger auto-reconnection");
  }
  MiniBalanLink.destroy();
 }
 public static void main(String[] args){if(args[0].equals("unit"))unit();else integration(args[0]);System.out.println("Debug auto-connect "+args[0]+": PASS ("+checks+" assertions)");}
}'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path, required=True)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location('ble_fixture', here / 'test_ble_session.py')
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    suffix = '.exe' if (args.java_home / 'bin/javac.exe').exists() else ''
    with tempfile.TemporaryDirectory(prefix='mvtbot-debug-auto-') as temp:
        root = Path(temp)
        sources = []
        for name, source in fixture.STUBS.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding='utf-8')
            sources.append(path)
        test = root / 'com/mvtbot/link/DebugAutoConnectTest.java'
        test.parent.mkdir(parents=True, exist_ok=True)
        test.write_text(TEST, encoding='utf-8')
        sources += sorted((here.parent / 'src').rglob('*.java')) + [test]
        classes = root / 'classes'
        classes.mkdir()
        subprocess.run([str(args.java_home / ('bin/javac'+suffix)), '-encoding', 'UTF-8', '-d', str(classes), *map(str, sources)], check=True)
        for mode in ('unit', 'normal', 'success', 'manual', 'stop', 'pause', 'destroy', 'rotation', 'pending-stop', 'pending-manual'):
            subprocess.run([str(args.java_home / ('bin/java'+suffix)), '-cp', str(classes), 'com.mvtbot.link.DebugAutoConnectTest', mode], check=True)


if __name__ == '__main__':
    main()
