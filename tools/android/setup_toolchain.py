#!/usr/bin/env python3
"""Installs the toolchain that builds the Android APK on an aarch64 Linux box (PRoot under Termux),
where Google ships no NDK or aapt2: docs/android-local-toolchain.md.

Everything lands under --root (default ~/android), outside the repository. Every step checks what
is already there and does nothing then, so the script can be rerun after an interruption.

Usage: setup_toolchain.py [--root DIR] [--install-apt] [--verify-only]
"""

import argparse
import hashlib
import os
import shutil
import stat
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

LLVM_DIR = Path("/usr/lib/llvm-22/bin")
APT_PACKAGES = ["clang-22", "lld-22", "llvm-22", "openjdk-21-jdk-headless", "unzip"]
JAVA_HOME = Path("/usr/lib/jvm/java-21-openjdk-arm64")

NDK_VERSION = "30.0.16248370"
NDK_CLANG_MAJOR = 21  # the compiler Google's NDK r30 ships: its resource directory name
NDK_DIRECTORY = "android-ndk-r30"
NDK_PREBUILT = Path("toolchains/llvm/prebuilt/linux-x86_64")
ELF_MAGIC = b"\x7fELF"
ELF_MACHINE_OFFSET = 18
ELF_MACHINE_X86_64 = 0x3E
ELF_MACHINE_AARCH64 = 0xB7
ELF_HEADER_SIZE = 20
ORIGINAL_SUFFIX = ".x86_64.orig"

ANDROID_REPOSITORY = "https://dl.google.com/android/repository"
TERMUX_REPOSITORY = "https://packages.termux.dev/apt/termux-main"
TERMUX_PREFIX = Path("data/data/com.termux/files/usr")

CLANG_WRAPPER = f"""#!/usr/bin/env bash
# Stand-in for the NDK's x86_64 clang-{NDK_CLANG_MAJOR}, written by tools/android/setup_toolchain.py.
llvm_dir=$(cd "$(dirname "${{BASH_SOURCE[0]}}")/.." && pwd)
if [ "${{1:-}}" = "-cc1" ]; then exec {LLVM_DIR}/clang "$@"; fi
extra=()
case "$(basename "$0")" in *++) extra=(--driver-mode=g++ -stdlib=libc++) ;; esac
exec {LLVM_DIR}/clang "${{extra[@]}}" -resource-dir "$llvm_dir/lib/clang/{NDK_CLANG_MAJOR}" \\
  --sysroot="$llvm_dir/sysroot" -fuse-ld=lld --ld-path="$llvm_dir/bin/ld.lld" \\
  --rtlib=compiler-rt --unwindlib=libunwind -Wno-unused-command-line-argument "$@"
"""

# aapt2 from Termux is a bionic binary: its libraries are found through LD_LIBRARY_PATH.
AAPT2_WRAPPER = """#!/usr/bin/env bash
# Termux's aapt2 (Android 16), written by tools/android/setup_toolchain.py.
prefix="{prefix}"
LD_LIBRARY_PATH="$prefix/lib${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}" exec "$prefix/bin/aapt2" "$@"
"""


@dataclass(frozen=True)
class Download:
    url: str
    algorithm: str
    digest: str

    @property
    def file_name(self) -> str:
        return self.url.rsplit("/", 1)[-1].replace("%3A", ":")


NDK = Download(f"{ANDROID_REPOSITORY}/android-ndk-r30-linux.zip", "sha1",
               "5107f898313790e449e87eee2183d9a20602dee9")
GRADLE_VERSION = "8.11.1"
GRADLE = Download(f"https://services.gradle.org/distributions/gradle-{GRADLE_VERSION}-bin.zip", "sha256",
                   "f397b287023acdba1e9f6fc5ea72d22dd63669d59ed4a289a29b1a76eee151c6")
# The platform and build tools the Gradle project asks for (android/app/build.gradle: compileSdk 35;
# build tools are AGP 8.7.3's default). sdkmanager does not run here, so the archives are laid out by hand.
SDK_PLATFORM = Download(f"{ANDROID_REPOSITORY}/platform-35_r02.zip", "sha1",
                        "0bb560a90a7a2cbd0dd8348224d518b638fe7949")
SDK_BUILD_TOOLS = Download(f"{ANDROID_REPOSITORY}/build-tools_r34-linux.zip", "sha1",
                           "d6d58e0c6925a9e4d9a541e84cd1f405c2f9d2a9")
SDK_CMDLINE_TOOLS = Download(f"{ANDROID_REPOSITORY}/commandlinetools-linux-16111833_latest.zip", "sha1",
                             "e025545c62a8e64c7559119566a569fb1dec5f60")

