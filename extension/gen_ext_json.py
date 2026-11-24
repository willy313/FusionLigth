#!/usr/bin/env python3
"""Lumen: single source of truth for the extension's actions, conditions and expressions.

Generates, from the SPEC below:
  DarkExt.json        built on top of the DarkEdif template's own DarkExt.json, so any field this
                      script does not know about is inherited from the template's example entries
  LumenMembers.inc    member declarations, #included inside `class Extension`
  LumenCtor.inc       LinkAction/LinkCondition/LinkExpression calls + init, #included in the constructor
  docs/API.md         human readable reference

Usage:
  gen_ext_json.py --template <template DarkExt.json> --out <project dir> [--docs <API.md>]
  gen_ext_json.py --selfcheck        (offline: consistency checks against LumenExt.cpp)
"""
import argparse, copy, json, os, re, sys

IDENT = "LUMN"          # four characters, unique. Never change after release.
NAME = "Lumen"
AUTHOR = "Lumen contributors"
COMMENT = "Fast 2D lighting with shadows: ambient darkness, many lights, obstacle and object shadows."

I, F, T = "Integer", "Float", "Text"
CTYPE = {I: "int", F: "float", T: "const TCHAR*"}

# (cpp function, title, [(type, parameter name)], menu group)
# Action titles use %0, %1... for parameters. IDs are list positions: only ever append.
ACTIONS = [
 ("SetAmbient", "Set ambient light to %0", [(F, "Level (0 = pitch black, 1 = fully lit)")], "Ambient"),
 ("CreateLight", "Create or update light %0 at (%1, %2), radius %3, brightness %4, on layer %5",
  [(I, "Light ID (-1 = assign automatically)"), (F, "X (frame pixels)"), (F, "Y (frame pixels)"),
   (F, "Radius (pixels)"), (F, "Brightness (0-1)"), (I, "Collision layer (1-based Fusion layer)")], "Lights"),
 ("DeleteLight", "Delete light %0", [(I, "Light ID")], "Lights"),
 ("DeleteAllLights", "Delete all lights", [], "Lights"),
 ("SetLightPosition", "Move light %0 to (%1, %2)", [(I, "Light ID"), (F, "X"), (F, "Y")], "Lights"),
 ("SetLightRadius", "Set light %0 radius to %1", [(I, "Light ID"), (F, "Radius (pixels)")], "Lights"),
 ("SetLightBrightness", "Set light %0 brightness to %1", [(I, "Light ID"), (F, "Brightness (0-1)")], "Lights"),
 ("SetLightFalloff", "Set light %0 falloff exponent to %1 (1 = linear, 2 = soft)", [(I, "Light ID"), (F, "Exponent")], "Lights"),
 ("SetLightEnabled", "Set light %0 on/off: %1 (1 = on, 0 = off)", [(I, "Light ID"), (I, "On (1) / Off (0)")], "Lights"),
 ("AttachLightToObject", "Attach light %0 to object %1 (offset %2, %3; delete with object: %4)",
  [(I, "Light ID"), (I, "Object (use Fixed(\"object\"))"), (F, "Offset X"), (F, "Offset Y"), (I, "Delete light when object is destroyed (1/0)")], "Objects"),
 ("DetachLight", "Detach light %0 from its object", [(I, "Light ID")], "Objects"),
 ("CreateLightOnObject", "Create light %0 on object %1 (radius %2, brightness %3, layer %4, offset %5, %6)",
  [(I, "Light ID (-1 = assign automatically)"), (I, "Object (use Fixed(\"object\"))"), (F, "Radius (pixels)"),
   (F, "Brightness (0-1)"), (I, "Collision layer (1-based Fusion layer)"), (F, "Offset X"), (F, "Offset Y")], "Objects"),
 ("AddObstacleRect", "Add obstacle on layer %0 at (%1, %2) size %3 x %4",
  [(I, "Layer"), (I, "X"), (I, "Y"), (I, "Width"), (I, "Height")], "Shadows"),
 ("ClearObstacles", "Clear all obstacles on layer %0", [(I, "Layer")], "Shadows"),
 ("AddShadowCasterObject", "Make object %0 cast shadows on layer %1",
  [(I, "Object (use Fixed(\"object\"))"), (I, "Layer")], "Shadows"),
 ("RemoveShadowCasterObject", "Stop object %0 casting shadows", [(I, "Object (use Fixed(\"object\"))")], "Shadows"),
 ("SetQuality", "Set lightmap pixel size to %0 x %0 screen pixels (1 = sharpest, 4 = fast)", [(I, "Pixel size (1-16)")], "Quality"),
 ("SetRenderOptions", "Debug: set shade levels to %0, invert blend: %1",
  [(I, "Shade levels (2-64)"), (I, "Invert blend coefficient (1/0)")], "Quality"),
]

