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

I, F, T, O = "Integer", "Float", "Text", "Object"
CTYPE = {I: "int", F: "float", T: "const TCHAR*", O: "RunObject*"}

from collections import namedtuple
# Action:     fn, menu label, event-editor sentence (%0, %1... = parameters), parameters, menu group
# Condition:  fn, menu label, sentence ("%o :" = the object's name), parameters, is trigger, menu group
# Expression: fn, menu label, name typed in expressions (ends with "(" ; "$(" for text), return type, parameters, group
# IDs are list positions: only ever append once released.
Act = namedtuple("Act", "fn label title params group")
Cond = namedtuple("Cond", "fn label title params trigger group")
Exp = namedtuple("Exp", "fn label title ret params group")

ACTIONS = [
 Act("SetAmbient", "Set ambient light level", "Set ambient light level to %0 (0 = pitch black, 1 = fully lit)",
     [(F, "Ambient level (0 to 1)")], "Ambient light"),
 Act("CreateLight", "Create or update light", "Create light %0 at X=%1, Y=%2 (radius %3, brightness %4, shadow layer %5)",
     [(I, "Light ID (-1 = pick one automatically)"), (F, "X position"), (F, "Y position"), (F, "Radius in pixels"),
      (F, "Brightness (0 to 1)"), (I, "Shadow layer (Fusion layer number)")], "Lights"),
 Act("DeleteLight", "Delete light", "Delete light %0", [(I, "Light ID")], "Lights"),
 Act("DeleteAllLights", "Delete all lights", "Delete all lights", [], "Lights"),
 Act("SetLightPosition", "Move light", "Move light %0 to X=%1, Y=%2", [(I, "Light ID"), (F, "X position"), (F, "Y position")], "Lights"),
 Act("SetLightRadius", "Set light radius", "Set radius of light %0 to %1 pixels", [(I, "Light ID"), (F, "Radius in pixels")], "Lights"),
 Act("SetLightBrightness", "Set light brightness", "Set brightness of light %0 to %1", [(I, "Light ID"), (F, "Brightness (0 to 1)")], "Lights"),
 Act("SetLightFalloff", "Set light falloff", "Set falloff of light %0 to %1 (1 = linear, 2 = smooth, higher = tighter)",
     [(I, "Light ID"), (F, "Falloff")], "Lights"),
 Act("SetLightEnabled", "Turn light on or off", "Turn light %0 on or off: %1 (1 = on, 0 = off)", [(I, "Light ID"), (I, "On (1) or off (0)")], "Lights"),
 Act("CreateLightOnObject", "Create light on object",
     "Create light %0 on %1 (radius %2, brightness %3, shadow layer %4, offset X=%5, Y=%6)",
     [(I, "Light ID (-1 = pick one automatically)"), (O, "Object to attach the light to"), (F, "Radius in pixels"),
      (F, "Brightness (0 to 1)"), (I, "Shadow layer (Fusion layer number)"), (F, "Offset X"), (F, "Offset Y")], "Objects"),
 Act("AttachLightToObject", "Attach existing light to object", "Attach light %0 to %1 (offset X=%2, Y=%3)",
     [(I, "Light ID"), (O, "Object to follow"), (F, "Offset X"), (F, "Offset Y")], "Objects"),
 Act("DetachLight", "Detach light from object", "Detach light %0 from its object", [(I, "Light ID")], "Objects"),
 Act("AddObstacleRect", "Add obstacle rectangle", "Add obstacle on shadow layer %0 at X=%1, Y=%2, size %3 x %4",
     [(I, "Shadow layer (Fusion layer number)"), (I, "X position"), (I, "Y position"), (I, "Width"), (I, "Height")], "Shadows"),
 Act("ClearObstacles", "Clear obstacles on a layer", "Clear all obstacle rectangles on shadow layer %0", [(I, "Shadow layer (Fusion layer number)")], "Shadows"),
 Act("AddShadowCasterObject", "Make object cast shadows", "Make %0 cast shadows on shadow layer %1",
     [(O, "Object that casts shadows"), (I, "Shadow layer (Fusion layer number)")], "Shadows"),
 Act("RemoveShadowCasterObject", "Stop object casting shadows", "Stop %0 casting shadows", [(O, "Object")], "Shadows"),
 Act("SetQuality", "Set lightmap pixel size", "Set lightmap pixel size to %0 (1 = sharpest, 4 = fastest)", [(I, "Pixel size (1 to 16)")], "Settings"),
 Act("SetRenderOptions", "Debug: render options",
     "Debug: smooth edges %0 (1/0), copy-alpha flag %1 (1/0), method %2 (-1 auto, 0, 1, 2), invert blend %3 (1/0), shade levels %4",
     [(I, "Smooth edges (1/0, alpha methods only)"), (I, "Copy-alpha blit flag (1/0, alpha methods only)"),
      (I, "Method (-1 = automatic, 0 = alpha lock, 1 = alpha surface, 2 = rectangles)"),
      (I, "Invert blend direction (1/0, rectangles method)"), (I, "Shade levels (4 to 64; fewer = faster, rectangles method)")], "Settings"),
]

