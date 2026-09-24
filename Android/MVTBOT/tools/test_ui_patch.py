"""Focused regression checks for the fail-closed APK patch guard."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

_spec = importlib.util.spec_from_file_location("ui_patch", Path(__file__).with_name("Apply-UiPatch.py"))
patch = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(patch)


class PatchGuardTests(unittest.TestCase):
    def test_exact_replacement_preserves_utf8_and_newlines(self):
        before = "矩视\r\nold\r\n".encode()
        after = "矩视\r\nnew\r\n".encode()
        edit = {"path": "res/values/strings.xml", "beforeSha256": patch.digest(before),
                "afterSha256": patch.digest(after), "replacements": [{"old": "old", "new": "new"}]}
        self.assertEqual(patch.exact_edit(before, edit), after)

    def test_missing_duplicate_and_hash_mismatch_are_rejected(self):
        edit = {"path": "res/values/strings.xml", "replacements": [{"old": "old", "new": "new"}]}
        for data in (b"nothing", b"old old"):
            with self.subTest(data=data), self.assertRaises(ValueError):
                patch.exact_edit(data, edit)
        for hash_field in ("beforeSha256", "afterSha256"):
            with self.subTest(hash_field=hash_field), self.assertRaises(ValueError):
                patch.exact_edit(b"old", {**edit, hash_field: "0" * 64})

    def test_traversal_and_absolute_paths_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("../escape", "res/../../escape", "/absolute", "C:/escape", r"res\escape"):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    patch.child(root, name)

    def test_baseline_change_and_untracked_inputs_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "classes.dex").write_bytes(b"original")
            baseline = {"schemaVersion": 1, "versionCode": 21, "fileCount": 1,
                        "files": [{"path": "classes.dex", "bytes": 8, "sha256": patch.digest(b"original")}]}
            self.assertTrue(patch.verify_baseline(root, baseline)["allHashesMatch"])
            (root / "classes.dex").write_bytes(b"modified")
            with self.assertRaises(ValueError):
                patch.verify_baseline(root, baseline)
            (root / "classes.dex").write_bytes(b"original")
            (root / "extra.dex").write_bytes(b"extra")
            with self.assertRaises(ValueError):
                patch.verify_baseline(root, baseline)

    def test_smali_normalization_ignores_encoding_choices_only(self):
        original = '.method public test()V\n.locals 1\nconst-string v0, ":cond_01"\ngoto :goto_01\n:goto_01\nreturn-void\n.end method\n'
        encoded = original.replace("const-string v0", "const-string/jumbo v0").replace("goto :goto_01", "goto/16 :goto_16").replace(":goto_01", ":goto_16")
        self.assertEqual(patch.normalize_smali(original), patch.normalize_smali(encoded))
        changed_string = encoded.replace('":cond_01"', '":cond_02"')
        self.assertNotEqual(patch.normalize_smali(original), patch.normalize_smali(changed_string))
        self.assertNotEqual(patch.normalize_smali(original), patch.normalize_smali(encoded.replace("v0,", "v1,")))
        self.assertNotEqual(patch.normalize_smali(original), patch.normalize_smali(encoded.replace("return-void", "throw v0")))

    def test_smali_field_types_and_branch_targets_are_not_ignored(self):
        field = ".field private name:Ljava/lang/String;"
        self.assertNotEqual(patch.normalize_smali(field), patch.normalize_smali(field.replace("String", "Object")))
        branches = "if-eqz v0, :cond_1\nif-eqz v1, :cond_2\n:cond_1\nreturn v0\n:cond_2\nreturn v1"
        changed = branches.replace("v0, :cond_1", "v0, :cond_2").replace("v1, :cond_2", "v1, :cond_1")
        self.assertNotEqual(patch.normalize_smali(branches), patch.normalize_smali(changed))

    def test_only_typed_static_default_elision_is_equivalent(self):
        for kind, value in (("Z", "false"), ("I", "0x0"), ("J", "0x0L"),
                            ("Ljava/lang/Object;", "null"), ("[I", "null"),
                            ("[[Ljava/lang/String;", "null")):
            declaration = f".field private static final value:{kind}"
            with self.subTest(kind=kind):
                self.assertEqual(patch.normalize_smali(declaration + " = " + value), patch.normalize_smali(declaration))

    def test_static_default_normalization_preserves_other_values(self):
        for kind, value in (("Z", "true"), ("I", "0x1"), ("J", "0x1L"),
                            ("F", "-0.0f"), ("D", "-0.0"), ("I", "null"),
                            ("Ljava/lang/String;", '"null"'), ("Ljava/lang/String;", '"false"')):
            declaration = f".field private static value:{kind}"
            with self.subTest(kind=kind, value=value):
                self.assertNotEqual(patch.normalize_smali(declaration + " = " + value), patch.normalize_smali(declaration))
        instance = ".field private value:Z"
        self.assertNotEqual(patch.normalize_smali(instance + " = false"), patch.normalize_smali(instance))


if __name__ == "__main__":
    unittest.main()