# aapt2 of Android 15 and newer reads API 35's android.jar; Debian's (2.19) does not.
TERMUX_AAPT2 = [
    Download(f"{TERMUX_REPOSITORY}/pool/main/{path}", "sha256", digest)
    for path, digest in [
        ("a/aapt2/aapt2_16.0.0.4-4_aarch64.deb", "1b3e4d716670d78fed4a66e04a1ba92827a36fe3000e9bef98208392f3100050"),
        ("z/zlib/zlib_1.4_aarch64.deb", "e3027565ae50bcd8ba5113c988b589754ae192c9cc3e6afbe3e4ed7c06b2598e"),
        ("libz/libzopfli/libzopfli_1.0.3-5_aarch64.deb", "95cd7cb2209fbafb25825f5fcd4f86f021512175608e038b1c3d8d3fa0a4fe40"),
        ("libc/libc++/libc++_30_aarch64.deb", "53d0b84a7ba7459024257cb94d5b136fe13ef858567f65a8064b35950799f2ca"),
        ("libp/libpng/libpng_1.6.59_aarch64.deb", "32d61f0b1a60cf7283aa3a80e288737493df2c6b84c8c749606b77c94a10f2b2"),
        ("libe/libexpat/libexpat_2.10.0_aarch64.deb", "0e4411f01f6fb664f3f04f9420289472f9afc30db01ea1c5d1e57fd9fa2354be"),
        ("f/fmt/fmt_1%3A12.2.0_aarch64.deb", "4a2cb43ab167b32daf0d7978f21c5391d17bce47dae6aa2a8b3a8f958e663ce7"),
        ("libp/libprotobuf/libprotobuf_2%3A36.2-1_aarch64.deb", "daa3b9169e094644ba95c35d40a5cc283473144decc7dae8396c49cc51440815"),
        ("a/abseil-cpp/abseil-cpp_20260817.0_aarch64.deb", "b698963bb7096494ad928ce89cd869a53e6adf0b05f5f407b40fda9c85c6e2b0"),
    ]
]


class SetupError(Exception):
    pass


def default_root() -> Path:
    return Path.home() / "android"


def ndk_home(root: Path) -> Path:
    return root / "ndk" / NDK_DIRECTORY


def log(message: str) -> None:
    print(f"[setup] {message}", flush=True)


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess:
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    if result.returncode != 0:
        raise SetupError(f"{' '.join(command)} failed ({result.returncode}):\n{result.stdout}{result.stderr}")
    return result


def file_digest(path: Path, algorithm: str) -> str:
    digest = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch(download: Download, downloads: Path) -> Path:
    """Downloads into `downloads` (resumable) and checks the digest; a file already there and right is kept."""
    target = downloads / download.file_name
    if target.exists() and file_digest(target, download.algorithm) == download.digest:
        return target
    log(f"downloading {download.file_name}")
    # -4: IPv6 to these hosts hangs on the box this is written for; harmless elsewhere.
    run(["curl", "-4", "-fsSL", "--retry", "3", "-C", "-", "-o", str(target), download.url])
    actual = file_digest(target, download.algorithm)
    if actual != download.digest:
        target.unlink()
        raise SetupError(f"{download.file_name}: {download.algorithm} {actual}, expected {download.digest}")
    return target


def install_zip(download: Download, downloads: Path, top_directory: str, installed: Path) -> None:
    """Unpacks the archive's single top directory as `installed` unless it is there; unzip keeps modes and symlinks."""
    if installed.exists():
        return
    archive = fetch(download, downloads)
    log(f"unpacking {archive.name}")
    installed.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=installed.parent) as scratch:
        run(["unzip", "-q", str(archive), "-d", scratch])
        shutil.move(Path(scratch) / top_directory, installed)


def elf_machine(path: Path) -> int | None:
    """e_machine of a regular ELF file, None for scripts and symlinks."""
    if path.is_symlink() or not path.is_file():
        return None
    with path.open("rb") as stream:
        header = stream.read(ELF_HEADER_SIZE)
    if len(header) < ELF_HEADER_SIZE or header[:4] != ELF_MAGIC:
        return None
    return int.from_bytes(header[ELF_MACHINE_OFFSET:ELF_MACHINE_OFFSET + 2], "little")


def swap_ndk_binaries(ndk: Path) -> None:
    """Replaces the NDK's x86_64 tools by the system's arm64 LLVM; the sysroot is the same on any host."""
    bin_dir = ndk / NDK_PREBUILT / "bin"
    wrapper = bin_dir / f"clang-{NDK_CLANG_MAJOR}"
    if elf_machine(wrapper) == ELF_MACHINE_X86_64:
        wrapper.rename(wrapper.with_name(wrapper.name + ORIGINAL_SUFFIX))
    if not wrapper.exists() or wrapper.read_text() != CLANG_WRAPPER:
        log(f"writing {wrapper.name} (system clang)")
        wrapper.write_text(CLANG_WRAPPER)
        wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    for tool in sorted(bin_dir.iterdir()):
        if elf_machine(tool) != ELF_MACHINE_X86_64 or tool.name.endswith(ORIGINAL_SUFFIX):
            continue
        system_tool = LLVM_DIR / tool.name
        if not system_tool.exists():
            continue  # clang-tidy, lldb, ...: not needed to build and not in Debian's llvm
        tool.rename(tool.with_name(tool.name + ORIGINAL_SUFFIX))
        tool.symlink_to(system_tool)


