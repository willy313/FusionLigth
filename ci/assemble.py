#!/usr/bin/env python3
"""Assemble the Lumen DarkEdif project from the SDK's template.

  assemble.py --sdk <MMF2Exts checkout> --repo <this repo> --out <artifact dir> [--object-access]

What it does (every step prints what it changed; anything it cannot find is a hard error with context):
  1. copies  DarkEdif/DarkEdif Template  ->  DarkEdif/Lumen  and renames every 'DarkEdif Template'
  2. snapshots the untouched template into <out>/template-reference (uploaded as a CI artifact, so a
     failing build can be diagnosed against the exact template version)
  3. overlays core/lumen.* and extension/Lumen* and generates DarkExt.json + the .inc files
  4. patches Extension.hpp / Extension.cpp / Edittime.cpp / FusionSDKConfig.ini / the vcxproj / the vcxitems
"""
import argparse, os, re, shutil, subprocess, sys

TEXT_EXT = {".vcxproj", ".vcxitems", ".filters", ".props", ".targets", ".json", ".ini", ".cpp", ".hpp",
            ".h", ".c", ".txt", ".sln", ".xml", ".rc", ".def", ".md", ".user", ".bat", ".ps1", ".py"}
OEFLAGS = "OEFLAGS::MANUAL_DISPLAY_EXT | OEFLAGS::SPRITES | OEFLAGS::BACK_SAVE | OEFLAGS::NEVER_KILL | OEFLAGS::NEVER_SLEEP"
OEPREFS = "OEPREFS::BACK_SAVE"
NAME = "Lumen"


def die(msg):
    print("ASSEMBLE ERROR: " + msg, file=sys.stderr)
    sys.exit(1)


def read(p):
    with open(p, encoding="utf-8-sig", errors="replace", newline="") as f:
        return f.read()


def write(p, s):
    with open(p, "w", encoding="utf-8-sig", newline="") as f:   # BOM: MSVC reads it as UTF-8
        f.write(s)


def find_one(root, name):
    hits = [os.path.join(d, name) for d, _s, fs in os.walk(root) if name in fs]
    hits.sort(key=lambda p: p.count(os.sep))
    return hits[0] if hits else None


