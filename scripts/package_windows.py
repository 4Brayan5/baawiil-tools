"""Prepare a Windows development bundle from a successful cross build.

Only explicitly resolved Qt/compiler DLLs are bundled; Windows system DLLs stay on
the target OS. No application or dependency signature checks are bypassed here.
"""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import re
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--qt", type=Path, required=True)
    parser.add_argument("--qt-source", type=Path, required=True)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    # Never clear an existing directory, even a prior package. Require a new one.
    output.mkdir(parents=True, exist_ok=False)
    tools = args.toolchain.resolve()
    objdump = tools / "usr/bin/x86_64-w64-mingw32-objdump"
    candidates = {}
    for folder in (args.qt / "bin", tools / "usr/lib/gcc/x86_64-w64-mingw32/14-posix", tools / "usr/x86_64-w64-mingw32/lib"):
        for path in folder.glob("*.dll"):
            candidates[path.name.lower()] = path
    queue = []
    for name in ("baawiil.exe", "baawiil-admin.exe", "baawiil-worker.exe"):
        source = args.build / name
        if not source.is_file():
            raise RuntimeError(f"Missing build output: {source}")
        target = output / name
        shutil.copy2(source, target)
        queue.append(target)
    plugins = ["platforms/qwindows.dll", "sqldrivers/qsqlite.dll", "tls/qschannelbackend.dll"]
    plugins += [str(p.relative_to(args.qt / "plugins")) for category in ("styles", "imageformats") for p in (args.qt / "plugins" / category).glob("*.dll")]
    for relative in plugins:
        source = args.qt / "plugins" / relative
        if not source.is_file():
            raise RuntimeError(f"Required plugin unavailable: {source}")
        target = output / "plugins" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        queue.append(target)
    system = set()
    visited = set()
    while queue:
        binary = queue.pop()
        if binary.name.lower() in visited:
            continue
        visited.add(binary.name.lower())
        listing = subprocess.run([str(objdump), "-p", str(binary)], check=True, capture_output=True, text=True).stdout
        for name in re.findall(r"DLL Name:\s*(\S+)", listing):
            source = candidates.get(name.lower())
            if source:
                target = output / source.name
                if not target.exists():
                    shutil.copy2(source, target)
                    queue.append(target)
            else:
                if name.lower().startswith(("qt", "libgcc", "libstdc++", "libwinpthread")):
                    raise RuntimeError(f"Unresolved runtime dependency: {name}")
                system.add(name)
    (output / "qt.conf").write_text("[Paths]\nPlugins=plugins\n", encoding="utf-8")
    shutil.copy2(root / "docs/etat-implementation.md", output / "DEVELOPMENT-STATUS.md")
    shutil.copytree(root / "docs", output / "docs")
    (output / "DEVELOPMENT-STATUS.md").write_text((output / "DEVELOPMENT-STATUS.md").read_text(encoding="utf-8").replace("(recette-windows.md)", "(docs/recette-windows.md)"), encoding="utf-8")
    (output / "LISEZ-MOI.txt").write_text("Baawiil Monitor — développement Windows 11 x64\n\nOuvrir baawiil.exe. Garder les trois exécutables, DLL et plugins ensemble.\nGuide utilisateur : docs/utilisation.md\nCouverture réelle et limites : docs/etat-implementation.md\nRecette Windows restant à exécuter : docs/recette-windows.md\n\nCe paquet n'est pas signé et ne constitue pas une version 1.0 validée.\nLes notices de dépendances et les sources Qt sont dans licenses/.\nAucune publicité, aucun compte, aucune activation en ligne.\n", encoding="utf-8")
    legal = output / "licenses"
    legal.mkdir()
    shutil.copytree(args.qt_source / "LICENSES", legal / "Qt-LICENSES")
    attribution = []
    for path in (args.qt_source / "src/3rdparty").rglob("qt_attribution.json"):
        # Some upstream attribution strings contain literal newlines. Preserve their
        # text while serializing the combined notices as strict JSON below.
        data = json.loads(path.read_text(encoding="utf-8"), strict=False)
        attribution.extend(data if isinstance(data, list) else [data])
    (legal / "qt-third-party-attributions.json").write_text(json.dumps(attribution, ensure_ascii=False, indent=2), encoding="utf-8")
    commit = subprocess.run(["git", "-C", str(args.qt_source), "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    source_archive = legal / "qtbase-6.8.2-source.tar.gz"
    process = subprocess.Popen(["git", "-C", str(args.qt_source), "archive", "--format=tar", "HEAD"], stdout=subprocess.PIPE)
    with source_archive.open("wb") as target, gzip.GzipFile(fileobj=target, mode="wb", mtime=0) as archive:
        shutil.copyfileobj(process.stdout, archive)
    if process.wait() != 0:
        raise RuntimeError("Qt source archive failed")
    compiler_docs = legal / "compiler"
    compiler_docs.mkdir()
    for name in ("gcc-mingw-w64-base", "gcc-mingw-w64-x86-64-posix", "gcc-mingw-w64-x86-64-posix-runtime", "mingw-w64-common", "mingw-w64-x86-64-dev", "nsis", "nsis-common"):
        source = tools / "usr/share/doc" / name / "copyright"
        if source.is_file():
            shutil.copy2(source, compiler_docs / f"{name}-copyright.txt")
    (legal / "README.txt").write_text(
        "Development build; application license decision and release audit remain pending.\n"
        "Do not publish as a stable or signed release. Qt DLLs are dynamically linked and replaceable.\n"
        "Qt base 6.8.2 source is included, with its third-party sources and license texts.\n"
        f"Qt source commit: {commit}\n"
        "Qt build: shared, Release, MinGW x64, OpenGL/dynamicgl/Vulkan/DBus disabled, bundled SQLite.\n"
        "Qt official source and build documentation: https://code.qt.io/qt/qtbase.git/\n"
        "Compiler: Debian MinGW GCC 14.2.0; runtime exception texts accompany the compiler notices.\n"
        "Compiler source packages: https://sources.debian.org/src/gcc-mingw-w64/\n"
        "and https://sources.debian.org/src/mingw-w64/\n"
        "Full dependency/redistribution audit is still required before a public release.\n", encoding="utf-8")
    manifest = {"qtCommit": commit, "architecture": "Windows x64", "systemDependencies": sorted(system), "sha256": {}}
    for path in sorted(output.rglob("*")):
        if path.is_file():
            with path.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            manifest["sha256"][path.relative_to(output).as_posix()] = digest
    (output / "build-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    # NSIS escapes are generated from the exact package content, never wildcards.
    def escape(value):
        return value.replace("$", "$$").replace('"', '$\\"').replace("/", "\\")
    delete = output.parent / (output.name + "-uninstall.nsh")
    lines = [f'Delete "$INSTDIR\\{escape(p.relative_to(output).as_posix())}"' for p in sorted(output.rglob("*")) if p.is_file()]
    lines += [f'RMDir "$INSTDIR\\{escape(p.relative_to(output).as_posix())}"' for p in sorted(output.rglob("*"), key=lambda p: len(p.parts), reverse=True) if p.is_dir()]
    delete.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Prepared {output}; explicit uninstall manifest: {delete}")


if __name__ == "__main__":
    main()
