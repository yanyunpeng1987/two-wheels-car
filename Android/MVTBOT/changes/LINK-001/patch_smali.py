#!/usr/bin/env python3
"""Apply the LINK-001 v21 bridge to an isolated Apktool smali tree.

Usage: python patch_smali.py --smali-root <decoded-project/smali> [--ui-patch patch.json]
The original project/raw DEX are never modified. All inputs are checked before
any write; a complete second invocation verifies outputs and is a no-op.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

BASE = "com/Wonder/bot/"
LINK = "Lcom/mvtbot/link/MiniBalanLink;"
SCAN = "Lcom/mvtbot/link/ManualBleScanner;"
MAN = "Lcom/Wonder/bot/BluetoothConnect/BLEManager;"
MAIN = "Lcom/Wonder/bot/MainActivity;"
CTRL = "Lcom/Wonder/bot/fragment/MiniBalan/BalanceCarControlFragment;"
MARKER = ".mvtbot-link-001.json"
PATCH_VERSION = 5
UI_COMMIT = "aa000240b183a8daeed3f09ca084cdf9335e0076"
UI_PATCH_SHA256 = "953db3d574a712fa707e11ba192c28d59f3758f8c80f3a2808016a423e9e1eef"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"expected one anchor, got {text.count(old)}: {old[:100]!r}")
    return text.replace(old, new, 1)


def find_method(text, signature):
    pattern = re.compile(r"(?ms)^\.method [^\n]* " + re.escape(signature) + r"\n.*?^\.end method$")
    found = list(pattern.finditer(text))
    if len(found) != 1:
        raise ValueError(f"expected one method {signature}, got {len(found)}")
    return found[0].group(0)


def replace_method(text, signature, body):
    old = find_method(text, signature)
    header = old.splitlines()[0]
    return replace_once(text, old, header + "\n" + body.strip("\n") + "\n.end method")


def wrap(text, signature, body):
    old = find_method(text, signature)
    name, rest = signature.split("(", 1)
    renamed = name + "$v21(" + rest
    legacy = old.replace(" " + signature + "\n", " " + renamed + "\n", 1)
    header = old.splitlines()[0]
    return replace_once(text, old, legacy + "\n\n" + header + "\n" + body.strip("\n") + "\n.end method")


def prepend(text, signature, code):
    old = find_method(text, signature)
    # The caller must use only existing locals, or explicitly replace the whole method.
    new = re.sub(r"(?m)^(    \.locals \d+)$", lambda m: m.group(0) + "\n\n" + code.strip("\n"), old, count=1)
    if new == old:
        raise ValueError("missing locals: " + signature)
    return replace_once(text, old, new)


def owned_guard(return_instruction="return-void", result=""):
    return f"""    invoke-static {{}}, {LINK}->isOwned()Z
    move-result v0
    if-eqz v0, :link_legacy
{result}
    {return_instruction}
    :link_legacy
"""


def transform(relative, text, ui_fixed=False):
    if relative == BASE + "BluetoothConnect/BLEManager.smali":
        text = wrap(text, "connect(Landroid/bluetooth/BluetoothDevice;)Z", f"""
    .locals 2
    invoke-static {{}}, {MAIN}->mvtbotMiniSelected()Z
    move-result v0
    if-eqz v0, :link_legacy
    invoke-static {{p1}}, {LINK}->connect(Landroid/bluetooth/BluetoothDevice;)Z
    move-result v0
    if-eqz v0, :link_return
    invoke-virtual {{p0}}, {MAN}->stop$v21()V
    :link_return
    return v0
    :link_legacy
    invoke-static {{}}, {LINK}->leaveForLegacy()V
    invoke-virtual {{p0, p1}}, {MAN}->connect$v21(Landroid/bluetooth/BluetoothDevice;)Z
    move-result v0
    return v0
""")
        text = wrap(text, "isConnected()Z", f"""
    .locals 1
{owned_guard('return v0', f'    invoke-static {{}}, {LINK}->isReady()Z' + chr(10) + '    move-result v0')}
    invoke-virtual {{p0}}, {MAN}->isConnected$v21()Z
    move-result v0
    return v0
""")
        text = wrap(text, "getDeviceName()Ljava/lang/String;", f"""
    .locals 1
{owned_guard('return-object v0', f'    invoke-static {{}}, {LINK}->deviceName()Ljava/lang/String;' + chr(10) + '    move-result-object v0')}
    invoke-virtual {{p0}}, {MAN}->getDeviceName$v21()Ljava/lang/String;
    move-result-object v0
    return-object v0
""")
        for signature in ("sendDirect(Ljava/lang/String;)V", "send(Ljava/lang/String;)V", "send(Ljava/lang/String;J)V"):
            args = "p0, p1, p2, p3" if ";J" in signature else "p0, p1"
            renamed = signature.replace("(", "$v21(", 1)
            text = wrap(text, signature, f"""
    .locals 1
{owned_guard(result=f'    invoke-static {{p1}}, {LINK}->send(Ljava/lang/String;)Z')}
    invoke-virtual {{{args}}}, {MAN}->{renamed}
    return-void
