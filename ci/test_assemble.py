#!/usr/bin/env python3
"""Runs assemble.py against a mock DarkEdif template, to test the script's mechanics (anchors, brace
matching, renames, generated files). It cannot prove the real template matches; the CI uploads the real
template files as 'template-reference' for that."""
import os, re, shutil, subprocess, sys, tempfile, json

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "extension"))
import gen_ext_json as GEN   # the spec: counts below are derived from it, never hard-coded
HPP = '''#pragma once
class Extension final
{
public:
	RunHeader * rhPtr;
	Edif::Runtime Runtime;
	static const OEFLAGS OEFLAGS = OEFLAGS::VALUE_CAN_BE_PROTECTED | OEFLAGS::NEVER_KILL;
	static const OEPREFS OEPREFS = OEPREFS::NONE;
	Extension(RunObject* rdPtr, const EDITDATA* edPtr, const CreateObjectInfo* cobPtr);
	void ActionExample(int, const TCHAR*);
	REFLAG Handle();
	REFLAG Display();
};
'''
CPP = '''#include "Common.hpp"
Extension::Extension(RunObject* const rdPtr, const EDITDATA* const edPtr, const CreateObjectInfo* const cobPtr)
	: rdPtr(rdPtr)
{
	LinkAction(0, ActionExample);
	LinkCondition(0, ConditionExample);
	LinkExpression(0, ExpressionExample);
}
REFLAG Extension::Handle()
{
	if (x) { y(); }
	return REFLAG::ONE_SHOT;
}
REFLAG Extension::Display()
{
	return REFLAG::NONE;
}
'''
VCX = '<Project>\n<Import Project="..\\Lib\\FusionSDK_AfterMSPropSheets.props" />\n</Project>\n'
ITEMS = '<Project>\n<ItemGroup><ClCompile Include="$(MSBuildThisFileDirectory)Extension.cpp" /></ItemGroup>\n</Project>\n'
LANG = {"About": {"Name": "DarkEdif Template", "Author": "a", "Comment": "c"},
        "ActionMenu": [["Sub", [0, "Ex"]]], "ConditionMenu": [], "ExpressionMenu": [],
        "Actions": [{"Title": "a %0", "Parameters": [["Integer", "p"]]}], "Conditions": [{"Title": "c", "Triggered": False, "Parameters": []}],
        "Expressions": [{"Title": "e(", "Returns": "Integer", "Parameters": []}],
        "Properties": [{"Type": "Checkbox", "Title": "t", "DefaultState": False}, {"Type": "Combo Box", "Title": "c", "Items": ["a"], "DefaultState": 0}]}
JSON = {"Identifier": "EXMP", "Dependencies": [""], "UK English": LANG, "French": LANG}


def put(p, s):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "w", encoding="utf-8").write(s)


def lower_keys(j):
    return {k: ({a.lower(): b for a, b in v.items()} if isinstance(v, dict) else v) for k, v in j.items()}


