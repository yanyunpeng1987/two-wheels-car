#!/usr/bin/env python3
"""Replay APP-UI-001 then LINK-001 and retain the original UI acceptance checks.

Uses the restored v21 project/reference only as read-only fixtures. The complete
application smali subtree is checked, including renamed legacy methods; no
openDrawer or fixed-model check is skipped for the BLE integration. This does
not build/install an APK or claim Android runtime/vehicle acceptance.
"""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import shutil
import tempfile


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require(condition: bool, reason: str) -> None:
    if not condition:
        raise AssertionError(reason)


def combined_entry_checks(ui_check, root: Path) -> None:
    main = (root / "smali/com/Wonder/bot/MainActivity.smali").read_text(encoding="utf-8")
    select = ui_check.method(main, "selectDevice(Lcom/Wonder/bot/Device;)V")
    require(select.count("->MINIBALAN:Lcom/Wonder/bot/Device;") == 1,
            "Combined selector must keep exactly one fixed MiniBalan assignment")
    require(select.count("->selectMini()V") == 1,
            "Fixed MiniBalan UI must claim the isolated BLE session")
    require(select.index("->MINIBALAN:Lcom/Wonder/bot/Device;")
            < select.index("->selectMini()V")
            < select.index("->show(Lcom/Wonder/bot/Device;)"),
            "Transport ownership must follow the forced model and precede navigation")
    require("->leaveForLegacy()V" not in select,
            "Fixed MiniBalan selector cannot release ownership based on an ignored caller argument")

    manager = (root / "smali/com/Wonder/bot/BluetoothConnect/BLEManager.smali").read_text(encoding="utf-8")
    unregister = ui_check.method(manager, "unregister(Landroid/content/Context;)V")
    require("->registeredContext:Landroid/content/Context;" in unregister,
            "APP-002 must unregister on the saved registration Context")
    require("Lcom/mvtbot/link/MiniBalanLink;->unregister(Landroid/content/Context;Landroid/content/BroadcastReceiver;)V" in unregister,
            "Receiver cleanup must use the guarded same-Context helper")
    require("sput-boolean" in unregister and "->isRegistered:Z" in unregister,
            "Receiver cleanup must clear the static registration state")
    destroy = ui_check.method(manager, "destroy()V")
    require("->unregister(Landroid/content/Context;)V" in destroy,
            "Singleton destruction must unregister before losing its receiver")
    require(destroy.index("->unregister(Landroid/content/Context;)V")
            < destroy.index("->mReceiver:Landroid/content/BroadcastReceiver;"),
            "Destroy discarded the registered receiver before cleanup")


def main() -> None:
    module = Path(__file__).resolve().parents[2]
    project = module / "project"
    reference = module / "materials/analysis/original-base-apktool/smali"
    ui_path = module / "changes/APP-UI-001/patch.json"
    ui_patch = json.loads(ui_path.read_text(encoding="utf-8-sig"))
    ui_apply = load("ui_exact_apply", module / "tools/Apply-UiPatch.py")
    ui_check = load("original_ui_checks", module / "changes/APP-UI-001/verify_release.py")
    link = load("link_bridge", module / "changes/LINK-001/patch_smali.py")

    with tempfile.TemporaryDirectory(prefix="mvtbot-combined-ui-link-") as directory:
        root = Path(directory)
        shutil.copytree(reference / "com/Wonder/bot", root / "smali/com/Wonder/bot")
        resource_inputs = {edit["path"] for edit in ui_patch["resourceEdits"]}
        resource_inputs.add("res/values/public.xml")
        for relative in resource_inputs:
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(project / relative, target)
        for kind in ("resourceEdits", "smaliEdits"):
            for edit in ui_patch[kind]:
                target = root / edit["path"]
                target.write_bytes(ui_apply.exact_edit(target.read_bytes(), edit))
        ui_only = ui_check.check_smali(root)
        record = link.apply(root / "smali", ui_path)
        require(link.apply(root / "smali", ui_path) == record,
                "Combined patch is not idempotent")
        combined = ui_check.check_smali(root)
        require(ui_only["applicationClassesCheckedForDrawerCalls"]
                == combined["applicationClassesCheckedForDrawerCalls"],
                "Combined verification lost application classes")
        ui_check.check_resource_ids(project, root)
        ui_check.check_layouts(project, root)
        ui_check.check_strings(root)
        combined_entry_checks(ui_check, root)

        # The unchanged UI verifier must still catch a drawer route anywhere in
        # the composed class, including a renamed legacy method.
        main_path = root / "smali/com/Wonder/bot/MainActivity.smali"
        main_source = main_path.read_text(encoding="utf-8")
        main_path.write_text(main_source.replace(
            "->closeDrawer(Landroid/view/View;Z)V",
            "->openDrawer(Landroid/view/View;Z)V", 1), encoding="utf-8")
        try:
            ui_check.check_smali(root)
        except ui_check.VerificationError:
            pass
        else:
            raise AssertionError("Composed UI accepted an openDrawer regression")

        # UI-only checks intentionally do not know the transport. The independent
        # composition check must detect a lost ownership hook even if UI passes.
        main_path.write_text(main_source.replace("->selectMini()V", "->leaveForLegacy()V", 1),
                             encoding="utf-8")
        ui_check.check_smali(root)
        try:
            combined_entry_checks(ui_check, root)
        except AssertionError:
            pass
        else:
            raise AssertionError("Composed UI accepted a missing BLE ownership hook")
        main_path.write_text(main_source, encoding="utf-8")
        combined_entry_checks(ui_check, root)

    print("Combined UI/LINK replay: PASS (original UI checks, full application drawer scan, "
          "fixed Mini ownership, APP-002 lifecycle cleanup, 2 negative composition cases)")


if __name__ == "__main__":
    main()