CONDITIONS = [
 Cond("OnLightCreated", "On light created", "%o : On light created", [], True, "Lights"),
 Cond("LightExists", "Light exists", "%o : Light %0 exists", [(I, "Light ID")], False, "Lights"),
]

EXPRESSIONS = [
 Exp("LightCount", "Number of lights", "NumberOfLights(", I, [], "Lights"),
 Exp("LastLightID", "ID of the last created light", "LastCreatedLightID(", I, [], "Lights"),
 Exp("LightX", "Light X position", "LightX(", F, [(I, "Light ID")], "Lights"),
 Exp("LightY", "Light Y position", "LightY(", F, [(I, "Light ID")], "Lights"),
 Exp("LightRadius", "Light radius", "LightRadius(", F, [(I, "Light ID")], "Lights"),
 Exp("LightBrightness", "Light brightness", "LightBrightness(", F, [(I, "Light ID")], "Lights"),
 Exp("Ambient", "Ambient light level", "AmbientLevel(", F, [], "Ambient light"),
 Exp("DebugInfo", "Debug information", "DebugInfo$(", T, [], "Debug"),
 Exp("LastError", "Last error message", "LastError$(", T, [], "Debug"),
]

PROPERTIES = [   # kinds below exist in the template's own example properties ("Checkbox", "Editbox String")
 # (kind, title, info, default, items)
 ("checkbox", "Backdrop obstacles", "Reserved for a later version: backdrops cannot cast shadows yet. Use Add obstacle rectangle or Make object cast shadows", True, None),
 ("string", "Pixel size", "Lightmap pixel size in screen pixels (1-8). 1 is sharpest and slowest, 4 is a good fast setting", "2", None),
]


def load_jsonc(text):
    """Parse JSON that may contain // and /* */ comments and trailing commas (DarkEdif's DarkExt.json does)."""
    text = text.lstrip("\ufeff")
    out, i, n, in_str = [], 0, len(text), False
    while i < n:                                   # pass 1: strip comments, leave strings alone
        c = text[i]
        if in_str:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1]); i += 2; continue
            if c == '"':
                in_str = False
            i += 1; continue
        if c == '"':
            in_str = True; out.append(c); i += 1; continue
        if c == "/" and text[i + 1:i + 2] == "/":
            while i < n and text[i] not in "\r\n":
                i += 1
            continue
        if c == "/" and text[i + 1:i + 2] == "*":
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        out.append(c); i += 1
    s = "".join(out)
    out, i, n, in_str = [], 0, len(s), False
    while i < n:                                   # pass 2: drop trailing commas
        c = s[i]
        if in_str:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(s[i + 1]); i += 2; continue
            if c == '"':
                in_str = False
            i += 1; continue
        if c == '"':
            in_str = True
        elif c == ",":
            j = i + 1
            while j < n and s[j] in " \t\r\n":
                j += 1
            if j < n and s[j] in "}]":
                i += 1; continue
        out.append(c); i += 1
    s = "".join(out)
    try:
        return json.loads(s)
    except json.JSONDecodeError as e:
        lines = s.split("\n")
        ctx = "\n".join(lines[max(0, e.lineno - 3):e.lineno + 2])
        sys.exit("Could not parse the template DarkExt.json (%s). Context after comment removal:\n%s" % (e, ctx))


def menu(entries, group_of, label_of):
    """Menu in DarkEdif format: one ["Group", [id, "Label"], ...] submenu per group, in first-seen order."""
    groups, order = {}, []
    for i, e in enumerate(entries):
        g = group_of(e)
        if g not in groups:
            groups[g] = []; order.append(g)
        groups[g].append([i, label_of(e)])
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


def K(d, name):
    """The key in d that matches `name` ignoring case, else `name` itself."""
    for k in d:
        if isinstance(k, str) and k.lower() == name.lower():
            return k
    return name


def split_langs(tmpl):
    """Language objects of a multi-language DarkExt.json ("UK English": {...}, "French": {...}), by key."""
    return {k: v for k, v in tmpl.items() if isinstance(v, dict) and isinstance(v.get(K(v, "Actions")), list)}


