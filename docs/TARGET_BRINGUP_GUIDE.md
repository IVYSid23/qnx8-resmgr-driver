# QNX 8.0 Target Bring-Up Guide

How to compile the driver, package it into a QNX boot image, boot a target, and check that it runs.

There are two ways to do it, and they do exactly the same things:

* **The script** (`scripts/auto_qnx.py`): one command. Recommended for everyday use.
* **The manual steps** (section 4): the commands the script runs. Use them to understand what is happening, or if the script fails.

> **Status.** The script has been tested on Windows with VirtualBox and QEMU. Linux and macOS paths are written but not yet tested on real machines. If something breaks, please open an issue with the full command and its output.

## 1. One-time setup

You need:

* **QNX SDP 8.0** installed.
* **Python 3** (for the script) and the **OpenSSH client** (`ssh`, `scp`, `ssh-keygen`) on your `PATH`.
* **A target to run on.** VirtualBox needs a host-only network with DHCP enabled (VirtualBox: Tools > Network). QEMU needs `qemu-system-x86_64`. A Raspberry Pi needs to be booted and reachable on your network.

### 1.1 Create an SSH key (skip if you already have one)

```
ssh-keygen -t ed25519
```

Press Enter to accept the default location. The public key (`~/.ssh/id_ed25519.pub`) gets built into the target image so you can log in without a password. The private key never leaves your computer and must never be committed.

### 1.2 Tell the tools where the SDP is

The SDP environment script is `qnxsdp-env.bat` on Windows and `qnxsdp-env.sh` on Linux and macOS. Set the path once and the script loads it for you.

| System | Command |
|---|---|
| Windows (then open a new `cmd`) | `setx QNX_SDP_ENV "D:\qnx_downloads\qnxsdp-env.bat"` |
| Linux / macOS (add to `~/.bashrc`) | `export QNX_SDP_ENV=~/qnx800/qnxsdp-env.sh` |

Adjust the path to where the SDP is installed. If you prefer, load the environment yourself in each terminal (`call ...\qnxsdp-env.bat` or `source .../qnxsdp-env.sh`) and the script will detect it.

### 1.3 Windows only: the SSH MAC workaround

The Windows OpenSSH client can fail with `Corrupted MAC on input`. Fix it once by creating `%USERPROFILE%\.ssh\config` (no file extension) with:

```
Host 192.168.56.* 127.0.0.1
    MACs hmac-sha2-256-etm@openssh.com,hmac-sha2-512-etm@openssh.com
```

The script applies the same setting on its own. This file is for plain `ssh` and `scp` commands.

## 2. Project layout

```
qnx8-resmgr-driver/
├── src/driver/     driver source files
├── bsp/            *.custom snippets copied into the boot image
├── bin/            compiled binaries (not committed)
├── vm-test/        VirtualBox workspace, created by the script (not committed)
├── qemu-test/      QEMU workspace, created by the script (not committed)
├── scripts/        auto_qnx.py
└── docs/
```

`vm-test/`, `qemu-test/` and `bin/` belong in `.gitignore`. They contain generated files and your key.

The snippets in `bsp/` do these jobs:

| File | Purpose |
|---|---|
| `ifs_files.custom` | Puts the compiled binary into the boot image. Uses `@BIN@` as a placeholder for its path |
| `post_start.custom` | Starts the driver at the end of boot |
| `system_files.custom`, `ifs_env.custom` | Install the `sshd-auth` helper that the SSH server needs |

## 3. Quick start with the script

Run everything from the repository root. The same commands work in `cmd`, PowerShell and bash.

**Build the boot image with the driver inside, boot it, and check that it runs** (VirtualBox is the default target):

```
python scripts/auto_qnx.py src/driver/dummy_driver.c -l slog2 --rebuild-image --mode verify
```

The first time, this also creates the `vm-test/` workspace. Expect a few minutes.

**After editing code, push the new binary to a target that is already running:**

```
python scripts/auto_qnx.py src/driver/dummy_driver.c -l slog2 -o my_driver --mode daemon
```

`-o my_driver` replaces the copy that started at boot instead of starting a second one.

**Stop the VM:**