""")
        text = wrap(text, "write([BJ)Z", f"""
    .locals 1
{owned_guard('return v0', f'    invoke-static {{p1}}, {LINK}->sendBytes([B)Z' + chr(10) + '    move-result v0')}
    invoke-virtual {{p0, p1, p2, p3}}, {MAN}->write$v21([BJ)Z
    move-result v0
    return v0
""")
        text = wrap(text, "write(Ljava/nio/ByteBuffer;J)Z", f"""
    .locals 2
    invoke-static {{}}, {LINK}->legacyDispatchCurrent()Z
    move-result v0
    if-eqz v0, :link_cancelled
    invoke-virtual {{p0, p1, p2, p3}}, {MAN}->write$v21(Ljava/nio/ByteBuffer;J)Z
    move-result v1
    invoke-static {{}}, {LINK}->legacyDispatchCurrent()Z
    move-result v0
    if-eqz v0, :link_cancelled
    return v1
    :link_cancelled
    const/4 v0, 0x1
    return v0
""")
        text = wrap(text, "stop()V", f"""
    .locals 1
    invoke-static {{}}, {LINK}->allowStop()Z
    move-result v0
    if-nez v0, :link_allowed
    return-void
    :link_allowed
{owned_guard(result=f'    invoke-static {{}}, {LINK}->stop()V')}
    invoke-virtual {{p0}}, {MAN}->stop$v21()V
    return-void
""")
        # register() already saves application context; unregister that exact context.
        text = replace_method(text, "unregister(Landroid/content/Context;)V", f"""
    .locals 2
    sget-boolean v0, {MAN}->isRegistered:Z
    if-eqz v0, :link_done
    iget-object v0, p0, {MAN}->registeredContext:Landroid/content/Context;
    iget-object v1, p0, {MAN}->mReceiver:Landroid/content/BroadcastReceiver;
    invoke-static {{v0, v1}}, {LINK}->unregister(Landroid/content/Context;Landroid/content/BroadcastReceiver;)V
    const/4 v0, 0x0
    sput-boolean v0, {MAN}->isRegistered:Z
    const/4 v0, 0x0
    iput-object v0, p0, {MAN}->registeredContext:Landroid/content/Context;
    :link_done
    return-void
""")
        # Service disconnection can destroy the manager before Activity teardown.
        # Unregister while receiver/context still exist and clear the static flag.
        text = prepend(text, "destroy()V", f"""
    iget-object v0, p0, {MAN}->registeredContext:Landroid/content/Context;
    invoke-virtual {{p0, v0}}, {MAN}->unregister(Landroid/content/Context;)V
""")
    elif relative == BASE + "BluetoothConnect/BLEManager$1.smali":
        text = prepend(text, "onReceive(Landroid/content/Context;Landroid/content/Intent;)V", owned_guard())
    elif relative == BASE + "BluetoothConnect/BLEManager$2.smali":
        text = prepend(text, "handleMessage(Landroid/os/Message;)Z", owned_guard("return v0", "    const/4 v0, 0x1"))
    elif relative == BASE + "BluetoothConnect/BLEManager$3.smali":
        # Capture the legacy dispatch generation across its sleeps. GATT operations
        # use the same short gate as ownership takeover; never hold it for a sleep.
        text = replace_once(text, "    :goto_0\n    invoke-interface {p1}, Ljava/util/Iterator;->hasNext()Z", f"""    :goto_0
    invoke-static {{}}, {LINK}->legacyDispatchCurrent()Z
    move-result v0
    if-eqz v0, :cond_5
    invoke-interface {{p1}}, Ljava/util/Iterator;->hasNext()Z""")
        text = replace_once(text, "    .line 472\n    sget-boolean v0, Lcom/Wonder/bot/MainActivity;->handDisconnect:Z", f"""    .line 472
    invoke-static {{}}, {LINK}->legacyDispatchCurrent()Z
    move-result v0
    if-eqz v0, :cond_5
    sget-boolean v0, Lcom/Wonder/bot/MainActivity;->handDisconnect:Z""")
        text = replace_once(text, f"    sput-boolean v1, {MAIN}->noShowConnect:Z", f"""    invoke-static {{}}, {LINK}->beginLegacyOperation()Z
    move-result v0
    if-eqz v0, :cond_5
    sput-boolean v1, {MAIN}->noShowConnect:Z
    invoke-static {{}}, {LINK}->endLegacyOperation()V""")
        text = wrap(text, "handleMessage(Landroid/os/Message;)Z", f"""
    .locals 1
    invoke-static {{}}, {LINK}->beginLegacyDispatch()Z
    move-result v0
    if-eqz v0, :link_cancelled
    :link_try
    invoke-virtual {{p0, p1}}, Lcom/Wonder/bot/BluetoothConnect/BLEManager$3;->handleMessage$v21(Landroid/os/Message;)Z
    move-result v0
    :link_end
    .catchall {{:link_try .. :link_end}} :link_error
    invoke-static {{}}, {LINK}->endLegacyDispatch()V
    return v0
    :link_error
    move-exception v0
    invoke-static {{}}, {LINK}->endLegacyDispatch()V
    throw v0
    :link_cancelled
    const/4 v0, 0x1
    return v0
""")
    elif relative == BASE + "BluetoothConnect/BLEService.smali":
        cls = "Lcom/Wonder/bot/BluetoothConnect/BLEService;"
        for signature, arguments in (("writeCharacteristic(Landroid/bluetooth/BluetoothGattCharacteristic;)Z", "p0, p1"),
                                      ("connect(Ljava/lang/String;)Z", "p0, p1"), ("reconnect()Z", "p0")):
            original = signature.replace("(", "$v21(", 1)
            text = wrap(text, signature, f"""
    .locals 1
    invoke-static {{}}, {LINK}->beginLegacyOperation()Z
    move-result v0
    if-eqz v0, :link_denied
    :link_try
    invoke-virtual {{{arguments}}}, {cls}->{original}
    move-result v0
    :link_end
    .catchall {{:link_try .. :link_end}} :link_error
    invoke-static {{}}, {LINK}->endLegacyOperation()V
    return v0
    :link_error
    move-exception v0
    invoke-static {{}}, {LINK}->endLegacyOperation()V
    throw v0
    :link_denied
    const/4 v0, 0x0
    return v0
""")
    elif relative == BASE + "MainActivity$MsgCallBack.smali":
        text = wrap(text, "handleMessage(Landroid/os/Message;)Z", f"""
    .locals 1
    invoke-static {{p1}}, {LINK}->acceptUiEvent(Landroid/os/Message;)Z
    move-result v0
    if-eqz v0, :link_drop
    invoke-virtual {{p0, p1}}, Lcom/Wonder/bot/MainActivity$MsgCallBack;->handleMessage$v21(Landroid/os/Message;)Z
    move-result v0
    return v0
    :link_drop
    const/4 v0, 0x1
    return v0
""")
    elif relative == BASE + "MainActivity.smali":
        permission = "    invoke-static {p0}, Lcom/Wonder/bot/utils/PermissionUtils;->mayRequestLocation(Landroid/app/Activity;)Z"
        clicked = find_method(text, "onClick(Landroid/view/View;)V")
        text = replace_once(text, clicked, replace_once(clicked, permission,
                            f"    invoke-static {{p0}}, {MAIN}->mvtbotManualBlePermissions(Landroid/app/Activity;)Z"))
        if ui_fixed:
            # This reviewed UI composition always selects MiniBalan, but that
            # selection occurs after this onCreate location-permission call.
            # Request only from the user's Bluetooth click, never from startup.
            created = find_method(text, "onCreate(Landroid/os/Bundle;)V")
            text = replace_once(text, created, replace_once(created, permission, ""))
        anchor = f"    iput-object v0, p0, {MAIN}->mHandler:Landroid/os/Handler;"
        text = replace_once(text, anchor, anchor + f"\n\n    invoke-static {{p0, v0}}, {LINK}->bind(Landroid/content/Context;Landroid/os/Handler;)V")
        text = prepend(text, "onPause()V", f"    invoke-static {{}}, {LINK}->release()V\n    invoke-static {{}}, {MAIN}->mvtbotPauseLink()V")
        text = prepend(text, "onResume()V", f"    const/4 v0, 0x1\n    invoke-static {{v0}}, {LINK}->foreground(Z)V")
        text = prepend(text, "onDestroy()V", f"    invoke-static {{}}, {LINK}->destroy()V")
        text = wrap(text, "setState(Z)V", f"""
    .locals 3
    invoke-static {{}}, {LINK}->isOwned()Z
    move-result v0
    if-eqz v0, :link_legacy
    invoke-static {{}}, {LINK}->isReady()Z
    move-result v0
    sput-boolean v0, {MAIN}->isConnected:Z
    iget-object v1, p0, {MAIN}->bluetoothBtn:Landroid/widget/ImageButton;
    if-eqz v0, :link_disconnected
    const v2, 0x7f0700d9
    goto :link_icon
    :link_disconnected
    const v2, 0x7f070116
    :link_icon
    invoke-virtual {{v1, v2}}, Landroid/widget/ImageButton;->setBackgroundResource(I)V
    return-void
    :link_legacy
    invoke-direct {{p0, p1}}, {MAIN}->setState$v21(Z)V
    return-void
""")
        # The v21 MiniBalan page's bluetoothReflectsConnection flag also redraws
        # the connected icon during page/resume refresh, independently of setState.
        text = wrap(text, "viewChangeButtons(Lcom/Wonder/bot/Device;)V", f"""
    .locals 1
    invoke-direct {{p0, p1}}, {MAIN}->viewChangeButtons$v21(Lcom/Wonder/bot/Device;)V
    invoke-static {{}}, {LINK}->isOwned()Z
    move-result v0
    if-eqz v0, :link_done
    invoke-static {{}}, {LINK}->isReady()Z
    move-result v0
    invoke-direct {{p0, v0}}, {MAIN}->setState(Z)V
    :link_done
    return-void
""")
        text = wrap(text, "switchToBluetoothDevicePage(Ljava/lang/String;)V", f"""
    .locals 1
{owned_guard()}
    invoke-direct {{p0, p1}}, {MAIN}->switchToBluetoothDevicePage$v21(Ljava/lang/String;)V
    return-void
""")
        if ui_fixed:
            # APP-UI-001 already forces p1 to MINIBALAN. Insert AFTER that exact
            # instruction, preserving its unique occurrence and persistence order.
            # Never inspect the incoming p1 (which may be an obsolete saved robot).
            signature = "selectDevice(Lcom/Wonder/bot/Device;)V"
            original = find_method(text, signature)
            force = "    sget-object p1, Lcom/Wonder/bot/Device;->MINIBALAN:Lcom/Wonder/bot/Device;"
            fixed_hook = f"""
    invoke-static {{}}, {LINK}->pageChanged()V
    invoke-static {{}}, {LINK}->isOwned()Z
    move-result v0
    if-nez v0, :link_fixed_mini
    invoke-static {{}}, {LINK}->selectMini()V
    const/4 v0, 0x0
    sput-boolean v0, {MAIN}->isConnected:Z
    sget-object v0, {MAIN}->bleManager:{MAN}
    if-eqz v0, :link_fixed_mini
    invoke-virtual {{v0}}, {MAN}->stop$v21()V
    :link_fixed_mini
"""
            changed = replace_once(original, force, force + "\n" + fixed_hook)
            text = replace_once(text, original, changed)
        else:
            text = prepend(text, "selectDevice(Lcom/Wonder/bot/Device;)V", f"""
    invoke-static {{}}, {LINK}->pageChanged()V
    sget-object v0, Lcom/Wonder/bot/Device;->MINIBALAN:Lcom/Wonder/bot/Device;
    if-ne p1, v0, :link_other_robot
    invoke-static {{}}, {LINK}->isOwned()Z
    move-result v0
    if-nez v0, :link_stay_mini
    invoke-static {{}}, {LINK}->selectMini()V
    const/4 v0, 0x0
    sput-boolean v0, {MAIN}->isConnected:Z
    sget-object v0, {MAIN}->bleManager:{MAN}
    if-eqz v0, :link_stay_mini
    invoke-virtual {{v0}}, {MAN}->stop$v21()V
    goto :link_stay_mini
    :link_other_robot
    invoke-static {{}}, {LINK}->isOwned()Z
    move-result v0
    if-eqz v0, :link_stay_mini
    const/4 v0, 0x0
    sput-boolean v0, {MAIN}->isConnected:Z
    invoke-static {{}}, {LINK}->leaveForLegacy()V
    :link_stay_mini
""")
        text += f"""

.method public static mvtbotMiniSelected()Z
    .locals 2
    sget v0, {MAIN}->CurrentEquipment:I
    sget-object v1, Lcom/Wonder/bot/Device;->MINIBALAN:Lcom/Wonder/bot/Device;
    invoke-virtual {{v1}}, Lcom/Wonder/bot/Device;->id()I
    move-result v1
    if-ne v0, v1, :link_no
    const/4 v0, 0x1
    return v0
    :link_no
    const/4 v0, 0x0
    return v0
.end method

.method private static mvtbotPauseLink()V
    .locals 1
    const/4 v0, 0x0
    invoke-static {{v0}}, {LINK}->foreground(Z)V
    return-void
.end method

.method private static mvtbotManualBlePermissions(Landroid/app/Activity;)Z
    .locals 1
    invoke-static {{}}, {MAIN}->mvtbotMiniSelected()Z
    move-result v0
    if-eqz v0, :link_legacy
    invoke-static {{p0}}, Lcom/mvtbot/link/ManualBlePermissions;->ensure(Landroid/app/Activity;)Z
    move-result v0
    return v0
    :link_legacy
    invoke-static {{p0}}, Lcom/Wonder/bot/utils/PermissionUtils;->mayRequestLocation(Landroid/app/Activity;)Z
    move-result v0
    return v0
.end method
"""
    elif relative == BASE + "fragment/MiniBalan/BalanceCarControlFragment.smali":
        text = text.replace("# interfaces\n", "# interfaces\n.implements Lcom/mvtbot/link/MiniBalanLink$ControlReset;\n", 1)
        if "MiniBalanLink$ControlReset" not in text:
            raise ValueError("control interface anchor missing")
        text = replace_method(text, "sendMoveCmd()V", f"""
    .locals 2
    iget v0, p0, {CTRL}->moveCmd:I
    iget v1, p0, {CTRL}->turnCmd:I
    if-nez v1, :link_motion
    iget v1, p0, {CTRL}->turnCmd2:I
    :link_motion
    invoke-static {{p0, v0, v1}}, {LINK}->motion(Lcom/mvtbot/link/MiniBalanLink$ControlReset;II)V
    return-void
""")
        text = prepend(text, "onViewCreated(Landroid/view/View;Landroid/os/Bundle;)V", f"    invoke-static {{p0, p1}}, {LINK}->bindControl(Lcom/mvtbot/link/MiniBalanLink$ControlReset;Landroid/view/View;)V")
        text = prepend(text, "onDestroy()V", f"    invoke-virtual {{p0}}, {CTRL}->mvtbotStopSensors()V\n    invoke-static {{p0}}, {LINK}->unbindControl(Lcom/mvtbot/link/MiniBalanLink$ControlReset;)V")
        text = prepend(text, "changeControlMode(I)V", f"    invoke-static {{p0, p1}}, {LINK}->controlMode(Lcom/mvtbot/link/MiniBalanLink$ControlReset;I)V")
        # A direction event is a fresh complete vector, not a previous direction plus a new axis.
        text = prepend(text, "onDirection(I)V", f"    invoke-virtual {{p0}}, {CTRL}->resetMotion()V")
        text = wrap(text, "onTouch(Landroid/view/View;Landroid/view/MotionEvent;)Z", f"""
    .locals 2
    invoke-virtual {{p2}}, Landroid/view/MotionEvent;->getActionMasked()I
    move-result v0
    const/4 v1, 0x1
    if-eq v0, v1, :link_release
    const/4 v1, 0x3
    if-ne v0, v1, :link_original
    :link_release
    invoke-virtual {{p0}}, {CTRL}->resetMotion()V
    invoke-static {{}}, {LINK}->release()V
    :link_original
    invoke-virtual {{p0, p1, p2}}, {CTRL}->onTouch$v21(Landroid/view/View;Landroid/view/MotionEvent;)Z
    move-result v0
    return v0
""")
        # Keep the original XYZ labels, replace all old partial-vector/rounding logic.
        sensor = find_method(text, "onSensorChanged(Landroid/hardware/SensorEvent;)V")
        prefix = sensor.split("    .line 154\n", 1)[0]
        if prefix == sensor:
            raise ValueError("sensor presentation boundary missing")
        prefix = prefix.replace(":cond_4", ":link_sensor_done")
        prefix = replace_once(prefix, "    .locals 9", f"""    .locals 9
    invoke-static {{p0}}, {LINK}->isControlOwner(Lcom/mvtbot/link/MiniBalanLink$ControlReset;)Z
    move-result v8
    if-eqz v8, :link_sensor_done""")
        text = replace_once(text, sensor, prefix + f"""    invoke-static {{p0, v0, v2}}, {LINK}->gravity(Lcom/mvtbot/link/MiniBalanLink$ControlReset;FF)I
    move-result v0
    iget-object v1, p0, {CTRL}->handShake:Lcom/Wonder/bot/view/HandShake;
    if-eqz v1, :link_sensor_done
    invoke-virtual {{v1, v0}}, Lcom/Wonder/bot/view/HandShake;->setOriention(I)V
    :link_sensor_done
    return-void
.end method""")
        text += f"""

.method public resetMotion()V
    .locals 1
    const/4 v0, 0x0
    iput v0, p0, {CTRL}->moveCmd:I
    iput v0, p0, {CTRL}->turnCmd:I
    iput v0, p0, {CTRL}->turnCmd2:I
    const/4 v0, -0x2
    iput v0, p0, {CTRL}->curDirection:I
    return-void
.end method

.method public onPause()V
    .locals 0
    invoke-virtual {{p0}}, {CTRL}->mvtbotStopSensors()V
    invoke-static {{}}, {LINK}->release()V
    invoke-super {{p0}}, Landroidx/fragment/app/Fragment;->onPause()V
    return-void
.end method

.method public onHiddenChanged(Z)V
    .locals 0
    invoke-super {{p0, p1}}, Landroidx/fragment/app/Fragment;->onHiddenChanged(Z)V
    if-eqz p1, :link_done
    invoke-static {{}}, {LINK}->release()V
    :link_done
    return-void
.end method

.method public onResume()V
    .locals 1
    invoke-super {{p0}}, Landroidx/fragment/app/Fragment;->onResume()V
    iget v0, p0, {CTRL}->controlMode:I
    invoke-direct {{p0, v0}}, {CTRL}->changeControlMode(I)V
    return-void
.end method

.method public onDestroyView()V
    .locals 0
    invoke-virtual {{p0}}, {CTRL}->mvtbotStopSensors()V
    invoke-static {{p0}}, {LINK}->unbindControl(Lcom/mvtbot/link/MiniBalanLink$ControlReset;)V
    invoke-super {{p0}}, Landroidx/fragment/app/Fragment;->onDestroyView()V
    return-void
.end method

.method public mvtbotStopSensors()V
    .locals 1
    iget-object v0, p0, {CTRL}->sensorManager:Landroid/hardware/SensorManager;
    if-eqz v0, :link_done
    invoke-virtual {{v0, p0}}, Landroid/hardware/SensorManager;->unregisterListener(Landroid/hardware/SensorEventListener;)V
    :link_done
    return-void
.end method
"""
    elif relative == BASE + "fragment/MiniBalan/BalanceCarHomePageFragment.smali":
        text = prepend(text, "setTabSelection(I)V", f"    invoke-static {{}}, {LINK}->pageChanged()V")
        text = wrap(text, "handleRecv(Ljava/lang/String;)V", """
    .locals 1
    invoke-static {p1}, Lcom/mvtbot/link/ProtocolValidation;->validTelemetry(Ljava/lang/String;)Z
    move-result v0
    if-eqz v0, :link_done
    :link_try
    invoke-virtual {p0, p1}, Lcom/Wonder/bot/fragment/MiniBalan/BalanceCarHomePageFragment;->handleRecv$v21(Ljava/lang/String;)V
    :link_end
    .catch Ljava/lang/RuntimeException; {:link_try .. :link_end} :link_reject
    :link_done
    return-void
    :link_reject
    move-exception v0
    return-void
""")
        text += f"""

.method public onHiddenChanged(Z)V
    .locals 0
    invoke-super {{p0, p1}}, Landroidx/fragment/app/Fragment;->onHiddenChanged(Z)V
    if-eqz p1, :link_done
    invoke-static {{}}, {LINK}->release()V
    :link_done
    return-void
.end method
"""
    elif relative == BASE + "dialog/SearchDeviceDialog.smali":
        cls = "Lcom/Wonder/bot/dialog/SearchDeviceDialog;"
        create = "createDialog(Landroid/app/Activity;IILcom/Wonder/bot/dialog/SearchDeviceDialog$OnDeviceSelectedListener;)Lcom/Wonder/bot/dialog/SearchDeviceDialog;"
        factory = find_method(text, create)
        # Preserve the exact legacy method for other robots. Mini computes dimensions
        # first and constructs once; constructors never start its scan.
        construction = f"    new-instance v1, {cls}\n\n    invoke-direct {{v1, p0, v0, p1, p2}}, {cls}-><init>(Landroid/app/Activity;Landroid/view/View;II)V"
        if factory.count(construction) != 2:
            raise ValueError("expected two v21 picker constructions")
        factory = factory.replace(construction, "")
        factory = replace_once(factory, "    move-result p1\n\n    if-eqz p1, :cond_0", "    move-result v1\n\n    if-eqz v1, :cond_0")
        factory = replace_once(factory, f"    :cond_0\n    iput-object v0, v1, {cls}->view:Landroid/view/View;",
                               f"    :cond_0\n{construction}\n\n    iput-object v0, v1, {cls}->view:Landroid/view/View;")
        body = factory.split("\n", 1)[1].rsplit("\n.end method", 1)[0]
        body = replace_once(body, "    .locals 3", f"""    .locals 3
    invoke-static {{}}, {MAIN}->mvtbotMiniSelected()Z
    move-result v1
    if-eqz v1, :link_legacy""")
        body += f"""
    :link_legacy
    invoke-static {{p0, p1, p2, p3}}, {cls}->createDialog$v21(Landroid/app/Activity;IILcom/Wonder/bot/dialog/SearchDeviceDialog$OnDeviceSelectedListener;)Lcom/Wonder/bot/dialog/SearchDeviceDialog;
    move-result-object v1
    return-object v1
"""
        text = wrap(text, create, body)
        # scanBLEDevice is also called by research; anchor the constructor method only.
        constructor = find_method(text, "<init>(Landroid/app/Activity;Landroid/view/View;II)V")
        updated = replace_once(constructor, f"    invoke-virtual {{p0}}, {cls}->scanBLEDevice()V", f"""    invoke-static {{}}, {MAIN}->mvtbotMiniSelected()Z
    move-result p1
    if-nez p1, :link_constructor_done
    invoke-virtual {{p0}}, {cls}->scanBLEDevice()V
    :link_constructor_done""")
        text = replace_once(text, constructor, updated)
        text = wrap(text, "showDialog()V", f"""
    .locals 1
    invoke-virtual {{p0}}, {cls}->showDialog$v21()V
    invoke-static {{}}, {MAIN}->mvtbotMiniSelected()Z
    move-result v0
    if-eqz v0, :link_done
    invoke-virtual {{p0}}, {cls}->scanBLEDevice()V
    :link_done
    return-void
""")
        text = wrap(text, "scanBLEDevice()V", f"""
    .locals 4
    invoke-static {{}}, {MAIN}->mvtbotMiniSelected()Z
    move-result v0
    if-eqz v0, :link_legacy
    iget-object v0, p0, {cls}->mBluetoothAdapter:Landroid/bluetooth/BluetoothAdapter;
    iget-object v1, p0, {cls}->leCallBack:Landroid/bluetooth/BluetoothAdapter$LeScanCallback;
    iget-object v2, p0, {cls}->titleTV:Landroid/widget/TextView;
    iget-object v3, p0, {cls}->progressView:Lcom/Wonder/bot/component/CircularProgressView;
    invoke-virtual {{v3}}, Lcom/Wonder/bot/component/CircularProgressView;->resetAnimation()V
    invoke-static {{p0, v0, v1, v2, v3}}, {SCAN}->start(Landroid/widget/PopupWindow;Landroid/bluetooth/BluetoothAdapter;Landroid/bluetooth/BluetoothAdapter$LeScanCallback;Landroid/widget/TextView;Landroid/view/View;)V
    return-void
    :link_legacy
    invoke-virtual {{p0}}, {cls}->scanBLEDevice$v21()V
    return-void
""")
        text = wrap(text, "stopScan()V", f"""
    .locals 1
    invoke-static {{}}, {MAIN}->mvtbotMiniSelected()Z
    move-result v0
    if-eqz v0, :link_legacy
    invoke-static {{p0}}, {SCAN}->stop(Landroid/widget/PopupWindow;)V
    return-void
    :link_legacy
    invoke-virtual {{p0}}, {cls}->stopScan$v21()V
    return-void
""")
        # Method annotations precede executable code in a DEX round trip. Insert
        # after the existing Signature annotation, not immediately after .locals.
        selected = find_method(text, "onItemClick(Landroid/widget/AdapterView;Landroid/view/View;IJ)V")
        updated = replace_once(selected, "    .line 278\n",
                               f"    invoke-static {{p0}}, {SCAN}->stop(Landroid/widget/PopupWindow;)V\n\n    .line 278\n")
        text = replace_once(text, selected, updated)
        text += f"""

.method public dismiss()V
    .locals 0
    invoke-static {{p0}}, {SCAN}->stop(Landroid/widget/PopupWindow;)V
    invoke-super {{p0}}, Landroid/widget/PopupWindow;->dismiss()V
    return-void
.end method
"""
    elif relative == BASE + "dialog/SearchDeviceDialog$1.smali":
        # Only MiniBalan changes its inclusion policy; other robot lists retain v21 behavior.
        cls = "Lcom/Wonder/bot/dialog/SearchDeviceDialog$1;"
        dialog = "Lcom/Wonder/bot/dialog/SearchDeviceDialog;"
        text = wrap(text, "onLeScan(Landroid/bluetooth/BluetoothDevice;I[B)V", f"""
    .locals 1
    invoke-static {{}}, {MAIN}->mvtbotMiniSelected()Z
    move-result v0
    if-eqz v0, :link_original
    iget-object v0, p0, {cls}->this$0:{dialog}
    invoke-static {{v0}}, {dialog}->access$000({dialog})Lcom/Wonder/bot/dialog/SearchDeviceDialog$BluetoothDataAdapter;
    move-result-object v0
    invoke-virtual {{v0, p1}}, Lcom/Wonder/bot/dialog/SearchDeviceDialog$BluetoothDataAdapter;->add(Landroid/bluetooth/BluetoothDevice;)V
    return-void
    :link_original
    invoke-virtual {{p0, p1, p2, p3}}, {cls}->onLeScan$v21(Landroid/bluetooth/BluetoothDevice;I[B)V
    return-void
""")
    elif relative == BASE + "dialog/SearchDeviceDialog$BluetoothDataAdapter.smali":
        cls = "Lcom/Wonder/bot/dialog/SearchDeviceDialog$BluetoothDataAdapter;"
        text = wrap(text, "add(Landroid/bluetooth/BluetoothDevice;)V", f"""
    .locals 2
    invoke-static {{}}, {MAIN}->mvtbotMiniSelected()Z
    move-result v0
    if-eqz v0, :link_original
    if-eqz p1, :link_done
    iget-object v0, p0, {cls}->devices:Ljava/util/ArrayList;
    invoke-virtual {{v0, p1}}, Ljava/util/ArrayList;->contains(Ljava/lang/Object;)Z
    move-result v1
    if-nez v1, :link_done
    invoke-virtual {{v0, p1}}, Ljava/util/ArrayList;->add(Ljava/lang/Object;)Z
    invoke-virtual {{p0}}, {cls}->notifyDataSetChanged()V
    :link_done
    return-void
    :link_original
    invoke-virtual {{p0, p1}}, {cls}->add$v21(Landroid/bluetooth/BluetoothDevice;)V
    return-void
""")
        text = replace_once(text,
            "    invoke-virtual {p3}, Landroid/bluetooth/BluetoothDevice;->getName()Ljava/lang/String;",
            f"    invoke-static {{p3}}, {LINK}->displayName(Landroid/bluetooth/BluetoothDevice;)Ljava/lang/String;")
    else:
        raise ValueError("no transformation for " + relative)
    return text


TARGETS = [BASE + value for value in (
    "BluetoothConnect/BLEManager.smali", "BluetoothConnect/BLEManager$1.smali", "BluetoothConnect/BLEManager$2.smali", "BluetoothConnect/BLEManager$3.smali", "BluetoothConnect/BLEService.smali",
    "MainActivity.smali", "MainActivity$MsgCallBack.smali", "fragment/MiniBalan/BalanceCarControlFragment.smali", "fragment/MiniBalan/BalanceCarHomePageFragment.smali",
    "dialog/SearchDeviceDialog.smali", "dialog/SearchDeviceDialog$1.smali", "dialog/SearchDeviceDialog$BluetoothDataAdapter.smali",
)]


def canonical_text(data):
    return data.decode("utf-8").replace("\r\n", "\n")


def line_hash_matches(text, expected):
    """Only host line endings may differ from the pinned UI patch's byte hashes."""
    return expected in (digest(text.encode("utf-8")), digest(text.replace("\n", "\r\n").encode("utf-8")))


def load_ui_profile(path):
    if path is None:
        return None, {}
    text = canonical_text(Path(path).read_bytes())
    if digest(text.encode("utf-8")) != UI_PATCH_SHA256:
        raise ValueError("UI patch is not the pinned APP-UI-001 commit " + UI_COMMIT)
    document = json.loads(text)
    entries = {}
    for entry in document["smaliEdits"]:
        path = entry["path"]
        if not path.startswith("smali/"):
            raise ValueError("unexpected pinned UI smali path")
        relative = path[len("smali/"):]
        if relative in entries:
            raise ValueError("duplicate UI smali path")
        entries[relative] = entry
    if set(entries) != {BASE + "StartActivity.smali", BASE + "MainActivity.smali", BASE + "dialog/ContactDialog.smali"}:
        raise ValueError("unexpected pinned UI class set")
    return {"source_commit": UI_COMMIT, "patch_sha256": UI_PATCH_SHA256}, entries


def validate_ui_output(text, edit, baseline_sha=None):
    """Check exact UI output; additionally prove shared MainActivity in both directions."""
    if not line_hash_matches(text, edit["afterSha256"]):
        raise ValueError("exact UI output hash mismatch: " + edit["path"])
    # UI-only classes are not modified here. Their pinned final hash is exact.
    # ContactDialog intentionally maps two different old URLs to the same value,
    # so its text replacements are not uniquely invertible. The UI pipeline has
    # already checked/applied its original before -> after transformation.
    if baseline_sha is None:
        return
    original = text
    for replacement in reversed(edit["replacements"]):
        old = replacement["old"].replace("\r\n", "\n")
        new = replacement["new"].replace("\r\n", "\n")
        original = replace_once(original, new, old)
    if not line_hash_matches(original, edit["beforeSha256"]):
        raise ValueError("UI reconstruction is not the pinned v21 input: " + edit["path"])
    if baseline_sha is not None and digest(original.encode("utf-8")) != baseline_sha:
        raise ValueError("UI reconstruction does not match LINK v21 baseline: " + edit["path"])
    replay = original
    for replacement in edit["replacements"]:
        replay = replace_once(replay, replacement["old"].replace("\r\n", "\n"), replacement["new"].replace("\r\n", "\n"))
    if replay != text:
        raise ValueError("UI exact replay mismatch: " + edit["path"])


def apply(root, ui_patch=None):
    baseline = json.loads(Path(__file__).with_name("smali-baseline.json").read_text(encoding="utf-8"))
    ui_profile, ui_entries = load_ui_profile(ui_patch)
    marker = root / MARKER
    patch_hash = digest(Path(__file__).read_bytes())
    if marker.exists():
        record = json.loads(marker.read_text(encoding="utf-8"))
        if record.get("patch_sha256") != patch_hash:
            raise ValueError("patch version changed; rebuild from the immutable input")
        if record.get("ui_profile") != ui_profile:
            raise ValueError("UI composition mode changed; rebuild from the immutable input")
        for name, sha in record["outputs"].items():
            if digest((root / name).read_bytes()) != sha:
                raise ValueError("patched output changed: " + name)
        for name, sha in record.get("ui_unchanged_outputs", {}).items():
            if digest((root / name).read_bytes()) != sha:
                raise ValueError("composed UI output changed: " + name)
        print("LINK-001 bridge already applied; outputs verified")
        return record
    outputs = {}
    inputs = {}
    ui_unchanged = {}
    for relative, edit in ui_entries.items():
        if relative in TARGETS:
            continue
        data = (root / relative).read_bytes()
        validate_ui_output(canonical_text(data), edit)
        ui_unchanged[relative] = digest(data)
    for relative in TARGETS:
        data = (root / relative).read_bytes()
        # Apktool smali text may use either host line ending; no instruction changes are tolerated.
        canonical = canonical_text(data)
        if relative in ui_entries:
            validate_ui_output(canonical, ui_entries[relative], baseline[relative])
        elif digest(canonical.encode("utf-8")) != baseline[relative]:
            raise ValueError("v21 smali baseline mismatch: " + relative)
        inputs[relative] = digest(canonical.encode("utf-8"))
        outputs[relative] = transform(relative, canonical, ui_fixed=ui_profile is not None).encode("utf-8")
    for relative, data in outputs.items():
        (root / relative).write_bytes(data)
    record = {"patch_version": PATCH_VERSION, "patch_sha256": patch_hash,
              "v21_baseline": baseline, "inputs": inputs, "ui_profile": ui_profile,
              "ui_unchanged_outputs": ui_unchanged, "outputs": {name: digest(data) for name, data in outputs.items()}}
    marker.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(f"LINK-001 bridge applied: {len(outputs)} verified smali files")
    return record


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--smali-root", type=Path, required=True)
    parser.add_argument("--ui-patch", type=Path, help="Pinned APP-UI-001 patch.json, already applied to the fresh snapshot")
    args = parser.parse_args()
    apply(args.smali_root.resolve(), args.ui_patch)


if __name__ == "__main__":
    main()