def replace_body(text, sig_regex, body):
    """Replace a function body, matching braces from the first '{' after the signature."""
    m = re.search(sig_regex, text)
    if not m:
        return None
    open_i = text.find("{", m.end())
    if open_i < 0:
        return None
    depth, i = 0, open_i
    while i < len(text):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    if depth != 0:
        return None
    return text[:open_i + 1] + "\n\t" + body + "\n" + text[i:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--repo", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--object-access", action="store_true")
    a = ap.parse_args()

    de = os.path.join(a.sdk, "DarkEdif")
    tmpl = os.path.join(de, "DarkEdif Template")
    if not os.path.isdir(tmpl):
        die("template not found at %s. DarkEdif folder contains: %s" % (tmpl, sorted(os.listdir(de)) if os.path.isdir(de) else "(missing)"))
    proj = os.path.join(de, NAME)
    if os.path.exists(proj):
        shutil.rmtree(proj)
    os.makedirs(a.out, exist_ok=True)

    # 2. snapshot of the untouched template
    ref = os.path.join(a.out, "template-reference")
    os.makedirs(ref, exist_ok=True)
    with open(os.path.join(ref, "tree.txt"), "w", encoding="utf-8") as f:
        for d, _s, fs in os.walk(tmpl):
            for n in sorted(fs):
                f.write(os.path.relpath(os.path.join(d, n), tmpl) + "\n")
    for n in os.listdir(tmpl):
        p = os.path.join(tmpl, n)
        if os.path.isfile(p) and os.path.splitext(n)[1].lower() in {".cpp", ".hpp", ".h", ".json", ".ini", ".vcxproj", ".vcxitems", ".props", ".filters"}:
            shutil.copy(p, ref)

    # SDK headers for diagnosis: the exact Surface / run-header API this build compiles against
    hdr_out = os.path.join(a.out, "sdk-headers")
    os.makedirs(hdr_out, exist_ok=True)
    for d, _s, fs in os.walk(os.path.join(de, "Inc")):
        for n in fs:
            if n.lower().endswith((".hpp", ".h")):
                dest_dir = os.path.join(hdr_out, os.path.relpath(d, os.path.join(de, "Inc")))
                os.makedirs(dest_dir, exist_ok=True)
                shutil.copy(os.path.join(d, n), dest_dir)
    surf_h = None
    for d, _s, fs in os.walk(hdr_out):
        if "SurfaceMultiPlat.hpp" in fs:
            surf_h = os.path.join(d, "SurfaceMultiPlat.hpp")
    if surf_h and os.path.exists(surf_h):
        print("---- SurfaceMultiPlat.hpp: declarations Lumen uses")
        for i, ln in enumerate(read(surf_h).split("\n"), 1):
            if re.search(r"\b(SetPixel|GetPixel|CopySection|CreateFromMainWindow|FillImageWith|SetTransparencyToColor|enum class Blit\w+|class Surface|struct Rect)\b", ln):
                print("%5d| %s" % (i, ln.strip()[:200]))

    # 1. copy + rename
    shutil.copytree(tmpl, proj)
    for d, _s, fs in os.walk(proj):
        for n in fs:
            if os.path.splitext(n)[1].lower() in TEXT_EXT:
                p = os.path.join(d, n)
                t = read(p)
                if "DarkEdif Template" in t:
                    write(p, t.replace("DarkEdif Template", NAME))
    for d, dirs, fs in os.walk(proj, topdown=False):
        for n in fs + dirs:
            if "DarkEdif Template" in n:
                os.rename(os.path.join(d, n), os.path.join(d, n.replace("DarkEdif Template", NAME)))
    print("copied template to", proj)
    if os.environ.get("GITHUB_ENV"):
        with open(os.environ["GITHUB_ENV"], "a") as f:
            f.write("LUMEN_PROJECT_DIR=%s\n" % proj)

    # 3. overlay + generate
    for src in ("core/lumen.hpp", "core/lumen.cpp", "extension/LumenState.hpp", "extension/LumenExt.cpp"):
        shutil.copy(os.path.join(a.repo, src), proj)
    stale = os.path.join(proj, "DarkExt.PostMinify.json")
    if os.path.exists(stale):
        os.remove(stale)     # regenerated by the pre-build tool from the new DarkExt.json
    # Log what the template really looks like, so a failure can be diagnosed from the log alone.
    def show(path, pats, limit=70):
        print("---- %s: lines matching /%s/" % (os.path.relpath(path, proj), pats))
        n = 0
        for i, ln in enumerate(read(path).split("\n"), 1):
            if re.search(pats, ln):
                print("%4d| %s" % (i, ln.rstrip()))
                n += 1
                if n >= limit:
                    print("     ...")
                    break

    def strip_comments(t):
        return re.sub(r"//[^\n]*|/\*.*?\*/", "", t, flags=re.S)

    print("---- template root files:", sorted(os.listdir(proj)))

    # 4a. Extension.hpp
    hpp = find_one(proj, "Extension.hpp") or die("Extension.hpp not found in the template")
    show(hpp, r"OEFLAGS|OEPREFS|Handle|Display|Runtime|class Extension|GetZone|GetDisplay|GetCollision")
    t = read(hpp)
    if '#include "LumenState.hpp"' not in t:
        if re.search(r"^\s*#pragma once", t, re.M):
            t = re.sub(r"(^\s*#pragma once[^\n]*\n)", lambda m: m.group(1) + '#include "LumenState.hpp"\n', t, count=1, flags=re.M)
        else:
            t = '#include "LumenState.hpp"\n' + t
    m = re.search(r"Edif::Runtime\s+Runtime\s*;[^\n]*\n", t)
    if not m:
        die("anchor 'Edif::Runtime Runtime;' not found in Extension.hpp; see the lines printed above")
    t = t[:m.end()] + '\t#include "LumenMembers.inc"\n' + t[m.end():]
    t, n1 = re.subn(r"(\bOEFLAGS\s*=\s*)[^;]+;", lambda m: m.group(1) + OEFLAGS + ";", t, count=1)
    t, n2 = re.subn(r"(\bOEPREFS\s*=\s*)[^;]+;", lambda m: m.group(1) + OEPREFS + ";", t, count=1)
    if not n1:
        die("could not find the OEFLAGS assignment in Extension.hpp; see the lines printed above")
    if not n2:
        print("warning: no OEPREFS assignment found in Extension.hpp (display flag left as is)")
    write(hpp, t)
    print("patched", hpp)

    # 4b. Extension.cpp: constructor links
    cpp = find_one(proj, "Extension.cpp") or die("Extension.cpp not found in the template")
    show(cpp, r"Link(Action|Condition|Expression)|Extension::|GetZone|GetDisplay|GetCollision")
    lines, out, first = read(cpp).split("\n"), [], False
    for ln in lines:
        if re.match(r"^\s*Link(Action|Condition|Expression)\s*\(.*\)\s*;", ln):
            if not first:
                out.append('\t#include "LumenCtor.inc"')
                first = True
            continue
        out.append(ln)
    if not first:
        die("no LinkAction/LinkCondition/LinkExpression lines in Extension.cpp; see the lines printed above")
    write(cpp, "\n".join(out))

    # 4b2. Handle() and Display(): replace the template's body wherever it is defined; if the template has no
    # definition, add the declaration (unless already declared) and the definition.
    cpps = [os.path.join(proj, n) for n in sorted(os.listdir(proj)) if n.lower().endswith(".cpp")]
    cpps += [os.path.join(d, n) for d, _s, fs in os.walk(proj) for n in fs
             if n.lower().endswith(".cpp") and os.path.join(d, n) not in cpps]

    def patch_function(name, call):
        pat = r"\bExtension::%s\s*\(\s*(?:void)?\s*\)(?=\s*(?:const\s*)?(?:noexcept\s*)?\{)" % name
        for path in cpps:
            t = read(path)
            m = re.search(pat, t)
            if not m:
                continue
            prefix = t[t.rfind("\n", 0, m.start()) + 1:m.start()].split()
            ret = prefix[-1] if prefix else "REFLAG"
            body = call + ";" if ret == "void" else "return static_cast<%s>(%s);" % (ret, call)
            write(path, replace_body(t, pat, body))
            print("replaced Extension::%s() body in %s (return type '%s')" % (name, os.path.relpath(path, proj), ret))
            return
        h = read(hpp)
        decl = re.search(r"([\w:<>&\*]+)\s+%s\s*\(" % name, strip_comments(h))
        ret = decl.group(1) if decl else "REFLAG"
        if not decl:
            h = h.replace('\t#include "LumenMembers.inc"\n', '\t#include "LumenMembers.inc"\n\t%s %s();\n' % (ret, name), 1)
            write(hpp, h)
        t = read(cpp)
        body = call + ";" if ret == "void" else "return static_cast<%s>(%s);" % (ret, call)
        write(cpp, t.rstrip("\n") + "\n\n%s Extension::%s()\n{\n\t%s\n}\n" % (ret, name, body))
        print("template has no Extension::%s() definition; added %s%s definition to %s" % (
            name, "" if decl else "declaration and ", ret, os.path.relpath(cpp, proj)))

    patch_function("Handle", "LumenHandle()")
    patch_function("Display", "LumenDisplay()")
    print("patched", cpp)

    # 4c. Edittime.cpp: remove the deliberate #error
    et = find_one(proj, "Edittime.cpp")
    if et:
        show(et, r"#\s*error|SetEditSize|GetObjectRect|objSize|EditorDisplay|DrawRectangle|DARKEDIF_DISPLAY")
        t = read(et)
        # The template's block is:  #if ... IS_DARKEDIF_TEMPLATE==0 / #error ... / frameSurf.FillImageWith(...) / #endif
        # Removing only the #error would leave a FillImageWith on the editor frame (fills the whole editor / crashes per the
        # field notes), so the whole block goes: the editor shows Fusion's own icon.
        t2, n = re.subn(r"(?ms)^[ \t]*#\s*if[^\n]*IS_DARKEDIF_TEMPLATE\s*==\s*0[^\n]*\n.*?^[ \t]*#\s*endif[^\n]*\n",
                        "\t// Lumen: nothing is drawn in the editor; Fusion shows the extension icon.\n", t, count=1)
        if not n:   # other template versions: at least remove the #error line
            t2, n = re.subn(r"^[ \t]*#\s*error\s+Add your drawing tech here[^\n]*\n?", "", t, flags=re.M)
        if n:
            write(et, t2)
        print("Edittime.cpp: removed %d '#error' line(s)" % n)

    # 4d. FusionSDKConfig.ini lives in the SDK root (not the project folder); same content as a known-good build.
    write(os.path.join(a.sdk, "FusionSDKConfig.ini"),
          "[Global]\nWindowsXPCompatibility = false\nFavorSizeOrSpeed = speed\nMakePDBDuringReleaseBuild = false\n")
    print("wrote", os.path.join(a.sdk, "FusionSDKConfig.ini"))

    # 4e. vcxproj: preprocessor defines
    vcx = [os.path.join(d, n) for d, _s, fs in os.walk(proj) for n in fs if n.lower().endswith(".vcxproj")]
    win = [p for p in vcx if "windows" in os.path.basename(p).lower()] or vcx
    if not win:
        die("no .vcxproj found in the assembled project")
    vproj = sorted(win, key=len)[0]
    defs = "FUSION_INTERNAL_ACCESS;DARKEDIF_LOG_MIN_LEVEL=6;" + ("LUMEN_OBJECT_ACCESS;" if a.object_access else "")
    block = ("\n  <ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>%s%%(PreprocessorDefinitions)"
             "</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup>" % defs)
    t = read(vproj)
    m = re.search(r'<Import\s+Project="[^"]*FusionSDK_AfterMSPropSheets\.props"[^>]*/>', t)
    if m:
        t = t[:m.end()] + block + t[m.end():]
    else:
        print("warning: FusionSDK_AfterMSPropSheets.props import not found; defines added before </Project>")
        t = t.rsplit("</Project>", 1)[0] + block + "\n</Project>\n"
    write(vproj, t)
    print("defines in %s: %s" % (os.path.basename(vproj), defs.rstrip(";")))

    # 4f. add our sources to the vcxitems (or the vcxproj if there is none)
    items = [os.path.join(d, n) for d, _s, fs in os.walk(proj) for n in fs if n.lower().endswith(".vcxitems")]
    target = sorted(items, key=len)[0] if items else vproj
    src = ('\n  <ItemGroup>\n'
           '    <ClCompile Include="$(MSBuildThisFileDirectory)lumen.cpp"><PrecompiledHeader>NotUsing</PrecompiledHeader></ClCompile>\n'
           '    <ClCompile Include="$(MSBuildThisFileDirectory)LumenExt.cpp" />\n'
           '  </ItemGroup>')
    t = read(target)
    if "lumen.cpp" not in t:
        t = t.rsplit("</Project>", 1)[0] + src + "\n</Project>\n"
        write(target, t)
    print("sources added to", os.path.basename(target))

    # 5. DarkExt.json + .inc files (last, so the template log above is printed even if this step fails)
    tj = os.path.join(proj, "DarkExt.json")
    if not os.path.exists(tj):
        die("template has no DarkExt.json in its root")
    subprocess.check_call([sys.executable, os.path.join(a.repo, "extension", "gen_ext_json.py"),
                           "--template", tj, "--out", proj, "--docs", os.path.join(a.out, "API.md")])

    for var, val in (("LUMEN_PROJECT_DIR", proj), ("LUMEN_VCXPROJ", vproj), ("LUMEN_SDK_DARKEDIF", de)):
        print("%s=%s" % (var, val))
        if os.environ.get("GITHUB_ENV"):
            with open(os.environ["GITHUB_ENV"], "a") as f:
                f.write("%s=%s\n" % (var, val))


if __name__ == "__main__":
    main()
