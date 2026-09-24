#!/usr/bin/env python3
"""Host replay of the actual MiniBalanLink class against deterministic Android fakes.

This is a state/queue contract test, not evidence of Android radio or UI acceptance.
Run with --java-home pointing to JDK 21 (no device, network or APK installation).
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

STUBS = {
"android/os/Looper.java": '''package android.os; public class Looper { static final Looper MAIN=new Looper(); public static Looper getMainLooper(){return MAIN;} public static Looper myLooper(){return MAIN;} }''',
"android/os/Message.java": '''package android.os; public class Message { public int what,arg1,arg2; public Object obj; public void recycle(){} }''',
"android/os/Handler.java": '''package android.os;
import java.util.*;
public class Handler {
 public interface Callback { boolean handleMessage(Message m); }
 private Callback callback; public static long now; private static long seq;
 private static final List<Task> tasks=new ArrayList<>();
 static class Task { Handler h; Runnable r; long time,order; Task(Handler h,Runnable r,long t){this.h=h;this.r=r;time=t;order=seq++;} }
 public Handler(Looper l){} public Handler(Callback c){callback=c;}
 public boolean post(Runnable r){return postDelayed(r,0);}
 public boolean postDelayed(Runnable r,long d){tasks.add(new Task(this,r,now+d));return true;}
 public void removeCallbacks(Runnable r){tasks.removeIf(t->t.h==this&&t.r==r);}
 public Message obtainMessage(int what,Object obj){Message m=new Message();m.what=what;m.obj=obj;return m;}
 public void dispatchMessage(Message m){if(callback!=null)callback.handleMessage(m);}
 public static void advance(long ms){long end=now+ms;int guard=0;while(true){Task first=null;for(Task t:tasks)if(t.time<=end&&(first==null||t.time<first.time||(t.time==first.time&&t.order<first.order)))first=t;if(first==null)break;if(++guard>20000)throw new AssertionError("runaway tasks");tasks.remove(first);now=first.time;first.r.run();}now=end;}
}''',
"android/os/SystemClock.java": '''package android.os; public class SystemClock { public static long uptimeMillis(){return Handler.now;} }''',
"android/content/BroadcastReceiver.java": '''package android.content; public class BroadcastReceiver {}''',
"android/content/Context.java": '''package android.content; public class Context { public int unregisters; public Context getApplicationContext(){return this;} public void unregisterReceiver(BroadcastReceiver r){if(++unregisters>1)throw new IllegalArgumentException();} }''',
"android/util/Log.java": '''package android.util; public class Log { public static String lastWarning=""; public static int i(String t,String s){return 0;} public static int d(String t,String s){return 0;} public static int w(String t,String s){lastWarning=s;return 0;} public static int e(String t,String s,Throwable x){return 0;} }''',
"android/view/View.java": '''package android.view; public class View { public static final int VISIBLE=0; public boolean shown=true; public boolean isShown(){return shown;} public int getWindowVisibility(){return 0;} }''',
"android/bluetooth/BluetoothProfile.java": '''package android.bluetooth; public interface BluetoothProfile { int STATE_DISCONNECTED=0, STATE_CONNECTED=2; }''',
"android/bluetooth/BluetoothGattCallback.java": '''package android.bluetooth; public class BluetoothGattCallback { public void onConnectionStateChange(BluetoothGatt g,int s,int n){} public void onServicesDiscovered(BluetoothGatt g,int s){} public void onDescriptorWrite(BluetoothGatt g,BluetoothGattDescriptor d,int s){} public void onCharacteristicChanged(BluetoothGatt g,BluetoothGattCharacteristic c){} public void onCharacteristicChanged(BluetoothGatt g,BluetoothGattCharacteristic c,byte[] b){} public void onCharacteristicWrite(BluetoothGatt g,BluetoothGattCharacteristic c,int s){} }''',
"android/bluetooth/BluetoothGattDescriptor.java": '''package android.bluetooth; import java.util.*; public class BluetoothGattDescriptor { public static final byte[] ENABLE_NOTIFICATION_VALUE={1,0}; public byte[] value; public void setValue(byte[] b){value=b.clone();} }''',
"android/bluetooth/BluetoothGattCharacteristic.java": '''package android.bluetooth; import java.util.*;
public class BluetoothGattCharacteristic {
 public static final int PROPERTY_WRITE_NO_RESPONSE=4,PROPERTY_WRITE=8,PROPERTY_NOTIFY=16,WRITE_TYPE_NO_RESPONSE=1,WRITE_TYPE_DEFAULT=2;
 private final UUID id; private final int properties; public int writeType; private byte[] value; public BluetoothGattDescriptor descriptor; BluetoothGattService service;
 public BluetoothGattCharacteristic(UUID u,int p,boolean cccd){id=u;properties=p;descriptor=cccd?new BluetoothGattDescriptor():null;}
 public UUID getUuid(){return id;}public int getProperties(){return properties;}public BluetoothGattDescriptor getDescriptor(UUID u){return descriptor;}public BluetoothGattService getService(){return service;}
 public byte[] getValue(){return value;}public boolean setValue(byte[] b){value=b.clone();return true;}public void setWriteType(int t){writeType=t;}
}''',
"android/bluetooth/BluetoothGattService.java": '''package android.bluetooth;import java.util.*;public class BluetoothGattService { private UUID id; private List<BluetoothGattCharacteristic> cs=new ArrayList<>();public BluetoothGattService(UUID id){this.id=id;}public UUID getUuid(){return id;}public List<BluetoothGattCharacteristic> getCharacteristics(){return cs;}public void add(BluetoothGattCharacteristic c){c.service=this;cs.add(c);} }''',
"android/bluetooth/BluetoothGatt.java": '''package android.bluetooth;import java.util.*;import android.os.*;public class BluetoothGatt {
 public static final int GATT_SUCCESS=0; public BluetoothGattCallback callback;public List<BluetoothGattService> services=new ArrayList<>();public List<String> writes=new ArrayList<>();public List<Long> writeTimes=new ArrayList<>();public List<Integer> types=new ArrayList<>();public BluetoothGattDescriptor pendingDescriptor;public BluetoothGattCharacteristic lastWrite;public boolean closed,autoWrite=true,rejectWrite,denyWrite;
 public List<BluetoothGattService> getServices(){return services;}public boolean discoverServices(){return true;}public boolean setCharacteristicNotification(BluetoothGattCharacteristic c,boolean b){return true;}public boolean writeDescriptor(BluetoothGattDescriptor d){pendingDescriptor=d;return true;}
 public boolean writeCharacteristic(BluetoothGattCharacteristic c){if(denyWrite)throw new SecurityException();if(rejectWrite)return false;lastWrite=c;writes.add(new String(c.getValue(),java.nio.charset.StandardCharsets.US_ASCII));writeTimes.add(Handler.now);types.add(c.writeType);if(autoWrite)new Handler(Looper.getMainLooper()).post(()->callback.onCharacteristicWrite(this,c,0));return true;}
 public void disconnect(){}public void close(){closed=true;}public void completeDescriptor(int status){callback.onDescriptorWrite(this,pendingDescriptor,status);}public void discover(){callback.onConnectionStateChange(this,0,2);Handler.advance(0);callback.onServicesDiscovered(this,0);Handler.advance(0);}
}''',
"android/bluetooth/BluetoothDevice.java": '''package android.bluetooth;import android.content.*;public class BluetoothDevice { public static final int TRANSPORT_LE=2;public BluetoothGatt fake;private String name;public BluetoothDevice(String n,BluetoothGatt g){name=n;fake=g;}public String getName(){return name;}public BluetoothGatt connectGatt(Context c,boolean a,BluetoothGattCallback cb,int transport){if(a||transport!=TRANSPORT_LE)throw new AssertionError("not explicit BLE connection");fake.callback=cb;return fake;} }''',
}

TEST = r'''package com.mvtbot.link;
import android.bluetooth.*;import android.os.*;import android.content.*;import android.view.*;import java.util.*;import java.nio.charset.StandardCharsets;
public final class BleSessionTest {
 static int checks;static List<String> frames=new ArrayList<>();static Context ctx=new Context();static View view=new View();static int resets;
 static MiniBalanLink.ControlReset owner=()->resets++;static Message lastTrusted;
 static Handler ui=new Handler(m->{if(MiniBalanLink.acceptUiEvent(m)){lastTrusted=m;if(m.what==103)frames.add((String)m.obj);}return true;});
 static UUID u(String s){return UUID.fromString("0000"+s+"-0000-1000-8000-00805f9b34fb");}
 static void check(boolean b,String s){checks++;if(!b)throw new AssertionError(s);}
 static BluetoothGattCharacteristic characteristic(int p,boolean desc){return new BluetoothGattCharacteristic(u("ffe1"),p,desc);}
 static BluetoothGatt make(String service,int props){BluetoothGatt g=new BluetoothGatt();BluetoothGattService s=new BluetoothGattService(u(service));s.add(characteristic(props,true));g.services.add(s);return g;}
 static void open(BluetoothGatt g){MiniBalanLink.destroy();MiniBalanLink.bind(ctx,ui);view.shown=true;MiniBalanLink.bindControl(owner,view);MiniBalanLink.foreground(true);check(MiniBalanLink.connect(new BluetoothDevice("HC-05D",g)),"connect accepted");Handler.advance(0);g.discover();}
 static void ready(BluetoothGatt g){open(g);check(!MiniBalanLink.isReady(),"not ready before CCCD");g.completeDescriptor(0);Handler.advance(60);check(MiniBalanLink.isReady(),"ready after CCCD");}
 static void receive(BluetoothGatt g,String text){BluetoothGattCharacteristic c=g.services.get(0).getCharacteristics().get(0);g.callback.onCharacteristicChanged(g,c,text.getBytes(StandardCharsets.US_ASCII));Handler.advance(0);}
 public static void main(String[] args) throws Exception {
  BluetoothGatt g=make("e0ff",20);ready(g);check(g.types.get(0)==1,"WWR preferred");check(g.writes.get(0).equals("CMD|3|0|0|$"),"first zero");
  receive(g,"CMD|4|1|");check(frames.isEmpty(),"partial withheld");receive(g,"2|$CMD|5|23|$");check(frames.size()==2,"fragmented and glued frames decoded");receive(g,"CMD|4|-1\u0000|3|$");check(frames.size()==2,"NUL rejected");check(android.util.Log.lastWarning.contains("raw=")&&android.util.Log.lastWarning.contains("00")&&android.util.Log.lastWarning.contains("count="),"raw NUL diagnostics available");receive(g,"CMD|4|1|2$");check(frames.get(frames.size()-1).equals("CMD|4|1|2|$"),"canonical legacy presentation separator");
  check(!MiniBalanLink.send("CMD|3|1|0|$"),"generic path cannot arm motion");
  int before=g.writes.size();MiniBalanLink.motion(owner,1,0);Handler.advance(90);check(g.writes.get(before).equals("CMD|3|0|0|$"),"first arm zero barrier");check(!g.writes.contains("CMD|3|1|0|$"),"motion withheld between arm zeros");Handler.advance(100);check(g.writes.get(before+1).equals("CMD|3|0|0|$"),"second independent arm zero");check(g.writeTimes.get(before+1)-g.writeTimes.get(before)>=100,"arm zeros separated by at least 100ms");check(g.writes.contains("CMD|3|1|0|$"),"fresh motion after both zeros");
  MiniBalanLink.release();Handler.advance(100);check(g.writes.get(g.writes.size()-1).equals("CMD|3|0|0|$"),"release zero");int stopped=g.writes.size();Handler.advance(600);check(g.writes.size()==stopped,"release stops heartbeat");
  MiniBalanLink.motion(owner,-1,0);Handler.advance(200);view.shown=false;Handler.advance(150);check(g.writes.get(g.writes.size()-1).equals("CMD|3|0|0|$"),"hidden view zero");stopped=g.writes.size();Handler.advance(300);check(g.writes.size()==stopped,"hidden heartbeat stopped");
  BluetoothGatt old=g;g=make("ffe0",28);ready(g);check(g.types.get(0)==1,"original FFE0 supports WWR");int count=frames.size();receive(old,"CMD|5|99|$");check(frames.size()==count,"stale GATT notification ignored");Handler.advance(200);check(g.writes.size()==1,"reconnect does not replay motion");
  MiniBalanLink.foreground(false);MiniBalanLink.motion(owner,1,0);Handler.advance(200);check(g.writes.size()==1,"background cannot arm");MiniBalanLink.foreground(true);
  // Consecutive neutral samples cannot toggle the re-arm gate back on.
  MiniBalanLink.controlMode(owner,1);check(MiniBalanLink.gravity(owner,0,0)==-2,"initial gravity neutral");check(MiniBalanLink.gravity(owner,0,0)==-2,"second gravity neutral");MiniBalanLink.motion(owner,0,0);check(MiniBalanLink.gravity(owner,4,0)==2,"fresh tilt after consecutive neutral samples");Handler.advance(200);
  check(MiniBalanLink.gravity(owner,3.2f,0)==-2,"rounding-gap sample explicitly zero");Handler.advance(100);check(g.writes.get(g.writes.size()-1).equals("CMD|3|0|0|$"),"4 to 3.2 transition stops old vector");
  for(int i=0;i<5;i++)MiniBalanLink.gravity(owner,0,0);check(MiniBalanLink.gravity(owner,4,0)==2,"odd neutral count does not block tilt");Handler.advance(100);MiniBalanLink.gravity(owner,0,0);Handler.advance(100);
  MiniBalanLink.ControlReset oldOwner=owner;owner=()->resets++;MiniBalanLink.bindControl(owner,view);int boundCount=g.writes.size();MiniBalanLink.controlMode(owner,1);MiniBalanLink.gravity(oldOwner,0,0);MiniBalanLink.gravity(oldOwner,4,0);MiniBalanLink.motion(oldOwner,1,0);Handler.advance(200);check(g.writes.size()==boundCount,"old sensor/joystick owner cannot move new view");
  MiniBalanLink.controlMode(owner,2);MiniBalanLink.gravity(owner,0,0);MiniBalanLink.gravity(owner,4,0);Handler.advance(200);check(g.writes.size()==boundCount,"queued gravity event ignored in joystick mode");
  for(int code:new int[]{3,4,5,6,11,13,15,16,21,78,86,87,92,101,103,104}){Message m=ui.obtainMessage(code,"CMD|7|bad|$");check(!MiniBalanLink.acceptUiEvent(m),"queued legacy BLE event rejected "+code);}check(MiniBalanLink.acceptUiEvent(ui.obtainMessage(20,null)),"UI ticker 20 preserved");check(!MiniBalanLink.acceptUiEvent(lastTrusted),"trusted event cannot be replayed outside guarded dispatch");
  g=make("e0ff",24);g.autoWrite=false;ready(g);check(g.types.get(0)==2,"response fallback");MiniBalanLink.send("CMD|7|$");Handler.advance(100);check(g.writes.size()==1,"response serial queue");g.callback.onCharacteristicWrite(g,g.lastWrite,0);Handler.advance(40);check(g.writes.size()==2,"response callback advances queue");Handler.advance(800);check(!MiniBalanLink.isReady()&&g.closed,"write timeout disconnects");
  g=make("e0ff",20);open(g);g.completeDescriptor(5);Handler.advance(0);check(!MiniBalanLink.isReady()&&g.closed,"failed CCCD rejected");
  g=make("e0ff",20);open(g);Handler.advance(10001);check(!MiniBalanLink.isReady()&&g.closed,"total connect timeout");
  g=make("e0ff",20);g.services.get(0).add(characteristic(4,false));open(g);check(g.closed,"ambiguous write handles rejected");
  g=make("e0ff",4);g.services.get(0).add(characteristic(16,true));ready(g);check(g.types.get(0)==1,"separate same-UUID write and notify handles accepted");
  g=make("ffe0",20);BluetoothGattService preferred=new BluetoothGattService(u("e0ff"));preferred.add(characteristic(20,true));g.services.add(preferred);ready(g);check(g.lastWrite.getService()==preferred,"E0FF preference");
  g=make("e0ff",24);g.autoWrite=false;ready(g);for(int i=0;i<26;i++)MiniBalanLink.send("CMD|7|$");check(!MiniBalanLink.isReady()&&g.closed,"bounded queue fails closed");
  g=make("e0ff",20);ready(g);g.rejectWrite=true;MiniBalanLink.send("CMD|7|$");Handler.advance(100);check(!MiniBalanLink.isReady(),"GATT enqueue rejection handled");
  g=make("e0ff",20);g.rejectWrite=true;open(g);g.completeDescriptor(0);Handler.advance(60);check(!MiniBalanLink.isReady()&&g.closed,"first zero enqueue failure cannot crash readiness callback");
  g=make("e0ff",20);g.denyWrite=true;open(g);g.completeDescriptor(0);Handler.advance(60);check(!MiniBalanLink.isReady()&&g.closed,"first zero permission failure cannot crash readiness callback");
  // Quiet DMA epoch: first zero may be discarded; only the second dispatch arms MCU.
  g=make("e0ff",20);ready(g);Handler.advance(600);before=g.writes.size();MiniBalanLink.motion(owner,1,0);Handler.advance(50);MiniBalanLink.motion(owner,-1,0);Handler.advance(160);check(g.writes.get(before).equals("CMD|3|0|0|$")&&g.writes.get(before+1).equals("CMD|3|0|0|$"),"idle rearm sends two distinct zeros");check(g.writes.get(before+2).equals("CMD|3|-1|0|$"),"rearm uses latest gesture vector");
  g=make("e0ff",20);ready(g);before=g.writes.size();MiniBalanLink.motion(owner,1,0);Handler.advance(50);MiniBalanLink.release();Handler.advance(400);check(!g.writes.contains("CMD|3|1|0|$"),"release cancels delayed second zero completion");
  g=make("e0ff",20);ready(g);MiniBalanLink.motion(owner,1,0);Handler.advance(50);MiniBalanLink.foreground(false);Handler.advance(400);check(!g.writes.contains("CMD|3|1|0|$"),"pause cancels arm timer");
  g=make("e0ff",20);ready(g);MiniBalanLink.motion(owner,1,0);Handler.advance(50);owner=()->resets++;MiniBalanLink.bindControl(owner,view);Handler.advance(400);check(!g.writes.contains("CMD|3|1|0|$"),"owner replacement cancels old arm timer");
  g=make("e0ff",20);ready(g);MiniBalanLink.motion(owner,1,0);Handler.advance(50);BluetoothGatt replacement=make("e0ff",20);ready(replacement);Handler.advance(400);check(replacement.writes.size()==1,"reconnect cancels both old rearm phases");
  // A delayed completion from gesture A must not clear gesture B's arm barrier.
  g=make("e0ff",24);g.autoWrite=false;ready(g);g.callback.onCharacteristicWrite(g,g.lastWrite,0);Handler.advance(40);MiniBalanLink.motion(owner,1,0);MiniBalanLink.release();MiniBalanLink.motion(owner,-1,0);g.callback.onCharacteristicWrite(g,g.lastWrite,0);Handler.advance(40);check(!g.writes.contains("CMD|3|-1|0|$"),"old gesture completion cannot unlock new gesture");g.callback.onCharacteristicWrite(g,g.lastWrite,0);Handler.advance(140);check(!g.writes.contains("CMD|3|-1|0|$"),"new gesture waits for second response completion");g.callback.onCharacteristicWrite(g,g.lastWrite,0);Handler.advance(40);check(g.writes.get(g.writes.size()-1).equals("CMD|3|-1|0|$"),"new gesture moves only after own second zero completion");
  MiniBalanLink.unregister(ctx,new BroadcastReceiver());MiniBalanLink.unregister(ctx,new BroadcastReceiver());check(ctx.unregisters==2,"unregister idempotent");
  MiniBalanLink.leaveForLegacy();check(!MiniBalanLink.isOwned(),"legacy ownership restored");
  MiniBalanLink.selectMini();check(MiniBalanLink.isOwned()&&!MiniBalanLink.isReady(),"MiniBalan selection claims mode without auto-connecting");check(!MiniBalanLink.beginLegacyOperation(),"previous robot cannot provide selected MiniBalan transport");MiniBalanLink.leaveForLegacy();
  check(MiniBalanLink.beginLegacyDispatch(),"legacy sender entered old generation");check(MiniBalanLink.beginLegacyOperation(),"legacy GATT operation before takeover");MiniBalanLink.endLegacyOperation();
  BluetoothGatt next=make("e0ff",20);MiniBalanLink.connect(ctx,ui,new BluetoothDevice("new",next));next.discover();next.completeDescriptor(0);Handler.advance(60);check(MiniBalanLink.isReady(),"new GATT ready while old sender suspended");
  check(!MiniBalanLink.legacyDispatchCurrent(),"sleeping sender generation invalidated");check(!MiniBalanLink.beginLegacyOperation(),"old GATT write/reconnect fenced");check(!MiniBalanLink.allowStop(),"legacy failure cannot call stop");MiniBalanLink.stop();Handler.advance(250);check(MiniBalanLink.isReady(),"legacy failure did not close new session");check(!MiniBalanLink.sendBytes("CMD|7|$".getBytes(StandardCharsets.US_ASCII)),"old sender cannot inject into new queue");
  MiniBalanLink.endLegacyDispatch();MiniBalanLink.leaveForLegacy();check(MiniBalanLink.acceptUiEvent(ui.obtainMessage(3,null)),"other robot connection event preserved");check(!MiniBalanLink.acceptUiEvent(ui.obtainMessage(103,"CMD|7|bad|$")),"unmarked MiniBalan telemetry never enters old parser");
  java.util.concurrent.CountDownLatch entered=new java.util.concurrent.CountDownLatch(1);java.util.concurrent.atomic.AtomicBoolean finished=new java.util.concurrent.atomic.AtomicBoolean();java.util.concurrent.atomic.AtomicReference<Throwable> failure=new java.util.concurrent.atomic.AtomicReference<>();
  Thread legacy=new Thread(()->{try{if(!MiniBalanLink.beginLegacyDispatch()||!MiniBalanLink.beginLegacyOperation())throw new AssertionError("legacy gate acquisition");entered.countDown();Thread.sleep(40);if(MiniBalanLink.isOwned())throw new AssertionError("ownership changed inside old GATT operation");MiniBalanLink.endLegacyOperation();MiniBalanLink.endLegacyDispatch();finished.set(true);}catch(Throwable t){failure.set(t);entered.countDown();}});legacy.start();entered.await();MiniBalanLink.connect(ctx,ui,new BluetoothDevice("gated",make("e0ff",20)));legacy.join();check(failure.get()==null&&finished.get()&&MiniBalanLink.isOwned(),"atomic legacy GATT operation versus ownership takeover");MiniBalanLink.destroy();
  System.out.println("BLE session replay: PASS ("+checks+" assertions)");
 }
}'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--java-home', type=Path, required=True)
    args = parser.parse_args()
    link = Path(__file__).resolve().parents[1]
    suffix = '.exe' if (args.java_home / 'bin/javac.exe').exists() else ''
    with tempfile.TemporaryDirectory(prefix='mvtbot-ble-test-') as temp:
        root = Path(temp)
        files = []
        for name, source in STUBS.items():
            p = root / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(source, encoding='utf-8')
            files.append(p)
        test = root / 'com/mvtbot/link/BleSessionTest.java'
        test.parent.mkdir(parents=True, exist_ok=True)
        test.write_text(TEST, encoding='utf-8')
        sources = [link / 'src/com/mvtbot/link' / name for name in ('MiniBalanLink.java', 'FrameDecoder.java', 'ProtocolValidation.java')]
        classes = root / 'classes'
        classes.mkdir()
        subprocess.run([str(args.java_home / ('bin/javac' + suffix)), '-encoding', 'UTF-8', '-d', str(classes), *map(str, files + sources + [test])], check=True)
        subprocess.run([str(args.java_home / ('bin/java' + suffix)), '-cp', str(classes), 'com.mvtbot.link.BleSessionTest'], check=True)


if __name__ == '__main__':
    main()