def outline(tmpl):
    """Print the template's structure so a mismatch can be diagnosed from the CI log alone."""
    print("---- template DarkExt.json outline (top-level keys: %s)" % list(tmpl.keys()))
    langs = split_langs(tmpl)
    for k, v in tmpl.items():
        if k not in langs:
            print("  %s: %s" % (k, json.dumps(v)[:200]))
    if langs:
        name = "UK English" if "UK English" in langs else next(iter(langs))
        print("  language objects: %s; detail of '%s':" % (list(langs), name))
        tmpl = langs[name]
    for k, v in tmpl.items():
        if isinstance(v, dict):
            print("    %s: object %s" % (k, json.dumps(v)[:500]))
        elif isinstance(v, list):
            if k.lower().endswith("menu"):
                print("    %s: list(%d) %s" % (k, len(v), json.dumps(v)[:500]))
            else:
                print("    %s: list(%d), first=%s" % (k, len(v), json.dumps(v[0])[:500] if v else None))
        else:
            print("    %s: %s" % (k, json.dumps(v)[:100]))
    print("----")


def proto(lst):
    return copy.deepcopy(lst[0]) if lst else {}


def build_lang(tmpl):
    j = copy.deepcopy(tmpl)
    ak = K(j, "About")
    if not isinstance(j.get(ak), dict):
        j[ak] = {}
    about = j[ak]
    about[K(about, "Name")] = NAME
    for k, v in (("Author", AUTHOR), ("Comment", COMMENT)):
        if K(about, k) in about:
            about[K(about, k)] = v

    def lst(name):
        v = tmpl.get(K(tmpl, name), [])
        return v if isinstance(v, list) else []
    pa, pc, pe = proto(lst("Actions")), proto(lst("Conditions")), proto(lst("Expressions"))
    acts, conds, exprs = [], [], []
    for a_ in ACTIONS:
        a = copy.deepcopy(pa); a[K(a, "Title")] = a_.title
        a[K(a, "Parameters")] = clone_params(pa.get(K(pa, "Parameters"), []), a_.params); acts.append(a)
    for c_ in CONDITIONS:
        c = copy.deepcopy(pc); c[K(c, "Title")] = c_.title; c[K(c, "Triggered")] = c_.trigger
        c[K(c, "Parameters")] = clone_params(pc.get(K(pc, "Parameters"), []), c_.params); conds.append(c)
    for e_ in EXPRESSIONS:
        e = copy.deepcopy(pe); e[K(e, "Title")] = e_.title; e[K(e, "Returns")] = e_.ret
        e[K(e, "Parameters")] = clone_params(pe.get(K(pe, "Parameters"), []), e_.params); exprs.append(e)
    j[K(j, "Actions")], j[K(j, "Conditions")], j[K(j, "Expressions")] = acts, conds, exprs
    j[K(j, "ActionMenu")] = menu(ACTIONS, lambda e: e.group, lambda e: e.label)
    j[K(j, "ConditionMenu")] = menu(CONDITIONS, lambda e: e.group, lambda e: e.label)
    j[K(j, "ExpressionMenu")] = menu(EXPRESSIONS, lambda e: e.group, lambda e: e.label)
    tm = tmpl.get(K(tmpl, "ActionMenu"))
    # the template's menus hold [id, "Name"] items, "---" separators and ["Submenu", [id, "Name"], ...] groups
    if tm and not any(isinstance(e, list) and e and isinstance(e[0], (int, str)) for e in tm):
        print("WARNING: template menu entries look different from what this script writes: %s" % json.dumps(tm[0])[:300], file=sys.stderr)

    # Properties: clone the template's own entries per type so unknown required fields are inherited.
    tprops = lst("Properties")
    def find(kind):
        want = {"checkbox": "checkbox", "string": "editbox string"}[kind]
        for p in tprops:
            if isinstance(p, dict) and str(p.get(K(p, "Type"), "")).lower() == want:
                return copy.deepcopy(p)
        return None
    props = []
    for kind, title, info, default, items in PROPERTIES:
        p = find(kind)
        if p is None:
            print("WARNING: template has no %s property to copy; using best-guess schema" % kind, file=sys.stderr)
            p = {"Type": "Checkbox" if kind == "checkbox" else "Editbox String"}
        for extra in ("Bold", "Case", "MaxLength", "Minimum", "Maximum"):   # example-only options of the template's sample entries
            p.pop(K(p, extra), None)
        p[K(p, "Title")] = title; p[K(p, "Info")] = info; p[K(p, "DefaultState")] = default
        if kind == "string":
            p["MaxLength"] = 2
        props.append(p)
    j[K(j, "Properties")] = props
    return j


