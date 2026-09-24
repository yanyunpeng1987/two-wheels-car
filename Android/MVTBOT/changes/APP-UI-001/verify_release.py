#!/usr/bin/env python3
"""Check APP-UI-001 in a decoded release APK; this is static evidence only.

Use Apktool to decode the *final APK*, then pass that directory as --decoded-root.
--smali-root may be the same directory or a separate DEX-only decode. This does
not install or run Android, verify a signing certificate, or test Bluetooth.
"""

import argparse
import copy
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


ANDROID = "{http://schemas.android.com/apk/res/android}"
URL = "http://www.mvtlabs.com/"
COMPANY = "公司：杭州矩视科技有限公司"
COMPONENTS = {"activity", "activity-alias", "service", "receiver", "provider"}


class VerificationError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def parse(path):
    return ET.parse(path).getroot()


def canonical(element):
    return (element.tag, tuple(sorted(element.attrib.items())),
            (element.text or "").strip(), tuple(sorted(canonical(c) for c in element)))


def manifest_components(root):
    application = root.find("application")
    require(application is not None, "Manifest has no application")
    return sorted(canonical(c) for c in application if c.tag in COMPONENTS)


def check_manifest(baseline, decoded):
    before = parse(baseline / "AndroidManifest.xml")
    after = parse(decoded / "AndroidManifest.xml")
    require(after.get("package") == before.get("package") == "com.Wonder.bot",
            "Package identity changed")
    require(manifest_components(after) == manifest_components(before),
            "Manifest components, their attributes, or intent filters changed")
    checked_versions = []
    for key, expected in (("versionCode", "22"), ("versionName", "2.3.6-mvtbot.3")):
        value = after.get(ANDROID + key)
        if value is not None:
            require(value == expected, f"Unexpected manifest {key}: {value}")
            checked_versions.append(key)
    return {"manifestVersionFieldsChecked": checked_versions,
            "note": "Absent manifest version fields require APK badging/build metadata verification."}


def public_ids(root):
    result = {}
    for node in root.findall("public"):
        key = (node.get("type"), node.get("name"))
        require(key not in result, f"Duplicate public resource: {key}")
        result[key] = int(node.attrib["id"], 0)
    require(bool(result), "Public resource table is empty")
    return result


def check_public_maps(before, after):
    require(before == after, "Public resource names, types, or numeric IDs changed")


def check_resource_ids(baseline, decoded):
    before = public_ids(parse(baseline / "res/values/public.xml"))
    after = public_ids(parse(decoded / "res/values/public.xml"))
    check_public_maps(before, after)
    return {"resourceCount": len(after)}


def widgets(root):
    result = {}
    for node in root.iter():
        value = node.get(ANDROID + "id")
        if value:
            # @+id and @id identify the same widget after recompilation.
            name = value.replace("@+id/", "@id/")
            require(name not in result, f"Duplicate widget ID: {name}")
            result[name] = node
    return result


def check_menu(node):
    require(node.get(ANDROID + "visibility") == "gone", "Model menu is visible")
    require(node.get(ANDROID + "enabled") == "false", "Model menu is enabled")


def check_layouts(baseline, decoded):
    layouts = {}
    for name in ("base_content.xml", "layout_contact.xml"):
        before = widgets(parse(baseline / "res/layout" / name))
        after = widgets(parse(decoded / "res/layout" / name))
        require({k: v.tag for k, v in before.items()} ==
                {k: v.tag for k, v in after.items()},
                f"Widget IDs/types changed in {name}; ViewBinding may fail")
        layouts[name] = after
    toolbar = layouts["base_content.xml"]
    check_menu(toolbar["@id/iv_menu"])
    battery = toolbar["@id/batView"]
    require(battery.get(ANDROID + "layout_alignParentStart") == "true" and
            battery.get(ANDROID + "layout_toEndOf") != "@id/iv_menu",
            "Battery remains anchored to the hidden menu")
    contact = layouts["layout_contact.xml"]
    for name in ("logo1_img", "email", "scan_code"):
        require(contact["@id/" + name].get(ANDROID + "visibility") == "gone",
                f"Removed contact content remains visible: {name}")
    require(contact["@id/logo1_img"].get(ANDROID + "src") in (None, "@null"),
            "Removed contact logo still has an image source")
    require(contact["@id/company"].get(ANDROID + "layout_below") != "@id/logo1_img",
            "Company remains anchored to the hidden logo")
    require(contact["@id/wwwlayout"].get(ANDROID + "layout_below") == "@id/company",
            "Website does not follow company directly")
    require(contact["@id/company"].get(ANDROID + "text") == "@string/company" and
            contact["@id/www"].get(ANDROID + "text") == "@string/website_http",
            "Visible company/website does not use the checked strings")
    return {"layoutsChecked": sorted(layouts)}