CONDITIONS = [
 # (cpp function, title, params, triggered, group)
 ("OnLightCreated", "On light created", [], True, "Lights"),
 ("LightExists", "Light %0 exists", [(I, "Light ID")], False, "Lights"),
]

EXPRESSIONS = [
 # (cpp function, title (ends with "("), returns, params, group)
 ("LightCount", "LightCount(", I, [], "Lights"),
 ("LastLightID", "LastLightID(", I, [], "Lights"),
 ("LightX", "LightX(", F, [(I, "Light ID")], "Lights"),
 ("LightY", "LightY(", F, [(I, "Light ID")], "Lights"),
 ("LightRadius", "LightRadius(", F, [(I, "Light ID")], "Lights"),
 ("LightBrightness", "LightBrightness(", F, [(I, "Light ID")], "Lights"),
 ("Ambient", "Ambient(", F, [], "Ambient"),
 ("DebugInfo", "DebugInfo$(", T, [], "Debug"),
 ("LastError", "LastError$(", T, [], "Debug"),
]

PROPERTIES = [
 # (kind, title, info, default, items)
 ("checkbox", "Backdrop obstacles", "Backdrop objects with an obstacle type cast shadows (reserved; imported in a later version)", True, None),
 ("combo", "Quality", "Lightmap resolution: lower is faster, higher has sharper shadows",
  1, ["Full (1:1)", "Half", "Quarter", "Eighth"]),
]
# Quality default index 1 = Half. The "Full" setting avoids stretching, useful when testing.


def menu(entries, group_of):
    """Nested menu in DarkEdif format: ["Group", [id, "Name"], ...] per group, keeping first-seen group order."""
    groups, order = {}, []
    for i, e in enumerate(entries):
        g = group_of(e)
        if g not in groups:
            groups[g] = []; order.append(g)
        groups[g].append([i, e[1] if not e[1].endswith("(") else e[1]])
    return [[g] + groups[g] for g in order]


def clone_params(proto_params, params):
    """Build a parameter list shaped like the template's (list-of-lists or list-of-dicts)."""
    sample = proto_params[0] if proto_params else None
    out = []
    for t, n in params:
        if isinstance(sample, dict):
            keys = {k.lower(): k for k in sample}
            d = copy.deepcopy(sample)
            d[keys.get("type", "Type")] = t
            d[keys.get("name", keys.get("title", "Name"))] = n
            out.append(d)
        else:
            out.append([t, n])
    return out


def proto(lst):
    return copy.deepcopy(lst[0]) if lst else {}


def build_json(tmpl):
    j = copy.deepcopy(tmpl)
    about = j.setdefault("About", {})
    for k, v in (("Name", NAME), ("Identifier", IDENT), ("Author", AUTHOR), ("Comment", COMMENT)):
        about[k] = v
    for k in list(j.keys()):                      # some template versions keep these at top level
        if k.lower() in ("identifier", "name") and k not in ("About",):
            j[k] = IDENT if k.lower() == "identifier" else NAME

    pa, pc, pe = proto(tmpl.get("Actions", [])), proto(tmpl.get("Conditions", [])), proto(tmpl.get("Expressions", []))
    acts, conds, exprs = [], [], []
    for fn, title, params, _g in ACTIONS:
        a = copy.deepcopy(pa); a["Title"] = title
        a["Parameters"] = clone_params(pa.get("Parameters", []), params); acts.append(a)
    for fn, title, params, trig, _g in CONDITIONS:
        c = copy.deepcopy(pc); c["Title"] = title; c["Triggered"] = trig
        c["Parameters"] = clone_params(pc.get("Parameters", []), params); conds.append(c)
    for fn, title, ret, params, _g in EXPRESSIONS:
        e = copy.deepcopy(pe); e["Title"] = title; e["Returns"] = ret
        e["Parameters"] = clone_params(pe.get("Parameters", []), params); exprs.append(e)
    j["Actions"], j["Conditions"], j["Expressions"] = acts, conds, exprs
    j["ActionMenu"] = menu(ACTIONS, lambda e: e[3])
    j["ConditionMenu"] = menu(CONDITIONS, lambda e: e[4])
    j["ExpressionMenu"] = menu(EXPRESSIONS, lambda e: e[4])

    # Properties: clone the template's own entries per type so unknown required fields are inherited.
    tprops = tmpl.get("Properties", [])
    def find(word):
        for p in tprops:
            if isinstance(p, dict) and word in str(p.get("Type", "")).lower():
                return copy.deepcopy(p)
        return None
    props = []
    for kind, title, info, default, items in PROPERTIES:
        p = find("check" if kind == "checkbox" else "combo")
        if p is None:
            print("WARNING: template has no %s property to copy; using best-guess schema" % kind, file=sys.stderr)
            p = {"Type": "Checkbox" if kind == "checkbox" else "Combo Box"}
        p["Title"] = title; p["Info"] = info; p["DefaultState"] = default
        if items is not None:
            p["Items"] = items
        props.append(p)
    j["Properties"] = props
    return j


