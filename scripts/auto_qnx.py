import argparse
import re
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

# Default workspace & toolchain paths
DEFAULT_WORKSPACE = Path(r"D:\qnx-hackathon-build")
DEFAULT_TARGET_DIR = DEFAULT_WORKSPACE / "vm-test"
DEFAULT_SNIPPETS_DIR = DEFAULT_WORKSPACE / "snippets"
DEFAULT_BIN_DIR = DEFAULT_WORKSPACE / "bin"
BAT_PATH = r"D:\qnx_downloads\qnxsdp-env.bat"

ARCH_COMPILERS = {
    "x86_64": "gcc_ntox86_64",
    "aarch64le": "gcc_ntoaarch64le",
}

MACS = "hmac-sha2-256-etm@openssh.com"  # Win32-OpenSSH workaround
BOOT_TIMEOUT = 120
IP_PATTERN = r"\b\d{1,3}(?:\.\d{1,3}){3}\b"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Compile QNX binaries, sync snippets, build/boot images, and deploy over SSH."
    )
    parser.add_argument(
        "sources",
        nargs="*",
        type=Path,
        help="C/C++ source file(s) to compile (optional if only rebuilding/booting the image).",
    )
    parser.add_argument(
        "-o", "--output",
        help="Output binary name (default: 'my_driver' if rebuilding image, else first source stem).",
    )
    parser.add_argument(
        "-l", "--libs",
        nargs="*",
        default=[],
        help="Libraries to link against, e.g., -l slog2 socket",
    )
    parser.add_argument(
        "--arch",
        choices=ARCH_COMPILERS.keys(),
        default="x86_64",
        help="Target CPU architecture (default: x86_64; use aarch64le for Raspberry Pi).",
    )
    parser.add_argument(
        "--target-dir",
        type=Path,
        default=DEFAULT_TARGET_DIR,
        help=f"mkqnximage target directory (default: {DEFAULT_TARGET_DIR})",
    )
    parser.add_argument(
        "--snippets-dir",
        type=Path,
        default=DEFAULT_SNIPPETS_DIR,
        help=f"Portable snippets directory (default: {DEFAULT_SNIPPETS_DIR})",
    )
    parser.add_argument(
        "--rebuild-image",
        action="store_true",
        help="Sync snippets/*.custom, stop VM, and run mkqnximage --build before booting.",
    )
    parser.add_argument(
        "--no-vm",
        action="store_true",
        help="Skip mkqnximage VM lifecycle commands (for real Raspberry Pi or manual QEMU).",
    )
    parser.add_argument(
        "--host",
        help="Explicit target IP/hostname (skips mkqnximage --getip; e.g., 127.0.0.1 for QEMU).",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=22,
        help="Target SSH port (default: 22; use 2222 for QEMU port-forwarding).",
    )
    parser.add_argument(
        "--mode",
        choices=["oneshot", "daemon", "image-only"],
        default="oneshot",
        help=(
            "oneshot: scp to /tmp and run synchronously; "
            "daemon: scp to /tmp, restart in background, and print recent slog2info; "
            "image-only: compile and rebuild/boot IFS without scp to /tmp."
        ),
    )

    args = parser.parse_args()

    if not args.sources and not args.rebuild_image:
        parser.error("Provide at least one source file, or pass --rebuild-image.")

    resolved_sources = []
    for p in args.sources:
        rp = p.resolve()
        if not rp.is_file():
            parser.error(f"Source file not found: {rp}")
        resolved_sources.append(rp)
    args.sources = resolved_sources

    if not args.output:
        args.output = "my_driver" if args.rebuild_image else args.sources[0].stem

    return args


def run_qnx_cmd(command, cwd=None, check=True):
    """Run a command inside cmd.exe with QNX SDP environment variables loaded."""
    cd_part = f'cd /d "{cwd}" && ' if cwd else ""
    full_cmd = f'call "{BAT_PATH}" >nul && {cd_part}{command}'
    try:
        result = subprocess.run(
            full_cmd, shell=True, capture_output=True, check=check, text=True
        )
        return result
    except subprocess.CalledProcessError as e:
        print(f"\n[ERROR] Command failed (exit code {e.returncode}):")
        print(f"  {command}")
        if e.stdout.strip():
            print(f"Stdout:\n{e.stdout}")
        if e.stderr.strip():
            print(f"Stderr:\n{e.stderr}")
        sys.exit(1)


def run_host_cmd(command, cwd=None, check=True):
    """Run a standard Windows host command (ssh, scp)."""
    try:
        return subprocess.run(
            command, cwd=cwd, shell=True, capture_output=True, check=check, text=True
        )
    except subprocess.CalledProcessError as e:
        print(f"\n[ERROR] Host command failed (exit code {e.returncode}):")
        print(f"  {command}")
        if e.stderr.strip():
            print(f"Stderr:\n{e.stderr}")
        sys.exit(1)