def build_json(tmpl):
    langs = split_langs(tmpl)
    if not langs:                                   # flat, single-language schema
        out = build_lang(tmpl)
        out[K(out, "Identifier")] = IDENT
        return out
    out = {k: copy.deepcopy(v) for k, v in tmpl.items() if k not in langs}
    out[K(out, "Identifier")] = IDENT
    base = "UK English" if "UK English" in langs else next(iter(langs))
    out[base] = build_lang(langs[base])             # other languages are dropped: their text would not match
    return out


def lang_of(root):
    langs = split_langs(root)
    return root[("UK English" if "UK English" in langs else next(iter(langs)))] if langs else root


def decl(ret, fn, params):
    args = ", ".join("%s p%d" % (CTYPE[t], i) for i, (t, _n) in enumerate(params))
    return "%s %s(%s);" % (ret, fn, args)


def members_inc():
    L = ["// Generated by gen_ext_json.py. Included inside `class Extension`. Do not edit."]
    L += ["std::unique_ptr<LumenState> lx;",
          "void LumenInit(bool backdropObstacles, const std::wstring& pixelSize, int objW, int objH);",
          "REFLAG LumenHandle();", "REFLAG LumenDisplay();", ""]
    L += ["// Actions"] + [decl("void", a.fn, a.params) for a in ACTIONS]
    L += ["// Conditions"] + [decl("bool", c.fn, c.params) for c in CONDITIONS]
    L += ["// Expressions"] + [decl({I: "int", F: "float", T: "const TCHAR*"}[e.ret], e.fn, e.params) for e in EXPRESSIONS]
    return "\n".join(L) + "\n"


def ctor_inc():
    L = ["// Generated by gen_ext_json.py. Included inside the Extension constructor. Do not edit."]
    L += ["LinkAction(%d, %s);" % (i, a.fn) for i, a in enumerate(ACTIONS)]
    L += ["LinkCondition(%d, %s);" % (i, c.fn) for i, c in enumerate(CONDITIONS)]
    L += ["LinkExpression(%d, %s);" % (i, e.fn) for i, e in enumerate(EXPRESSIONS)]
    L += ["LumenInit(edPtr->Props.IsPropChecked(std::string_view(\"Backdrop obstacles\")),",
          "           std::wstring(edPtr->Props.GetPropertyStr(std::string_view(\"Pixel size\"))),",
          "           (int)edPtr->objSize.width, (int)edPtr->objSize.height);"]
    return "\n".join(L) + "\n"


def docs_md():
    def sig(title, params):
        t = title
        for i, (_t, n) in enumerate(params):
            t = t.replace("%%%d" % i, "<%s>" % n.split(" (")[0])
        return t
    out = ["# Lumen API reference", "",
           "Generated from `extension/gen_ext_json.py`. IDs are list positions and never change once released.",
           "Object parameters make Fusion run the action once for every selected instance of the chosen object.", ""]
    out += ["## Actions", ""]
    for g in dict.fromkeys(a.group for a in ACTIONS):
        out.append("### " + g)
        for a in ACTIONS:
            if a.group == g:
                out.append("- **%s**: %s" % (a.label, sig(a.title, a.params)))
                for _t, n in a.params:
                    out.append("  - %s: %s" % (_t, n))
        out.append("")
    out += ["## Conditions", ""]
    for c in CONDITIONS:
        out.append("- **%s**%s: %s" % (c.label, " (trigger)" if c.trigger else "", sig(c.title, c.params)))
    out += ["", "## Expressions", ""]
    for e in EXPRESSIONS:
        ps = ", ".join(n.split(" (")[0] for _t, n in e.params)
        out.append("- **%s**: `%s%s)` returns %s" % (e.label, e.title, ps, e.ret))
    out += ["", "## Editor properties", ""]
    for _k, title, info, default, items in PROPERTIES:
        out.append("- **%s**: %s (default: %s)" % (title, info, items[default] if items else default))
    return "\n".join(out) + "\n"


