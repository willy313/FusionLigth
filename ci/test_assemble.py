#!/usr/bin/env python3
"""Runs assemble.py against a mock DarkEdif template, to test the script's mechanics (anchors, brace
matching, renames, generated files). It cannot prove the real template matches; the CI uploads the real
template files as 'template-reference' for that."""
import os, shutil, subprocess, sys, tempfile, json

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
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
    put(t + "/Edittime.cpp", '#include "Common.hpp"\n#error Add your drawing tech here\nint x;\n')
    put(t + "/DarkEdif Template.Windows.vcxproj", VCX); put(t + "/DarkEdif Template.Shared.vcxitems", ITEMS)
    put(t + "/DarkExt.json", raw_json if raw_json else "{\n\t// DarkEdif style: comments and trailing commas\n" + json.dumps(js, indent="\t")[2:-2] + ",\n\t/* end */\n}\n")
    put(t + "/DarkExt.PostMinify.json", "{}")
    put(t + "/Android/readme.txt", "x")
    out = os.path.join(tmp, "out")
    subprocess.check_call([sys.executable, os.path.join(REPO, "ci", "assemble.py"), "--sdk", os.path.join(tmp, "sdk"),
                           "--repo", REPO, "--out", out, "--object-access"], stdout=subprocess.DEVNULL)
    p = os.path.join(tmp, "sdk", "DarkEdif", "Lumen")
    def rd(n): return open(os.path.join(p, n), encoding="utf-8-sig").read()
    errs = []
    def need(c, m):
        if not c: errs.append("[%s] %s" % (variant, m))
    hpp, cpp = rd("Extension.hpp"), rd("Extension.cpp")
    need('#include "LumenState.hpp"' in hpp and '#include "LumenMembers.inc"' in hpp, "hpp includes")
    need("OEFLAGS::MANUAL_DISPLAY_EXT" in hpp and "OEPREFS::BACK_SAVE" in hpp and "VALUE_CAN_BE_PROTECTED" not in hpp and "OEFLAGS::NONE" not in hpp, "oeflags")
    need(cpp.count("LumenCtor.inc") == 1 and "LinkAction(0, ActionExample)" not in cpp and "LinkExpression(1, HelloWorld)" not in cpp, "ctor include")
    need("LumenHandle()" in cpp and "if (x)" not in cpp and "ONE_SHOT" not in cpp, "handle body")
    if variant == "A":
        need("\tLumenDisplay();" in cpp and "void Extension::Display()" in cpp, "display body (void in the real template)")
        need("return static_cast<REFLAG>(LumenHandle());" in cpp and "ONE_SHOT" not in cpp.split("Extension::Handle")[1].split("PAUSABLE")[0], "handle body")
    if variant == "B":
        need("REFLAG Display();" in hpp and "REFLAG Extension::Display()" in cpp and "LumenDisplay()" in cpp, "display added")
    if variant == "C":
        need("short Extension::Display( void )" in rd("Runtime.cpp") and "static_cast<short>(LumenDisplay())" in rd("Runtime.cpp"), "display in other file")
        need("LumenDisplay" not in cpp, "display not duplicated")
    need("#error" not in rd("Edittime.cpp"), "#error removed")
    need("WindowsXPCompatibility = false" in open(os.path.join(tmp, "sdk", "FusionSDKConfig.ini")).read(), "ini in SDK root")
    need(not os.path.exists(os.path.join(p, "DarkExt.PostMinify.json")), "stale PostMinify removed")
    need(os.path.exists(os.path.join(p, "Lumen.Windows.vcxproj")) and os.path.exists(os.path.join(p, "Lumen.Shared.vcxitems")), "renamed files")
    need("FUSION_INTERNAL_ACCESS;DARKEDIF_LOG_MIN_LEVEL=6;LUMEN_OBJECT_ACCESS;" in rd("Lumen.Windows.vcxproj"), "defines")
    need("lumen.cpp" in rd("Lumen.Shared.vcxitems") and "LumenExt.cpp" in rd("Lumen.Shared.vcxitems"), "sources")
    j = json.loads(rd("DarkExt.json"))
    lang = j["UK English"]
    ak, ck = ("about", "actions") if variant == "B" else ("About", "Actions")
    need(j["Identifier"] == "LUMN" and "French" not in j and len(lang[ck]) == 18 and lang[ak]["Name"] == "Lumen", "json")
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