def decl(ret, fn, params):
    args = ", ".join("%s p%d" % (CTYPE[t], i) for i, (t, _n) in enumerate(params))
    return "%s %s(%s);" % (ret, fn, args)


def members_inc():
    L = ["// Generated by gen_ext_json.py. Included inside `class Extension`. Do not edit."]
    L += ["std::unique_ptr<LumenState> lx;",
          "void LumenInit(bool backdropObstacles, const std::wstring& quality);",
          "REFLAG LumenHandle();", "REFLAG LumenDisplay();", ""]
    L += ["// Actions"] + [decl("void", a[0], a[2]) for a in ACTIONS]
    L += ["// Conditions"] + [decl("bool", c[0], c[2]) for c in CONDITIONS]
    L += ["// Expressions"] + [decl({I: "int", F: "float", T: "const TCHAR*"}[e[2]], e[0], e[3]) for e in EXPRESSIONS]
    return "\n".join(L) + "\n"


def ctor_inc():
    L = ["// Generated by gen_ext_json.py. Included inside the Extension constructor. Do not edit."]
    L += ["LinkAction(%d, %s);" % (i, a[0]) for i, a in enumerate(ACTIONS)]
    L += ["LinkCondition(%d, %s);" % (i, c[0]) for i, c in enumerate(CONDITIONS)]
    L += ["LinkExpression(%d, %s);" % (i, e[0]) for i, e in enumerate(EXPRESSIONS)]
    L += ["LumenInit(edPtr->Props.IsPropChecked(std::string_view(\"Backdrop obstacles\")),",
          "           std::wstring(edPtr->Props.GetPropertyStr(std::string_view(\"Quality\"))));"]
    return "\n".join(L) + "\n"


def docs_md():
    def sig(title, params):
        t = title
        for i, (_t, n) in enumerate(params):
            t = t.replace("%%%d" % i, "<%s>" % n.split(" (")[0])
        return t
    out = ["# Lumen API reference", "", "Generated from `extension/gen_ext_json.py`. IDs are list positions and never change once released.", ""]
    out += ["## Actions", ""]
    for g in dict.fromkeys(a[3] for a in ACTIONS):
        out.append("### " + g)
        for i, a in enumerate(ACTIONS):
            if a[3] == g:
                out.append("- **%s**" % sig(a[1], a[2]))
                for _t, n in a[2]:
                    out.append("  - %s: %s" % (_t, n))
        out.append("")
    out += ["## Conditions", ""]
    for c in CONDITIONS:
        out.append("- **%s**%s" % (sig(c[1], c[2]), " (trigger)" if c[3] else ""))
    out += ["", "## Expressions", ""]
    for e in EXPRESSIONS:
        ps = ", ".join(n.split(" (")[0] for _t, n in e[3])
        out.append("- `%s%s)` returns %s" % (e[1], ps, e[2]))
    out += ["", "## Editor properties", ""]
    for _k, title, info, default, items in PROPERTIES:
        out.append("- **%s**: %s (default: %s)" % (title, info, items[default] if items else default))
    return "\n".join(out) + "\n"


FAKE_TEMPLATE = {
    "About": {"Name": "DarkEdif Template", "Identifier": "DARK"},
    "Actions": [{"Title": "x %0", "Parameters": [["Integer", "p"]]}],
    "Conditions": [{"Title": "c", "Triggered": False, "Parameters": []}],
    "Expressions": [{"Title": "e(", "Returns": "Integer", "Parameters": []}],
    "Properties": [{"Type": "Checkbox", "Title": "t", "DefaultState": False},
                   {"Type": "Combo Box", "Title": "c", "Items": ["a"], "DefaultState": 0}],
}