FAKE_LANG = {
    "About": {"Name": "DarkEdif Template", "Author": "x", "Comment": "x"},
    "ActionMenu": [["Sub", [0, "Example"]]], "ConditionMenu": [], "ExpressionMenu": [],
    "Actions": [{"Title": "x %0", "Parameters": [["Integer", "p"]]}],
    "Conditions": [{"Title": "c", "Triggered": False, "Parameters": []}],
    "Expressions": [{"Title": "e(", "Returns": "Integer", "Parameters": []}],
    "Properties": [{"Type": "Checkbox", "Title": "t", "DefaultState": False},
                   {"Type": "Editbox String", "Title": "s", "DefaultState": "x"}],
}
FAKE_TEMPLATE = {"Identifier": "EXMP", "Dependencies": [""], "UK English": FAKE_LANG, "French": FAKE_LANG}


def selfcheck(root):
    errs = []
    for kind, lst in (("action", ACTIONS), ("condition", CONDITIONS), ("expression", EXPRESSIONS)):
        names = [e.fn for e in lst]
        if len(set(names)) != len(names):
            errs.append("duplicate %s function names" % kind)
        labels = [e.label for e in lst]
        if len(set(labels)) != len(labels):
            errs.append("duplicate %s menu labels" % kind)
    for kind, lst in (("action", ACTIONS), ("condition", CONDITIONS)):
        for i, e in enumerate(lst):                       # titles reference exactly the existing parameters
            used = {int(x) for x in re.findall(r"%(\d+)", e.title)}
            if used != set(range(len(e.params))):
                errs.append("%s %d (%s): title uses parameters %s but there are %d" % (kind, i, e.fn, sorted(used), len(e.params)))
    for c in CONDITIONS:
        if not c.title.startswith("%o"):
            errs.append("condition %s title should start with '%%o : '" % c.fn)
    for e in EXPRESSIONS:
        if not e.title.endswith("("):
            errs.append("expression %s title must end with '('" % e.fn)
        if e.ret == T and not e.title.endswith("$("):
            errs.append("text expression %s should end with '$('" % e.fn)
    sample = '{ // c\n\t"a": "x // not a comment, ]",\n\t/* block */ "b": [1, 2,],\n}\n'
    if load_jsonc(sample) != {"a": "x // not a comment, ]", "b": [1, 2]}:
        errs.append("load_jsonc does not handle comments / trailing commas correctly")
    built = build_json(FAKE_TEMPLATE)
    if built.get("Identifier") != IDENT or "French" in built or "Dependencies" not in built:
        errs.append("multi-language template not handled")
    if build_json(FAKE_LANG).get("Identifier") != IDENT:
        errs.append("flat template not handled")
    j = lang_of(built)
    for key, lst in (("ActionMenu", ACTIONS), ("ConditionMenu", CONDITIONS), ("ExpressionMenu", EXPRESSIONS)):
        ids = [item[0] for grp in j[key] for item in grp[1:]]
        if sorted(ids) != list(range(len(lst))):
            errs.append("%s must list every ID exactly once" % key)
        for grp in j[key]:                                # menu labels are plain words, never %0 placeholders
            for item in grp[1:]:
                if "%" in item[1]:
                    errs.append("%s label '%s' contains a placeholder" % (key, item[1]))
    # C++ definitions must match the spec's signatures
    cpp = open(os.path.join(root, "extension", "LumenExt.cpp"), encoding="utf-8").read()
    defs = {}
    for m in re.finditer(r"^[\w\s\*:]+?\bExtension::(\w+)\s*\(([^)]*)\)\s*\{", cpp, re.M):
        args = [a.strip() for a in m.group(2).split(",") if a.strip() and a.strip() != "void"]
        defs[m.group(1)] = [re.sub(r"\s*\b\w+$", "", a).replace(" ", "") if a.count(" ") else a for a in args]
    norm = {I: "int", F: "float", T: "constTCHAR*", O: "RunObject*"}
    for lst in (ACTIONS, CONDITIONS, EXPRESSIONS):
        for e in lst:
            if e.fn not in defs:
                errs.append("no definition of Extension::%s in LumenExt.cpp" % e.fn); continue
            want = [norm[t] for t, _n in e.params]
            got = defs[e.fn]
            if len(got) != len(want) or any(g != w for g, w in zip(got, want)):
                errs.append("signature mismatch for %s: spec %s, cpp %s" % (e.fn, want, got))
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
        tmpl = load_jsonc(f.read())
    outline(tmpl)
    base = lang_of(tmpl) if split_langs(tmpl) else tmpl
    missing = [n for n in ("Actions", "Conditions", "Expressions") if not isinstance(base.get(K(base, n)), list)]
    if missing:
        sys.exit("ERROR: the template DarkExt.json has no %s list(s), so its schema is not what this script expects. "
                 "Refusing to generate a guessed schema. See the outline printed above (and "
                 "template-reference/DarkExt.json in the diagnostics artifact)." % ", ".join(missing))
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