```
python scripts/auto_qnx.py --stop-vm
```

**Other targets:**

```
# QEMU (on Windows, add --qemu-launch custom; see "QEMU" in section 4)
python scripts/auto_qnx.py src/driver/dummy_driver.c -l slog2 --target qemu --qemu-launch custom --rebuild-image --mode verify

# A Raspberry Pi that is already booted (deploy only; it cannot build the Pi image yet)
python scripts/auto_qnx.py src/driver/dummy_driver.c -l slog2 --target pi --arch aarch64le --host <pi-ip> --mode daemon
```

**Everything else.** Run `python scripts/auto_qnx.py --help` to list every option, and see the table in section 6.

## 4. Manual steps (what the script does)

Run these from the repository root in `cmd` (Windows) after loading the SDP environment. Linux and macOS equivalents are in section 4.7.

### 4.1 Compile

Name the binary `my_driver`, because that is the name the snippets use:

```
mkdir bin\x86_64 2>nul
qcc -Vgcc_ntox86_64 src\driver\dummy_driver.c -lslog2 -o bin\x86_64\my_driver
```

For a Raspberry Pi (aarch64), use `-Vgcc_ntoaarch64le` and `bin\aarch64le`.

### 4.2 Create the workspace (first time only)

```
mkdir vm-test
cd vm-test
mkqnximage --type=vbox --ssh-ident="C:/Users/<you>/.ssh/id_ed25519.pub" --build
cd ..
```

Use forward slashes in the key path, or `mkqnximage` drops the backslashes. For QEMU use `--type=qemu` in a folder such as `qemu-test`.

### 4.3 Add the snippets

```
copy /Y bsp\*.custom vm-test\local\snippets\
```

Then open `vm-test\local\snippets\ifs_files.custom` and replace `@BIN@` with the full path of the compiled binary, with forward slashes:

```
[perms=0755] /proc/boot/my_driver = C:/path/to/qnx8-resmgr-driver/bin/x86_64/my_driver
```

(`mkdir vm-test\local\snippets` first if the folder does not exist.)

### 4.4 Build and boot (VirtualBox)

```
cd vm-test
mkqnximage --ssh-ident="C:/Users/<you>/.ssh/id_ed25519.pub" --build
mkqnximage --run
mkqnximage --getip
```

**Pass `--ssh-ident` on every build.** The key is not remembered, so an image built without it has no key and ssh asks for a password. Stop the VM when done with `mkqnximage --stop`.

### 4.5 QEMU

On Windows, `mkqnximage --run` has started QEMU without a network card, so there is nothing to ssh to. Start QEMU by hand from the QEMU workspace, with a network card and a forward from port 2222 on your computer to port 22 in the VM:

```
qemu-system-x86_64 -smp 2 --cpu max -m 1G -drive file=output/disk-qemu.vmdk,if=ide,id=drv0 -pidfile output/qemu.pid -nographic -kernel output/ifs.bin -serial mon:stdio -netdev user,id=net0,hostfwd=tcp::2222-:22 -device virtio-net-pci,netdev=net0
```

Leave that window open, since it is the VM's console. Press Ctrl+A then X to quit. Connect with `ssh -p 2222 root@127.0.0.1`, and copy with `scp -P 2222` (capital P). Linux hosts may not need any of this, so try plain `mkqnximage --run` first.

### 4.6 Check the driver and iterate

Connect: `ssh root@<ip>` (use the address from `mkqnximage --getip`). Then on the target:

```
pidin | grep my_driver
slog2info -w -b dummy_driver
```

The log buffer name is whatever the driver registers with slog2. The dummy driver uses `dummy_driver`.

To push a new binary without rebuilding the image:

```
scp bin\x86_64\my_driver root@<ip>:/tmp/
ssh root@<ip> "slay -f my_driver 2>/dev/null; chmod +x /tmp/my_driver && (/tmp/my_driver >/dev/null 2>&1 </dev/null &)"
```

### 4.7 Linux and macOS equivalents