def selfcheck(root):
    errs = []
    for kind, lst in (("action", ACTIONS), ("condition", CONDITIONS), ("expression", EXPRESSIONS)):
        names = [e[0] for e in lst]
        if len(set(names)) != len(names):
            errs.append("duplicate %s function names" % kind)
    for i, a in enumerate(ACTIONS):                       # titles reference only existing parameters
        used = {int(x) for x in re.findall(r"%(\d+)", a[1])}
        if not used <= set(range(len(a[2]))):
            errs.append("action %d title references a missing parameter" % i)
    for i, c in enumerate(CONDITIONS):
        used = {int(x) for x in re.findall(r"%(\d+)", c[1])}
        if not used <= set(range(len(c[2]))):
            errs.append("condition %d title references a missing parameter" % i)
    for i, e in enumerate(EXPRESSIONS):
        if not e[1].endswith("("):
            errs.append("expression %s title must end with '('" % e[0])
        if e[2] == T and not e[1].endswith("$("):
            errs.append("text expression %s should end with '$('" % e[0])
    j = build_json(FAKE_TEMPLATE)
    for key, lst in (("ActionMenu", ACTIONS), ("ConditionMenu", CONDITIONS), ("ExpressionMenu", EXPRESSIONS)):
        ids = [item[0] for grp in j[key] for item in grp[1:]]
        if sorted(ids) != list(range(len(lst))):
            errs.append("%s must list every ID exactly once" % key)
    # C++ definitions must match the spec's signatures
    cpp = open(os.path.join(root, "extension", "LumenExt.cpp"), encoding="utf-8").read()
    defs = {}
    for m in re.finditer(r"^[\w\s\*:]+?\bExtension::(\w+)\s*\(([^)]*)\)\s*\{", cpp, re.M):
        args = [a.strip() for a in m.group(2).split(",") if a.strip() and a.strip() != "void"]
        defs[m.group(1)] = [re.sub(r"\s*\bp?\w+$", "", a).replace(" ", "") if a.count(" ") else a for a in args]
    norm = {I: "int", F: "float", T: "constTCHAR*"}
    for lst in (ACTIONS, CONDITIONS, EXPRESSIONS):
        for e in lst:
            fn = e[0]
            params = e[2] if lst is not EXPRESSIONS else e[3]
            if fn not in defs:
                errs.append("no definition of Extension::%s in LumenExt.cpp" % fn); continue
            want = [norm[t] for t, _n in params]
            got = [g.replace("constTCHAR*", "constTCHAR*") for g in defs[fn]]
            if len(got) != len(want) or any(g != w for g, w in zip(got, want)):
                errs.append("signature mismatch for %s: spec %s, cpp %s" % (fn, want, got))
    if errs:
        print("SELFCHECK FAILED"); [print(" -", e) for e in errs]; return 1
    print("selfcheck ok: %d actions, %d conditions, %d expressions" % (len(ACTIONS), len(CONDITIONS), len(EXPRESSIONS)))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--template"); ap.add_argument("--out"); ap.add_argument("--docs")
    ap.add_argument("--selfcheck", action="store_true")
    a = ap.parse_args()
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if a.selfcheck:
        rc = selfcheck(root)
        if a.docs:
            open(a.docs, "w", encoding="utf-8").write(docs_md())
        sys.exit(rc)
    if not (a.template and a.out):
        ap.error("--template and --out are required")
    with open(a.template, encoding="utf-8-sig") as f:
        tmpl = json.load(f)
    print("Template property types:", [p.get("Type") for p in tmpl.get("Properties", []) if isinstance(p, dict)])
    print("Template About keys:", list(tmpl.get("About", {}).keys()))
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "DarkExt.json"), "w", encoding="utf-8") as f:
        json.dump(build_json(tmpl), f, indent="\t")
    open(os.path.join(a.out, "LumenMembers.inc"), "w", encoding="utf-8").write(members_inc())
    open(os.path.join(a.out, "LumenCtor.inc"), "w", encoding="utf-8").write(ctor_inc())
    if a.docs:
        open(a.docs, "w", encoding="utf-8").write(docs_md())
    print("generated DarkExt.json and .inc files in", a.out)


if __name__ == "__main__":
    main()
