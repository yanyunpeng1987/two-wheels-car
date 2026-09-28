#!/usr/bin/env python3
"""Exercise real MiniBalan health/gesture/reconnect code with deterministic Android fakes.

The timelines model queue and callback delays. They do not prove radio timing,
Android native client cleanup, or vehicle acceptance.
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import tempfile

CASES = r'''
 static Object field(String key)throws Exception{java.lang.reflect.Field f=MiniBalanLink.class.getDeclaredField(key);f.setAccessible(true);return f.get(null);}
 static boolean flag(String key)throws Exception{return (Boolean)field(key);}
 static int movements(BluetoothGatt g){return (int)g.writes.stream().filter(x->x.startsWith("CMD|3|")&&!x.equals("CMD|3|0|0|$")).count();}
 static int queries(BluetoothGatt g){return Collections.frequency(g.writes,"CMD|7|$");}
 static void telemetry(BluetoothGatt g){receive(g,"CMD|4|1|2|$CMD|5|42|$");}
 static void until(BluetoothGatt g,long deadline){while(Handler.now<deadline&&!g.closed){telemetry(g);Handler.advance(Math.min(10,deadline-Handler.now));}}
 static void cleanState()throws Exception{
  check(field("gatt")==null&&field("active")==null,"closed session has no retained GATT or active write");
  check(((Collection<?>)field("queue")).isEmpty(),"closed session queue empty");
  check(!flag("waitingWrite")&&!flag("healthPending")&&!flag("healthRecovering"),"closed session pending state reset");
  check(field("compatibility").toString().equals("NORMAL"),"closed session cannot reuse vendor profile");
 }
 static void freshVoltageAndLateCallback()throws Exception{
  BluetoothGatt g=vendor();ready(g);long start=Handler.now;g.writeDelayMs=720;g.notifyDelayMs=90;MiniBalanLink.motion(owner,1,0);
  until(g,start+4749);
  check(MiniBalanLink.isReady()&&!g.closed,"live before former stale cutoff");
  check(Handler.now-(Long)field("healthFrameAt")==339&&field("healthVoltage")!=null,"fresh voltage before delayed write callback");
  check(flag("healthPending")&&!flag("healthWriteDone"),"query waiting only for its legal write callback");
  Handler.advance(1);
  check(!g.closed&&flag("healthRecovering")&&!flag("heartbeat"),"2500ms suspends movement without truncating active proof");
  MiniBalanLink.release();MiniBalanLink.motion(owner,0,0);
  check(flag("motionNeutralRequired"),"neutral/release while proof is absent cannot clear recovery latch");
  g.writeDelayMs=90;until(g,start+5040);
  check(!g.closed&&!flag("healthRecovering")&&flag("motionNeutralRequired"),"callback at720ms completes proof without replaying motion");
  int stopped=movements(g);
  MiniBalanLink.ControlReset ordinary=owner;owner=()->{resets++;MiniBalanLink.release();MiniBalanLink.motion(owner,0,0);MiniBalanLink.gravity(owner,0,0);};
  MiniBalanLink.bindControl(owner,view);MiniBalanLink.controlMode(owner,1);
  check(flag("motionNeutralRequired"),"programmatic reset callback cannot impersonate neutral/release");
  for(int i=0;i<30;i++){MiniBalanLink.motion(owner,1,0);Handler.advance(50);}
  check(movements(g)==stopped&&flag("motionNeutralRequired"),"held old gesture cannot resume after proof recovery");
  MiniBalanLink.release();MiniBalanLink.motion(owner,1,0);Handler.advance(600);
  check(movements(g)>stopped,"real release after proof permits new double-zero-armed gesture");
  MiniBalanLink.release();owner=ordinary;MiniBalanLink.bindControl(owner,view);
  System.out.println("HEALTH fresh CMD7 +720ms callback: PASS; old cutoff stops movement, bounded proof survives");
 }
 static void queuedQueryGetsItsWindow()throws Exception{
  BluetoothGatt g=vendor();ready(g);long start=Handler.now;g.writeDelayMs=450;g.notifyDelayMs=900;MiniBalanLink.motion(owner,1,0);
  until(g,start+4400);
  check(!g.closed&&flag("healthRecovering")&&flag("healthWriteDone"),"old deadline pauses while queued query waits for voltage");
  check((Long)field("healthSubmittedAt")==start+3700,"query begins at actual submission after queue delay");
  until(g,start+4600);
  check(!g.closed&&!flag("healthRecovering")&&(Long)field("lastHealthValidAt")==start+4600,"900ms response gets full submission window");
  check(!flag("heartbeat")&&flag("motionNeutralRequired"),"successful late query never replays old motion");
  System.out.println("HEALTH450ms writes +900ms reply: PASS; query3700ms reply4600ms survives old4400ms cutoff");
 }
 static void singleMissingQueryAndGesture()throws Exception{
  BluetoothGatt g=vendor();ready(g);g.writeDelayMs=90;g.notifyDelayMs=90;g.autoNotify=false;MiniBalanLink.motion(owner,1,0);
  while(queries(g)<2&&!g.closed)Handler.advance(10);
  long submitted=(Long)field("healthSubmittedAt");until(g,submitted+1499);
  check(!g.closed&&flag("healthPending"),"first query retains its full1500ms submission window");
  g.autoNotify=true;Handler.advance(1);
  check(!g.closed&&flag("healthRecovering")&&(Integer)field("healthAttempt")==2,"one completed read-only query may retry after missing response");
  Handler.advance(400);
  check(!g.closed&&!flag("healthRecovering")&&queries(g)==3,"single lost response recovered by exactly one CMD7 retry");
  int stopped=movements(g);for(int i=0;i<10;i++){MiniBalanLink.motion(owner,1,0);Handler.advance(30);}
  check(movements(g)==stopped,"retry recovery cannot continue held joystick vector");
  MiniBalanLink.controlMode(owner,1);
  for(int i=0;i<10;i++){MiniBalanLink.gravity(owner,4,0);Handler.advance(30);}
  check(movements(g)==stopped,"retry recovery cannot continue held tilt");
  MiniBalanLink.gravity(owner,0,0);MiniBalanLink.gravity(owner,4,0);Handler.advance(450);
  check(movements(g)>stopped,"post-proof neutral then fresh tilt may rearm");
  System.out.println("HEALTH one missing CMD7 +joystick/gravity neutral gate: PASS");
 }
 static void persistentMissingAndQueueBound()throws Exception{
  BluetoothGatt g=vendor();ready(g);g.writeDelayMs=90;g.autoNotify=false;long start=Handler.now;MiniBalanLink.motion(owner,1,0);
  until(g,start+7000);
  check(g.closed&&queries(g)==3,"two missing responses close; no third health attempt");
  check(android.util.Log.messages.stream().anyMatch(s->s.contains("health CMD7 round-trip timeout")&&s.contains("healthAttempt=2")&&s.contains("queryAgeMs=1500")),"final failure reports actual attempt and query elapsed");
  cleanState();
  g=vendor();ready(g);g.writeDelayMs=450;g.notifyDelayMs=90;start=Handler.now;MiniBalanLink.motion(owner,1,0);
  while(!g.closed&&Handler.now-start<3500){telemetry(g);MiniBalanLink.release();MiniBalanLink.motion(owner,1,0);Handler.advance(200);}
  check(g.closed&&queries(g)==1,"perpetual stop replacement cannot leave health queued forever");
  check(android.util.Log.messages.stream().anyMatch(s->s.contains("health CMD7 queue timeout")&&s.contains("submitted=false")&&s.contains("queueAgeMs=1500")),"queue deadline measured from enqueue");
  cleanState();System.out.println("HEALTH repeated loss and queue starvation remain bounded: PASS");
 }
 static void writesAndSameProcessReconnect()throws Exception{
  BluetoothGatt g=vendor();ready(g);
  for(int i=0;i<50;i++){
   BluetoothGatt previous=g;g.autoWrite=false;long start=Handler.now;MiniBalanLink.send("CMD|8|1|$");
   until(g,start+750);
   check(g.closed,"ongoing telemetry cannot mask750ms missing ATT callback");
   check(g.disconnectCalls==1&&g.closeCalls==1,"failed session disconnects and closes once");cleanState();
   g=vendor();check(MiniBalanLink.connect(ctx,ui,new BluetoothDevice("HC-05D",g)),"manual reconnect accepted without destroy/rebind");g.discover();g.completeDescriptor(0);Handler.advance(310);
   check(MiniBalanLink.isReady(),"same-process manual reconnect succeeds");
   previous.callback.onCharacteristicWrite(previous,previous.lastWrite,133);previous.callback.onConnectionStateChange(previous,133,0);Handler.advance(0);
   check(MiniBalanLink.isReady(),"old callback/status cannot close replacement");
   check(g.writes.stream().noneMatch(x->x.equals("CMD|3|1|0|$")),"reconnect does not replay nonzero motion");
  }
  g.disconnectDenied=true;g.closeDenied=true;MiniBalanLink.destroy();cleanState();
  check(g.disconnectCalls==1&&g.closeCalls==1,"close is attempted even when disconnect throws");
  check(android.util.Log.messages.stream().anyMatch(s->s.contains("GATT disconnect cleanup failed: exception=SecurityException")),"disconnect exception retained in diagnostics");
  check(android.util.Log.messages.stream().anyMatch(s->s.contains("GATT close cleanup failed: exception=SecurityException")),"close exception retained in diagnostics");
  g=vendor();ready(g);check(MiniBalanLink.isReady(),"cleanup exception cannot poison next Java session");MiniBalanLink.destroy();Handler.advance(20000);
  System.out.println("HEALTH write deadline +50 same-process reconnects +cleanup exceptions: PASS");
 }

 static void awaitRecoveryNotice(BluetoothGatt g)throws Exception{
  g.autoNotify=false;while(queries(g)<2&&!g.closed)Handler.advance(10);
  until(g,(Long)field("healthSubmittedAt")+1500);
  check(!g.closed&&flag("healthRecovering"),"notice scenario reaches bounded recovery");
 }
 static void recoverForNotice(BluetoothGatt g)throws Exception{
  g.autoNotify=true;receive(g,"CMD|7|12020|$");
  check(!g.closed&&!flag("healthRecovering")&&flag("motionNeutralRequired"),"notice scenario recovers proof and retains neutral gate");
 }
 static void recoveryNotices()throws Exception{
  Locale original=Locale.getDefault();
  try{
   for(Locale locale:new Locale[]{Locale.SIMPLIFIED_CHINESE,Locale.US}){
    Locale.setDefault(locale);BluetoothGatt g=vendor();ready(g);android.widget.Toast.shown.clear();int connected=connectedEvents();
    awaitRecoveryNotice(g);
    check(android.widget.Toast.shown.size()==1,"one pause notice per recovery cycle");
    check(android.widget.Toast.shown.get(0).equals(locale.getLanguage().equals("zh")?"通信恢复中，遥控已暂停":"Restoring communication. Remote control is paused."),"pause notice follows locale");
    for(int i=0;i<20;i++)MiniBalanLink.send("CMD|7|$");Handler.advance(100);
    check(android.widget.Toast.shown.size()==1,"tickers and duplicate query requests do not repeat pause notice");
    recoverForNotice(g);
    check(android.widget.Toast.shown.size()==2,"one restored notice for completed recovery");
    check(android.widget.Toast.shown.get(1).equals(locale.getLanguage().equals("zh")?"通信已恢复，请松开摇杆后重新操作":"Communication restored. Release the joystick, then try again."),"restored joystick notice follows locale");
    receive(g,"CMD|7|12020|$");Handler.advance(400);
    check(android.widget.Toast.shown.size()==2&&connectedEvents()==connected,"duplicate telemetry does not repeat notice or connection event");
   }
   Locale.setDefault(Locale.SIMPLIFIED_CHINESE);BluetoothGatt g=vendor();ready(g);MiniBalanLink.controlMode(owner,1);android.widget.Toast.shown.clear();
   awaitRecoveryNotice(g);recoverForNotice(g);
   check(android.widget.Toast.shown.size()==2&&android.widget.Toast.shown.get(1).equals("通信已恢复，请放平手机后重新操作"),"gravity mode gives meaningful neutral instruction");
   g=vendor();ready(g);android.widget.Toast.shown.clear();MiniBalanLink.foreground(false);
   awaitRecoveryNotice(g);recoverForNotice(g);MiniBalanLink.foreground(true);Handler.advance(100);
   check(android.widget.Toast.shown.isEmpty(),"background recovery emits no notice, including delayed foreground return");
   g=vendor();ready(g);android.widget.Toast.shown.clear();awaitRecoveryNotice(g);MiniBalanLink.foreground(false);recoverForNotice(g);MiniBalanLink.foreground(true);
   check(android.widget.Toast.shown.size()==1,"foreground pause followed by background restoration does not show restore notice");
   BluetoothGatt old=vendor();ready(old);old.autoNotify=false;while(queries(old)<2)Handler.advance(10);
   g=vendor();check(MiniBalanLink.connect(ctx,ui,new BluetoothDevice("HC-05D",g)),"replace pending health session");g.discover();g.completeDescriptor(0);Handler.advance(310);android.widget.Toast.shown.clear();
   receive(old,"CMD|7|12020|$");old.callback.onCharacteristicWrite(old,old.lastWrite,13);Handler.advance(3000);
   check(MiniBalanLink.isReady()&&android.widget.Toast.shown.isEmpty(),"old health timers and callbacks cannot emit notices in replacement session");
   g=vendor();ready(g);android.widget.Toast.shown.clear();((java.lang.ref.WeakReference<?>)field("ui")).clear();awaitRecoveryNotice(g);recoverForNotice(g);
   check(android.widget.Toast.shown.isEmpty(),"missing live UI suppresses health notices");
   g=vendor();ready(g);android.widget.Toast.shown.clear();android.widget.Toast.denied=true;awaitRecoveryNotice(g);recoverForNotice(g);
   check(MiniBalanLink.isReady()&&android.widget.Toast.shown.isEmpty(),"unavailable toast cannot fail healthy GATT session");android.widget.Toast.denied=false;MiniBalanLink.destroy();
  }finally{Locale.setDefault(original);android.widget.Toast.denied=false;}
  System.out.println("HEALTH notices: localized once per phase; background, stale session and lost UI suppressed: PASS");
 }
 public static void main(String[]args)throws Exception{
  freshVoltageAndLateCallback();queuedQueryGetsItsWindow();singleMissingQueryAndGesture();persistentMissingAndQueueBound();writesAndSameProcessReconnect();recoveryNotices();
  check(android.util.Log.messages.stream().anyMatch(s->s.contains("health round-trip verified:")&&s.contains("enqueuedAt=")&&s.contains("submittedAt=")&&s.contains("receivedAt=")&&s.contains("writeCompleteAt=")),"all health timing phases are logged");
  System.out.println("Health recovery replay: PASS ("+checks+" assertions)");
 }
}'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path, required=True)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location('ble_fixture', here / 'test_ble_session.py')
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    helpers = fixture.TEST.split(' public static void main(String[] args) throws Exception {')[0]
    suffix = '.exe' if (args.java_home / 'bin/javac.exe').exists() else ''
    with tempfile.TemporaryDirectory(prefix='mvtbot-health-test-') as temp:
        root = Path(temp)
        files = []
        for name, source in fixture.STUBS.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding='utf-8')
            files.append(path)
        test = root / 'com/mvtbot/link/BleSessionTest.java'
        test.parent.mkdir(parents=True, exist_ok=True)
        test.write_text(helpers + CASES, encoding='utf-8')
        classes = root / 'classes'
        classes.mkdir()
        sources = fixture.transport_sources(here.parent)
        subprocess.run([str(args.java_home / ('bin/javac' + suffix)), '-encoding', 'UTF-8', '-d', str(classes), *map(str, files + sources + [test])], check=True)
        subprocess.run([str(args.java_home / ('bin/java' + suffix)), '-cp', str(classes), 'com.mvtbot.link.BleSessionTest'], check=True)


if __name__ == '__main__':
    main()