def check_strings_table(strings, label):
    for name, expected in (("company", COMPANY), ("website_http", URL),
                           ("email", ""), ("webchat_subscription", "")):
        require(strings.get(name) == expected, f"Unexpected {label}/{name}")


def check_strings(decoded):
    locales = ("values", "values-zh-rCN", "values-zh-rTW")
    for locale in locales:
        root = parse(decoded / "res" / locale / "strings.xml")
        strings = {n.get("name"): "".join(n.itertext()) for n in root.findall("string")}
        check_strings_table(strings, locale)
    return {"localeTablesChecked": list(locales)}


def method(source, signature):
    matches = re.findall(r"^\.method[^\n]* " + re.escape(signature) +
                         r"\s*\n(.*?)^\.end method\s*$", source, re.M | re.S)
    require(len(matches) == 1, f"Expected one method: {signature}")
    return matches[0]


def instructions(body):
    return [line.strip() for line in body.splitlines()
            if line.strip() and not line.lstrip().startswith((".", "#"))]


def one_position(lines, fragment):
    positions = [i for i, line in enumerate(lines) if fragment in line]
    require(len(positions) == 1, f"Expected one instruction containing {fragment}")
    return positions[0]


def check_immediate_argument(lines, index, expected):
    """Check the constant actually passed, without depending on register names."""
    call = re.search(r"\{([^}]+)\}", lines[index])
    require(call is not None and index > 0, "Cannot inspect invocation argument")
    argument = call.group(1).split(",")[-1].strip()
    previous = lines[index - 1]
    if isinstance(expected, str):
        assignment = re.fullmatch(r'const-string(?:/jumbo)?\s+(\w+),\s*(".*")', previous)
        value = json.loads(assignment.group(2)) if assignment else None
    else:
        assignment = re.fullmatch(r"const(?:/4|/16)?\s+(\w+),\s*(0x[0-9a-f]+|\d+)", previous)
        value = int(assignment.group(2), 0) if assignment else None
    require(assignment is not None and assignment.group(1) == argument and value == expected,
            f"Invocation argument is not the required constant {expected!r}: {lines[index]}")


def check_smali(root):
    app_roots = [root / "com/Wonder/bot", *root.glob("smali*/com/Wonder/bot")]
    if root.as_posix().endswith("/com/Wonder/bot"):
        app_roots.append(root)
    app_roots = sorted({p.resolve() for p in app_roots if p.is_dir()})
    require(bool(app_roots), "No com/Wonder/bot smali directory found")

    def source(relative):
        paths = [p / relative for p in app_roots if (p / relative).is_file()]
        require(len(paths) == 1, f"Expected one app class: {relative}")
        return paths[0].read_text(encoding="utf-8-sig")

    checked_classes = 0
    for app in app_roots:
        for path in app.rglob("*.smali"):
            text = path.read_text(encoding="utf-8-sig")
            require(not re.search(r"^\s*invoke-\S+[^\n]*->openDrawer\(", text, re.M),
                    f"Application can still open model drawer: {path.name}")
            checked_classes += 1

    startup = instructions(method(source("StartActivity.smali"), "onCreate(Landroid/os/Bundle;)V"))
    require(not any("Lcom/bumptech/glide/" in line or "->postDelayed(" in line
                    for line in startup), "Startup still loads Glide or schedules a delay")
    positions = [one_position(startup, part) for part in (
        "->onCreate(Landroid/os/Bundle;)V", "Lcom/Wonder/bot/MainActivity;",
        "Landroid/content/Intent;-><init>(Landroid/content/Context;Ljava/lang/Class;)V",
        "->startActivity(Landroid/content/Intent;)V", "->finish()V")]
    require(positions == sorted(positions), "Startup lifecycle/Intent call order is incorrect")
    require(not any(re.match(r"(?:if-|goto|packed-switch|sparse-switch)", line)
                    for line in startup), "Startup has an unexpected conditional or deferred route")

    main = source("MainActivity.smali")
    require(instructions(method(main, "onLeftMenuBtn(Landroid/view/View;)V")) == ["return-void"],
            "Menu handler still performs an action")
    enter = instructions(method(main, "enterView(I)V"))
    close = one_position(enter, "->closeDrawer(Landroid/view/View;Z)V")
    lock = one_position(enter, "->setDrawerLockMode(I)V")
    require(close < lock, "Drawer is not closed before locking")
    check_immediate_argument(enter, close, 0)
    check_immediate_argument(enter, lock, 1)
    select = instructions(method(main, "selectDevice(Lcom/Wonder/bot/Device;)V"))
    ordered = [one_position(select, fragment) for fragment in (
        "->MINIBALAN:Lcom/Wonder/bot/Device;", "->cardChoosed:I",
        "->save(Lcom/Wonder/bot/Device;)V",
        "->show(Lcom/Wonder/bot/Device;)Landroidx/fragment/app/Fragment;",
        "->viewChanged(Lcom/Wonder/bot/Device;)V")]
    require(ordered == sorted(ordered), "Fixed model/persistence/navigation order differs")

    contact = instructions(method(source("dialog/ContactDialog.smali"), "<init>(Landroid/view/View;II)V"))
    literals = [json.loads(m.group(1)) for line in contact
                if (m := re.match(r'const-string(?:/jumbo)?\s+\w+,\s*(".*")$', line))]
    require(literals.count(URL) == 2, "Both contact locale branches must contain the requested URL")
    require(not any("hiwonder" in value.lower() or value == "https://" for value in literals),
            "Contact bytecode still overrides website with an old URL/prefix")
    require(any("Landroid/text/style/URLSpan;-><init>" in line for line in contact),
            "Website link span is missing")
    builder = one_position(contact, "Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V")
    check_immediate_argument(contact, builder, "")
    visibility_calls = 0
    for index, line in enumerate(contact):
        if "Landroid/widget/TextView;->setVisibility(I)V" not in line:
            continue
        visibility_calls += 1
        check_immediate_argument(contact, index, 8)
    require(visibility_calls == 2, "Expected both contact locale visibility branches")
    return {"applicationClassesCheckedForDrawerCalls": checked_classes,
            "note": "Call/constant checks are static; whole-DEX semantic-diff verification is a separate build gate."}


