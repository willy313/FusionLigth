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
JSON = {"About": {"Name": "DarkEdif Template", "Identifier": "DARK"},
        "Actions": [{"Title": "a %0", "Parameters": [["Integer", "p"]]}], "Conditions": [{"Title": "c", "Triggered": False, "Parameters": []}],
        "Expressions": [{"Title": "e(", "Returns": "Integer", "Parameters": []}],
        "Properties": [{"Type": "Checkbox", "Title": "t", "DefaultState": False}, {"Type": "Combo Box", "Title": "c", "Items": ["a"], "DefaultState": 0}]}


def put(p, s):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "w", encoding="utf-8").write(s)


def main():
    tmp = tempfile.mkdtemp()
    t = os.path.join(tmp, "sdk", "DarkEdif", "DarkEdif Template")
    put(t + "/Extension.hpp", HPP); put(t + "/Extension.cpp", CPP)
    put(t + "/Edittime.cpp", '#include "Common.hpp"\n#error Add your drawing tech here\nint x;\n')
    put(t + "/DarkEdif Template.Windows.vcxproj", VCX); put(t + "/DarkEdif Template.Shared.vcxitems", ITEMS)
    put(t + "/FusionSDKConfig.ini", "[x]\nWindowsXPCompatibility = true\n")
    put(t + "/DarkExt.json", json.dumps(JSON)); put(t + "/DarkExt.PostMinify.json", "{}")
    put(t + "/Android/readme.txt", "x")
    out = os.path.join(tmp, "out")
    subprocess.check_call([sys.executable, os.path.join(REPO, "ci", "assemble.py"), "--sdk", os.path.join(tmp, "sdk"),
                           "--repo", REPO, "--out", out, "--object-access"])
    p = os.path.join(tmp, "sdk", "DarkEdif", "Lumen")
    def rd(n): return open(os.path.join(p, n), encoding="utf-8-sig").read()
    errs = []
    def need(c, m):
        if not c: errs.append(m)
    hpp, cpp = rd("Extension.hpp"), rd("Extension.cpp")
    need('#include "LumenState.hpp"' in hpp and '#include "LumenMembers.inc"' in hpp, "hpp includes")
    need("OEFLAGS::MANUAL_DISPLAY_EXT" in hpp and "OEPREFS::BACK_SAVE" in hpp and "VALUE_CAN_BE_PROTECTED" not in hpp, "oeflags")
    need(cpp.count("LumenCtor.inc") == 1 and "LinkAction(0, ActionExample)" not in cpp, "ctor include")
    need("return LumenHandle();" in cpp and "return LumenDisplay();" in cpp and "ONE_SHOT" not in cpp and "if (x)" not in cpp, "bodies")
    need("#error" not in rd("Edittime.cpp"), "#error removed")
    need("WindowsXPCompatibility = false" in rd("FusionSDKConfig.ini"), "ini")
    need(not os.path.exists(os.path.join(p, "DarkExt.PostMinify.json")), "stale PostMinify removed")
    need(os.path.exists(os.path.join(p, "Lumen.Windows.vcxproj")) and os.path.exists(os.path.join(p, "Lumen.Shared.vcxitems")), "renamed files")
    need("FUSION_INTERNAL_ACCESS;LUMEN_OBJECT_ACCESS;" in rd("Lumen.Windows.vcxproj"), "defines")
    need("lumen.cpp" in rd("Lumen.Shared.vcxitems") and "LumenExt.cpp" in rd("Lumen.Shared.vcxitems"), "sources")
    j = json.loads(rd("DarkExt.json"))
    need(j["About"]["Identifier"] == "LUMN" and len(j["Actions"]) == 18, "json")
    for n in ("lumen.hpp", "lumen.cpp", "LumenState.hpp", "LumenExt.cpp", "LumenMembers.inc", "LumenCtor.inc"):
        need(os.path.exists(os.path.join(p, n)), "missing " + n)
    need(os.path.exists(os.path.join(out, "template-reference", "Extension.hpp")), "reference snapshot")
    shutil.rmtree(tmp)
    if errs:
        print("test_assemble FAILED:", errs); sys.exit(1)
    print("test_assemble ok")


main()