def install_aapt2(root: Path, downloads: Path) -> Path:
    prefix = root / "termux-aapt2" / TERMUX_PREFIX
    wrapper = root / "bin" / "aapt2"
    if wrapper.exists() and (prefix / "bin" / "aapt2").exists():
        return wrapper
    for package in TERMUX_AAPT2:
        run(["dpkg-deb", "-x", str(fetch(package, downloads)), str(root / "termux-aapt2")])
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    wrapper.write_text(AAPT2_WRAPPER.format(prefix=prefix))
    wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return wrapper


def missing_apt_packages() -> list[str]:
    installed = run(["dpkg-query", "-W", "-f=${Package} ${Status}\n"]).stdout.splitlines()
    present = {line.split()[0] for line in installed if line.endswith("installed")}
    return [package for package in APT_PACKAGES if package not in present]


def check_apt(install: bool) -> None:
    missing = missing_apt_packages()
    if not missing:
        return
    command = ["sudo", "apt-get", "install", "-y", "--no-install-recommends", *missing]
    if not install:
        raise SetupError(f"missing system packages; run: {' '.join(command)}  (or pass --install-apt)")
    log(f"installing {' '.join(missing)}")
    run(command)


def install_all(root: Path) -> dict[str, Path]:
    downloads = root / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    ndk = ndk_home(root)
    install_zip(NDK, downloads, NDK_DIRECTORY, ndk)
    swap_ndk_binaries(ndk)

    gradle = root / "gradle" / f"gradle-{GRADLE_VERSION}"
    install_zip(GRADLE, downloads, f"gradle-{GRADLE_VERSION}", gradle)

    sdk = root / "sdk"
    install_zip(SDK_PLATFORM, downloads, "android-35", sdk / "platforms" / "android-35")
    install_zip(SDK_BUILD_TOOLS, downloads, "android-14", sdk / "build-tools" / "34.0.0")
    install_zip(SDK_CMDLINE_TOOLS, downloads, "cmdline-tools", sdk / "cmdline-tools" / "latest")
    return {"ndk": ndk, "gradle": gradle, "sdk": sdk, "aapt2": install_aapt2(root, downloads)}


def verify(paths: dict[str, Path]) -> None:
    """Builds a one-line C++ file for Android and starts the Gradle and aapt2 binaries."""
    compiler = paths["ndk"] / NDK_PREBUILT / "bin" / "aarch64-linux-android26-clang++"
    with tempfile.TemporaryDirectory() as scratch:
        source = Path(scratch) / "probe.cpp"
        source.write_text("#include <print>\nint main() { std::println(\"probe\"); }\n")
        binary = Path(scratch) / "probe"
        run([str(compiler), "-std=c++23", str(source), "-o", str(binary)])
        if elf_machine(binary) != ELF_MACHINE_AARCH64:
            raise SetupError("the probe is not an aarch64 binary")
    log("clang++ builds an Android aarch64 C++23 binary")
    environment = {**os.environ, "JAVA_HOME": str(JAVA_HOME)}
    run([str(paths["gradle"] / "bin" / "gradle"), "--version"], env=environment)
    log("gradle starts")
    version = run([str(paths["aapt2"]), "version"])  # aapt2 prints it on stderr
    log((version.stdout + version.stderr).strip())


def print_environment(paths: dict[str, Path]) -> None:
    log("environment for the Gradle build:")
    print(f"  export JAVA_HOME={JAVA_HOME}\n  export ANDROID_HOME={paths['sdk']}\n"
          f"  export ANDROID_NDK_HOME={paths['ndk']}\n  {paths['gradle']}/bin/gradle ... "
          f"-Pandroid.aapt2FromMavenOverride={paths['aapt2']}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=default_root())
    parser.add_argument("--install-apt", action="store_true", help="install the missing system packages with sudo")
    parser.add_argument("--verify-only", action="store_true", help="only check what is installed")
    args = parser.parse_args()
    try:
        check_apt(args.install_apt)
        root = args.root.expanduser()
        if args.verify_only:
            paths = {"ndk": ndk_home(root), "gradle": root / "gradle" / f"gradle-{GRADLE_VERSION}",
                     "sdk": root / "sdk", "aapt2": root / "bin" / "aapt2"}
        else:
            paths = install_all(root)
        verify(paths)
    except SetupError as error:
        print(f"[setup] {error}", file=sys.stderr)
        return 1
    print_environment(paths)
    return 0


if __name__ == "__main__":
    sys.exit(main())