| Windows `cmd` | Linux / macOS |
|---|---|
| `call D:\...\qnxsdp-env.bat` | `source ~/qnx800/qnxsdp-env.sh` |
| `mkdir bin\x86_64 2>nul` | `mkdir -p bin/x86_64` |
| `copy /Y bsp\*.custom vm-test\local\snippets\` | `cp bsp/*.custom vm-test/local/snippets/` |
| Paths with `\` | Paths with `/` |
| MAC workaround needed | Not needed |

## 5. Troubleshooting

| Message or symptom | Likely cause | What to do |
|---|---|---|
| `Permission denied (publickey...)` | The image has no key | Rebuild with the key: `--rebuild-image` (script), or `--ssh-ident=...` on `mkqnximage --build` |
| `REMOTE HOST IDENTIFICATION HAS CHANGED` | The image was rebuilt and has new host keys | `ssh-keygen -R <ip>` (or `ssh-keygen -R "[127.0.0.1]:2222"` for QEMU). The script does this itself after a rebuild |
| `Corrupted MAC on input` | Windows SSH client bug | Add the config from 1.3 |
| `Connection timed out during banner exchange` (QEMU) | The VM is still booting | Wait and retry. The script waits for the SSH banner automatically |
| QEMU boots but nothing answers on 2222 | `mkqnximage --run` started it without a network | Use `--qemu-launch custom`, or the command in 4.5 |
| `QNX SDP environment script not found` | `QNX_SDP_ENV` is not set | See 1.2, or pass `--sdp-env` |
| `undefined reference to slog2...` | Library not linked | Add `-l slog2` (script) or `-lslog2` (`qcc`) |
| Script waits at "Running the binary" | The program never exits | Use `--mode daemon`, which starts it in the background |
| `no mkqnximage workspace yet` | The workspace folder does not exist | Run once with `--rebuild-image` |
| `Permission denied` after rebuild only on the Pi | The Pi image is built outside this script | Make sure the Pi image contains your public key |

## 6. Script option reference

`python scripts/auto_qnx.py [options] [source files]`. The same list is printed by `--help`.

| Option | Meaning | Default |
|---|---|---|
| `sources` | C/C++ files to compile | none (not needed with `--rebuild-image`, `--mode verify`, `--stop-vm`) |
| `-o`, `--output` | Name of the compiled binary | `my_driver` when rebuilding the image, otherwise the first source's name |
| `-l`, `--libs` | Libraries to link, without `-l`: `-l slog2 socket` | none |
| `--target` | `vbox`, `qemu` or `pi` | `vbox` |
| `--arch` | `x86_64` or `aarch64le` | `x86_64` (`aarch64le` for `pi`) |
| `--rebuild-image` | Sync snippets, build the boot image, boot it. Creates the workspace on first use | off |
| `--mode` | `oneshot`: run in the foreground. `daemon`: restart in the background. `verify` (or `image-only`): only check the running driver and slog2 | `oneshot` |
| `--stop-vm` | Stop the VM and exit | off |
| `--ssh-ident` | Public key to put in the image | `~/.ssh/id_ed25519.pub` |
| `--qemu-launch` | `stock`: `mkqnximage --run`. `custom`: our QEMU command with network and port forward | `stock` |
| `--qemu-bin` | QEMU program for `custom` | `qemu-system-x86_64` |
| `--host` | Target address | found with `mkqnximage --getip`; `127.0.0.1` for custom QEMU; required for `pi` |
| `--port` | SSH port | 22; 2222 for custom QEMU |
| `--user` | SSH user | `root` |
| `--identity` | Private key file for ssh/scp | the SSH default |
| `--macs` | Override the SSH MAC list | set automatically on Windows only |
| `--boot-timeout` | Seconds to wait for SSH | 120 |
| `--run-timeout` | Seconds before a `oneshot` run is abandoned | 30 |
| `--target-dir` | mkqnximage workspace folder | `vm-test` (vbox) or `qemu-test` (qemu) in the project root |
| `--snippets-dir` | Folder with `*.custom` snippets | `bsp` |
| `--bin-dir` | Folder for compiled binaries | `bin` |
| `--root` | Project root | the folder above `scripts/` |
| `--sdp-env` | Path to `qnxsdp-env.bat` / `.sh` | `$QNX_SDP_ENV` |