def find_ip(output):
    for candidate in reversed(re.findall(IP_PATTERN, output)):
        if all(0 <= int(part) <= 255 for part in candidate.split(".")):
            return candidate
    return None


def get_target_ip(target_dir):
    res = run_qnx_cmd("mkqnximage --getip", cwd=target_dir, check=False)
    return find_ip(res.stdout)


def ssh_is_up(host, port):
    try:
        with socket.create_connection((host, port), timeout=2):
            return True
    except OSError:
        return False


def wait_for_target(target_dir, explicit_host, port, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        ip = explicit_host or get_target_ip(target_dir)
        if ip and ssh_is_up(ip, port):
            return ip
        print("  waiting for target SSH...")
        time.sleep(3)
    return None


def sync_snippets(snippets_dir, target_dir):
    dest_dir = target_dir / "local" / "snippets"
    if not snippets_dir.is_dir():
        print(f"[WARN] Snippets folder not found at {snippets_dir}, skipping sync.")
        return
    dest_dir.mkdir(parents=True, exist_ok=True)
    custom_files = list(snippets_dir.glob("*.custom"))
    for src_file in custom_files:
        shutil.copy2(src_file, dest_dir / src_file.name)
    print(f"Synced {len(custom_files)} snippet(s) into {dest_dir}")


def main():
    args = parse_args()
    out_dir = DEFAULT_BIN_DIR / args.arch
    out_dir.mkdir(parents=True, exist_ok=True)
    binary_path = out_dir / args.output

    # 1. Compile source files if provided
    if args.sources:
        compiler = ARCH_COMPILERS[args.arch]
        src_str = " ".join(f'"{p}"' for p in args.sources)
        lib_str = " ".join(f"-l{lib}" for lib in args.libs)
        compile_cmd = f'qcc -V{compiler} {src_str} {lib_str} -o "{binary_path}"'
        print(f"Compiling for {args.arch} -> {binary_path}...")
        run_qnx_cmd(compile_cmd)
        print("Compile successful.")

    # 2. Optional full IFS image rebuild (for testing boot-time driver startup)
    if args.rebuild_image:
        sync_snippets(args.snippets_dir, args.target_dir)
        if not args.no_vm:
            print("Stopping existing VM (if running)...")
            run_qnx_cmd("mkqnximage --stop", cwd=args.target_dir, check=False)
        print("Building QNX image filesystem (mkqnximage --build)...")
        run_qnx_cmd("mkqnximage --build", cwd=args.target_dir)
        print("Image build complete.")

    # 3. Ensure target VM is running (unless --no-vm is passed)
    if not args.no_vm:
        current_ip = args.host or get_target_ip(args.target_dir)
        if current_ip and ssh_is_up(current_ip, args.port):
            print(f"Target already online at {current_ip}:{args.port} (skipping --run).")
        else:
            print("Starting VM (mkqnximage --run)...")
            run_qnx_cmd("mkqnximage --run", cwd=args.target_dir)

    print("Waiting for SSH...")
    target_ip = wait_for_target(args.target_dir, args.host, args.port, BOOT_TIMEOUT)
    if not target_ip:
        print(f"[ERROR] SSH unreachable after {BOOT_TIMEOUT}s.")
        sys.exit(1)
    print(f"Target reachable at {target_ip}:{args.port}")

    # 4. Deploy / verify based on mode
    ssh_base = f"ssh -p {args.port} -o MACs={MACS} root@{target_ip}"
    scp_base = f"scp -P {args.port} -o MACs={MACS}"

    if args.mode == "image-only":
        print("\nVerifying boot-started process & slog2 output on target...")
        res = run_host_cmd(f'{ssh_base} "pidin | grep {args.output} && slog2info | tail -n 10"', check=False)
        print("------------ Target Status ------------")
        print(res.stdout or "(Process not found in pidin yet)")
        print("---------------------------------------")
        return

    print(f"\nCopying {binary_path.name} to target:/tmp/...")
    run_host_cmd(f'{scp_base} "{binary_path}" root@{target_ip}:/tmp/')

    if args.mode == "oneshot":
        print("Running one-shot binary on target...")
        res = run_host_cmd(f'{ssh_base} "chmod +x /tmp/{args.output} && /tmp/{args.output}"')
        print("------------ Result ------------")
        print(res.stdout)
        print("--------------------------------")

    elif args.mode == "daemon":
        print(f"Restarting {args.output} as background daemon on target...")
        remote_cmd = (
            f"slay -f {args.output} 2>/dev/null; "
            f"chmod +x /tmp/{args.output} && "
            f"/tmp/{args.output} >/dev/null 2>&1 & "
            f"sleep 2 && pidin | grep {args.output} && slog2info | tail -n 10"
        )
        res = run_host_cmd(f'{ssh_base} "{remote_cmd}"')
        print("------------ Daemon Status ------------")
        print(res.stdout)
        print("---------------------------------------")


if __name__ == "__main__":
    main()