def run(variant):
    tmp = tempfile.mkdtemp()
    t = os.path.join(tmp, "sdk", "DarkEdif", "DarkEdif Template")
    hpp, cpp, js = HPP, CPP, dict(JSON)
    extra_cpp = None
    real = os.path.join(REPO, "tests", "fixtures", "template")
    if variant == "A":      # the real template files (uploaded from a CI run)
        hpp = open(os.path.join(real, "Extension.hpp"), encoding="utf-8").read()
        cpp = open(os.path.join(real, "Extension.cpp"), encoding="utf-8").read()
        raw_json = open(os.path.join(real, "DarkExt.json"), encoding="utf-8").read()
    else:
        raw_json = None
    if variant == "B":      # no Display() anywhere, Handle(void), lower-case JSON keys
        hpp = hpp.replace("\tREFLAG Display();\n", "")
        cpp = cpp.replace("REFLAG Extension::Handle()", "REFLAG Extension::Handle(void)")
        cpp = cpp[:cpp.index("REFLAG Extension::Display()")]
        js = lower_keys(js)
    if variant == "C":      # Display() defined in another file with another return type
        cpp = cpp[:cpp.index("REFLAG Extension::Display()")]
        extra_cpp = '#include "Common.hpp"\nshort Extension::Display( void )\n{\n\treturn 0;\n}\n'
    put(t + "/Extension.hpp", hpp); put(t + "/Extension.cpp", cpp)
    if extra_cpp:
        put(t + "/Runtime.cpp", extra_cpp)
    if variant == "A":
        put(t + "/Edittime.cpp", open(os.path.join(real, "Edittime.cpp"), encoding="utf-8").read())
    else:
        put(t + "/Edittime.cpp", '#include "Common.hpp"\n#error Add your drawing tech here\nint x;\n')
    put(t + "/DarkEdif Template.Windows.vcxproj", VCX); put(t + "/DarkEdif Template.Shared.vcxitems", ITEMS)
    put(t + "/DarkExt.json", raw_json if raw_json else "{\n\t// DarkEdif style: comments and trailing commas\n" + json.dumps(js, indent="\t")[2:-2] + ",\n\t/* end */\n}\n")
    put(t + "/DarkExt.PostMinify.json", "{}")
    put(t + "/Android/readme.txt", "x")
    out = os.path.join(tmp, "out")
    subprocess.check_call([sys.executable, os.path.join(REPO, "ci", "assemble.py"), "--sdk", os.path.join(tmp, "sdk"),
                           "--repo", REPO, "--out", out], stdout=subprocess.DEVNULL)
    p = os.path.join(tmp, "sdk", "DarkEdif", "Lumen")
    def rd(n): return open(os.path.join(p, n), encoding="utf-8-sig").read()
    errs = []
    def need(c, m):
        if not c: errs.append("[%s] %s" % (variant, m))
    hpp, cpp = rd("Extension.hpp"), rd("Extension.cpp")
    need('#include "LumenState.hpp"' in hpp and "void SetAmbient(float p0);" in hpp and "LumenMembers.inc" not in hpp, "hpp members inlined")
    need("OEFLAGS::MANUAL_DISPLAY_EXT" in hpp and "OEPREFS::BACK_SAVE" in hpp and "VALUE_CAN_BE_PROTECTED" not in hpp and "OEFLAGS::NONE" not in hpp, "oeflags")
    need("LumenCtor.inc" not in cpp and "LinkAction(0, ActionExample)" not in cpp and "LinkExpression(1, HelloWorld)" not in cpp, "ctor include inlined")
    # the SDK's DarkEdifPreBuildTool scans Extension.cpp for literal Link* calls: emulate that scan
    links = re.findall(r"Link(Action|Condition|Expression)\s*\(\s*(\d+)\s*,\s*(\w+)\s*\)", cpp)
    need(len(links) == len(GEN.ACTIONS) + len(GEN.CONDITIONS) + len(GEN.EXPRESSIONS), "pre-build tool would find %d Link calls" % len(links))
    need(("Action", "0", "SetAmbient") in links and ("Expression", str(len(GEN.EXPRESSIONS) - 1), "LastError") in links, "Link calls content")
    need("void CreateLightOnObject(int p0, RunObject* p1," in rd("Extension.hpp") or "void CreateLightOnObject(int p0, RunObject* p1" in rd("Extension.hpp"), "object parameters declared")
    need("LumenHandle()" in cpp and "if (x)" not in cpp and "ONE_SHOT" not in cpp, "handle body")
    if variant == "A":
        # every property name read in the assembled constructor must exist in the generated DarkExt.json
        names = set(re.findall(r'(?:IsPropChecked|GetPropertyStr)\s*\(\s*(?:std::string_view\s*\()?\s*"([^"]+)"', cpp + rd("Extension.hpp")))
        titles = {x["Title"] for x in json.loads(rd("DarkExt.json"))["UK English"]["Properties"]}
        need(names <= titles and names, "property reads %s not all in JSON %s" % (names, titles))
        need("Checkbox within folder" not in cpp and "Editable 6" not in cpp, "template example property reads removed")
        need("\tLumenDisplay();" in cpp and "void Extension::Display()" in cpp, "display body (void in the real template)")
        need("return static_cast<REFLAG>(LumenHandle());" in cpp and "ONE_SHOT" not in cpp.split("Extension::Handle")[1].split("PAUSABLE")[0], "handle body")
    if variant == "B":
        need("REFLAG Display();" in hpp and "REFLAG Extension::Display()" in cpp and "LumenDisplay()" in cpp, "display added")
    if variant == "C":
        need("short Extension::Display( void )" in rd("Runtime.cpp") and "static_cast<short>(LumenDisplay())" in rd("Runtime.cpp"), "display in other file")
        need("LumenDisplay" not in cpp, "display not duplicated")
    need("#error" not in rd("Edittime.cpp") and "frameSurf.FillImageWith" not in rd("Edittime.cpp"), "#error block removed")
    if variant == "A":
        need("CopyToPoint(frameSurf" in rd("Edittime.cpp") and "EditorDisplay" in rd("Edittime.cpp"), "icon drawing kept")
    need("WindowsXPCompatibility = false" in open(os.path.join(tmp, "sdk", "FusionSDKConfig.ini")).read(), "ini in SDK root")
    need(not os.path.exists(os.path.join(p, "DarkExt.PostMinify.json")), "stale PostMinify removed")
    need(os.path.exists(os.path.join(p, "Lumen.Windows.vcxproj")) and os.path.exists(os.path.join(p, "Lumen.Shared.vcxitems")), "renamed files")
    need("FUSION_INTERNAL_ACCESS;DARKEDIF_LOG_MIN_LEVEL=6;" in rd("Lumen.Windows.vcxproj"), "defines")
    need("lumen.cpp" in rd("Lumen.Shared.vcxitems") and "LumenExt.cpp" in rd("Lumen.Shared.vcxitems"), "sources")
    j = json.loads(rd("DarkExt.json"))
    lang = j["UK English"]
    ak, ck = ("about", "actions") if variant == "B" else ("About", "Actions")
    need(j["Identifier"] == "LUMN" and "French" not in j and len(lang[ck]) == len(GEN.ACTIONS) and lang[ak]["Name"] == "Lumen", "json")
    am = lang.get("ActionMenu") or lang.get("actionmenu")
    labels = [i[1] for g in am for i in g[1:]]
    need("Create light on object" in labels and not any("%" in l for l in labels), "friendly menu labels: %s" % labels)
    acts = lang.get("Actions") or lang.get("actions")
    obj = [x for x in acts if x["Title"].startswith("Create light %0 on %1")]
    need(len(obj) == 1 and obj[0]["Parameters"][1][0] == "Object", "object parameter in JSON")
    props = [x["Title"] for x in (lang.get("Properties") or lang.get("properties"))]
    need(props == ["Backdrop obstacles", "Pixel size"], "properties %s" % props)
    for n in ("lumen.hpp", "lumen.cpp", "LumenState.hpp", "LumenExt.cpp", "LumenMembers.inc", "LumenCtor.inc"):
        need(os.path.exists(os.path.join(p, n)), "missing " + n)
    need(os.path.exists(os.path.join(out, "template-reference", "Extension.hpp")), "reference snapshot")
    shutil.rmtree(tmp)
    return errs


def main():
    errs = []
    for v in "ABC":
        errs += run(v)
    if errs:
        print("test_assemble FAILED:", errs); sys.exit(1)
    print("test_assemble ok (3 template shapes)")


main()
