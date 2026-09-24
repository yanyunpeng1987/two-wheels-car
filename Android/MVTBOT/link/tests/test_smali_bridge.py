#!/usr/bin/env python3
"""Replay patch application, exact-baseline rejection, and lifecycle hook checks."""
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile

MODULE = Path(__file__).resolve().parents[2]
PATCH = MODULE / 'changes/LINK-001/patch_smali.py'
spec = importlib.util.spec_from_file_location('link_patch', PATCH)
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def expect_error(run, message):
    try:
        run()
        raise AssertionError('unexpected success: ' + message)
    except ValueError as error:
        assert message in str(error), str(error)


def copy_inputs(reference, root, targets):
    for relative in targets:
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(reference / relative, target)


def replay_ui(data, edit):
    text = patch.canonical_text(data)
    assert patch.line_hash_matches(text, edit['beforeSha256'])
    for replacement in edit['replacements']:
        text = patch.replace_once(text, replacement['old'].replace('\r\n', '\n'), replacement['new'].replace('\r\n', '\n'))
    assert patch.line_hash_matches(text, edit['afterSha256'])
    return text


def replay_ui_resource(data, edit):
    # The archived XML string tables contain intentional mixed line endings.
    # Preserve raw bytes exactly, as the original UI pipeline does.
    assert patch.digest(data) == edit['beforeSha256']
    for replacement in edit['replacements']:
        old, new = replacement['old'].encode('utf-8'), replacement['new'].encode('utf-8')
        assert data.count(old) == replacement.get('count', 1)
        data = data.replace(old, new)
    assert patch.digest(data) == edit['afterSha256']
    return data


def combined_replay(reference):
    ui_path = MODULE / 'changes/APP-UI-001/patch.json'
    document = json.loads(ui_path.read_text(encoding='utf-8'))
    verifier_spec = importlib.util.spec_from_file_location('ui_verify', ui_path.with_name('verify_release.py'))
    verifier = importlib.util.module_from_spec(verifier_spec)
    verifier_spec.loader.exec_module(verifier)
    for newline in ('\n', '\r\n'):
        with tempfile.TemporaryDirectory(prefix='mvtbot-composed-bridge-') as temp:
            snapshot = Path(temp)
            root = snapshot / 'smali'
            targets = sorted(set(patch.TARGETS) | {edit['path'][len('smali/'):] for edit in document['smaliEdits']})
            copy_inputs(reference, root, targets)
            for edit in document['smaliEdits']:
                file = snapshot / edit['path']
                text = replay_ui(file.read_bytes(), edit)
                file.write_bytes(text.replace('\n', newline).encode('utf-8'))
            for edit in document['resourceEdits']:
                file = snapshot / edit['path']
                file.parent.mkdir(parents=True, exist_ok=True)
                data = replay_ui_resource((MODULE / 'project' / edit['path']).read_bytes(), edit)
                file.write_bytes(data)
            record = patch.apply(root, ui_path)
            assert record['ui_profile']['source_commit'] == 'aa000240b183a8daeed3f09ca084cdf9335e0076'
            assert patch.apply(root, ui_path) == record
            verifier.check_smali(root)
            verifier.check_layouts(MODULE / 'project', snapshot)
            verifier.check_strings(snapshot)
            main = (root / patch.BASE / 'MainActivity.smali').read_text(encoding='utf-8')
            select = patch.find_method(main, 'selectDevice(Lcom/Wonder/bot/Device;)V')
            assert select.count('->MINIBALAN:') == 1
            assert select.index('sget-object p1,') < select.index('->pageChanged()V') < select.index('->selectMini()V') < select.index('->save(')
            assert 'leaveForLegacy' not in select, 'obsolete incoming Other mode must never leave MiniBalan'
            manager = (root / patch.BASE / 'BluetoothConnect/BLEManager.smali').read_text()
            destroy = patch.find_method(manager, 'destroy()V')
            assert destroy.index('->unregister(') < destroy.index('->mReceiver:')
            expect_error(lambda: patch.apply(root), 'UI composition mode changed')
            # Preserve and verify UI-only files that LINK does not rewrite.
            startup = root / patch.BASE / 'StartActivity.smali'
            saved = startup.read_bytes()
            startup.write_bytes(saved + b'\n# changed UI-only output\n')
            expect_error(lambda: patch.apply(root, ui_path), 'composed UI output changed')
            startup.write_bytes(saved)
            main_path = root / patch.BASE / 'MainActivity.smali'
            main_saved = main_path.read_bytes()
            main_path.write_bytes(main_saved + b'\n# changed combined output\n')
            expect_error(lambda: patch.apply(root, ui_path), 'patched output changed')
            main_path.write_bytes(main_saved)
            changed_patch = snapshot / 'altered-patch.json'
            changed_patch.write_bytes(ui_path.read_bytes().replace(b'"schemaVersion": 1', b'"schemaVersion": 2', 1))
            expect_error(lambda: patch.apply(root, changed_patch), 'not the pinned')
    # No mutation occurs when an unapproved UI edit is presented as a composed input.
    with tempfile.TemporaryDirectory(prefix='mvtbot-composed-input-reject-') as temp:
        root = Path(temp)
        copy_inputs(reference, root, targets)
        for edit in document['smaliEdits']:
            file = root / edit['path'][len('smali/'):]
            file.write_text(replay_ui(file.read_bytes(), edit), encoding='utf-8')
        main_path = root / patch.BASE / 'MainActivity.smali'
        main_path.write_text(main_path.read_text() + '\n# unauthorized MainActivity change\n')
        untouched = {name: (root / name).read_bytes() for name in targets}
        expect_error(lambda: patch.apply(root, ui_path), 'exact UI output hash mismatch')
        assert all((root / name).read_bytes() == data for name, data in untouched.items())
    print('UI + LINK composition: PASS (LF/CRLF, original UI checks, pinned provenance, mode/tamper refusal)')