def self_test():
    valid = ET.Element("ImageView", {ANDROID + "visibility": "gone", ANDROID + "enabled": "false"})
    check_menu(valid)
    strings = {"company": COMPANY, "website_http": URL, "email": "", "webchat_subscription": ""}
    check_strings_table(strings, "fixture")
    tests = []
    visible = copy.deepcopy(valid)
    visible.set(ANDROID + "visibility", "visible")
    tests.append(("visible menu", lambda: check_menu(visible)))
    wrong_url = dict(strings, website_http="http://www.hiwonder.com/")
    tests.append(("wrong website", lambda: check_strings_table(wrong_url, "fixture")))
    tests.append(("changed resource ID", lambda: check_public_maps(
        {("id", "www"): 0x7F0803F2}, {("id", "www"): 0x7F0803F3})))
    for label, run in tests:
        try:
            run()
        except VerificationError:
            continue
        raise VerificationError(f"Negative self-test was not rejected: {label}")
    return {"negativeCasesRejected": [name for name, _ in tests]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decoded-root", type=Path)
    parser.add_argument("--baseline-project", type=Path)
    parser.add_argument("--smali-root", type=Path)
    parser.add_argument("--report", type=Path, help="Write static verification JSON")
    parser.add_argument("--self-test", action="store_true", help="Run three in-memory negative fixtures")
    args = parser.parse_args()
    if not args.self_test and (args.decoded_root is None or args.baseline_project is None):
        parser.error("--decoded-root and --baseline-project are required unless only --self-test is used")
    if (args.decoded_root is None) != (args.baseline_project is None):
        parser.error("--decoded-root and --baseline-project must be supplied together")
    report = {"schemaVersion": 1, "requirement": "APP-UI-001", "evidenceLevel": "static-only",
              "androidRuntimeTested": False, "deviceInteractionPerformed": False,
              "checks": [], "limitations": ["No Android layout/lifecycle or URL intent execution",
                  "No Bluetooth or vehicle test", "Signing/alignment and unchanged-method verification are separate gates"]}
    jobs = []
    if args.self_test:
        jobs.append(("negative-self-tests", self_test))
    if args.decoded_root is not None:
        jobs.extend([
            ("manifest-identity", lambda: check_manifest(args.baseline_project, args.decoded_root)),
            ("public-resource-ids", lambda: check_resource_ids(args.baseline_project, args.decoded_root)),
            ("layout-and-binding-contracts", lambda: check_layouts(args.baseline_project, args.decoded_root)),
            ("localized-company-and-website", lambda: check_strings(args.decoded_root)),
        ])
    if args.smali_root is not None:
        jobs.append(("compiled-smali-ui-routes", lambda: check_smali(args.smali_root)))
    else:
        report["limitations"].append("DEX checks not requested (--smali-root omitted)")
    for name, run in jobs:
        try:
            detail = run()
            report["checks"].append({"name": name, "passed": True, "detail": detail})
            print(f"PASS {name}")
        except (VerificationError, OSError, ValueError, KeyError, ET.ParseError) as error:
            report["checks"].append({"name": name, "passed": False, "error": str(error)})
            print(f"FAIL {name}: {error}", file=sys.stderr)
    report["passed"] = all(item["passed"] for item in report["checks"])
    if args.report is not None:
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{'PASS' if report['passed'] else 'FAIL'} APP-UI-001 static checks; Android/device behavior not tested")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