def main():
    reference = MODULE / 'materials/analysis/original-base-apktool/smali'
    with tempfile.TemporaryDirectory(prefix='mvtbot-bridge-test-') as temp:
        root = Path(temp)
        for relative in patch.TARGETS:
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(reference / relative, target)
        record = patch.apply(root)
        assert patch.apply(root) == record
        manager = (root / patch.BASE / 'BluetoothConnect/BLEManager.smali').read_text()
        main = (root / patch.BASE / 'MainActivity.smali').read_text()
        control = (root / patch.BASE / 'fragment/MiniBalan/BalanceCarControlFragment.smali').read_text()
        assert '->mvtbotMiniSelected()Z' in manager
        assert '->isReady()Z' in manager and '->connect$v21' in manager
        assert '->bind(Landroid/content/Context;Landroid/os/Handler;)V' in main
        assert '->foreground(Z)V' in main and '->destroy()V' in main
        assert '.implements Lcom/mvtbot/link/MiniBalanLink$ControlReset;' in control
        assert '->gravity(Lcom/mvtbot/link/MiniBalanLink$ControlReset;FF)I' in control
        assert '->motion(Lcom/mvtbot/link/MiniBalanLink$ControlReset;II)V' in control
        assert 'onPause()V' in control and 'onHiddenChanged(Z)V' in control
        assert 'onDestroyView()V' in control and '->unregisterListener' in control and 'onResume()V' in control
        final_handler = (root / patch.BASE / 'MainActivity$MsgCallBack.smali').read_text()
        assert '->acceptUiEvent(Landroid/os/Message;)Z' in final_handler
        service = (root / patch.BASE / 'BluetoothConnect/BLEService.smali').read_text()
        assert service.count('->beginLegacyOperation()Z') == 3
        sender = (root / patch.BASE / 'BluetoothConnect/BLEManager$3.smali').read_text()
        assert '->beginLegacyDispatch()Z' in sender and '->endLegacyDispatch()V' in sender
        assert '->legacyDispatchCurrent()Z' in sender and '->allowStop()Z' in manager
        assert 'write$v21(Ljava/nio/ByteBuffer;J)Z' in manager
        destroy = patch.find_method(manager, 'destroy()V')
        assert destroy.index('->unregister(') < destroy.index('->mReceiver:')
        damaged = root / patch.TARGETS[0]
        damaged.write_text(damaged.read_text() + '\n# unexpected edit\n')
        try:
            patch.apply(root)
            raise AssertionError('modified patched output accepted')
        except ValueError as error:
            assert 'patched output changed' in str(error)
        (root / patch.MARKER).unlink()
        try:
            patch.apply(root)
            raise AssertionError('non-baseline input accepted')
        except ValueError as error:
            assert 'baseline mismatch' in str(error)
    print('Smali bridge replay: PASS (idempotence, hooks, tamper/baseline refusal)')
    combined_replay(reference)


if __name__ == '__main__':
    main()
