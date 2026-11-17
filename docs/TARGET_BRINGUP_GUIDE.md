# QNX 8.0 Target Bring-Up Guide (Manual Workflow)

A concise, step-by-step guide to building the driver, packaging the QNX IFS boot image, starting the target VM, and verifying execution directly from Windows Command Prompt (`cmd.exe`) without using automation scripts.

## 1. Prerequisites & Environment Setup

Ensure your host machine has:

* **QNX SDP 8.0** installed.

* **OpenSSH client tools** (`ssh` and `scp`) on your system `PATH`.

* **Windows Command Prompt (`cmd.exe`)** — Use `cmd.exe` rather than PowerShell so `qnxsdp-env.bat` exports toolchain variables cleanly.

### Initialize the QNX SDP Environment

Open `cmd.exe` in the repository root (`qnx8-resmgr-driver/`) and load the QNX environment script (adjust the path if your SDP is installed elsewhere):

```
call D:\qnx_downloads\qnxsdp-env.bat

```

## 2. Project Directory Layout

```
qnx8-resmgr-driver/
├── src/driver/             # Driver source files (e.g., dummy_driver.c)
├── bsp/                    # Custom mkqnximage *.custom override snippets
├── bin/                    # Output directory for compiled binaries
└── vm-test/                # Local mkqnximage working directory

```

## 3. Compile the Driver (`qcc`)

Create the output directory and cross-compile your C source directly with `qcc`:

* **For x86_64 (VirtualBox / QEMU):**

  ```
  mkdir bin\x86_64 2>nul
  qcc -Vgcc_ntox86_64 src\driver\dummy_driver.c -lslog2 -o bin\x86_64\dummy_driver
  
  ```

* **For aarch64le (Raspberry Pi 4):**

  ```
  mkdir bin\aarch64le 2>nul
  qcc -Vgcc_ntoaarch64le src\driver\dummy_driver.c -lslog2 -o bin\aarch64le\dummy_driver
  
  ```

## 4. Package the IFS Image & Boot the VM (`mkqnximage`)

1. **Sync custom snippets into your local `mkqnximage` workspace:**

   ```
   mkdir vm-test\local\snippets 2>nul
   copy /Y bsp\*.custom vm-test\local\snippets\
   
   ```

2. **Build the IFS boot image and launch the VM:**

   ```
   cd vm-test
   mkqnximage --build
   mkqnximage --run
   
   ```

3. **Retrieve the target's IP address:**

   ```
   mkqnximage --getip
   cd ..
   
   ```

*(When you are finished testing, stop the managed VM from `vm-test\` using `mkqnximage --stop`.)*

## 5. Fast Iteration: Deploy & Restart Over SSH

If the target is already booted and you only modified C source code, copy the binary over `scp` and restart it via `ssh` (replace `<target-ip>` with your VM or Pi IP):

1. **Copy the binary to `/tmp/` on the target:**

   ```
   scp -o MACs=hmac-sha2-256-etm@openssh.com bin\x86_64\dummy_driver root@<target-ip>:/tmp/
   
   ```

2. **Restart the process in the background:**

   ```
   ssh -o MACs=hmac-sha2-256-etm@openssh.com root@<target-ip> "slay -f dummy_driver 2>/dev/null; chmod +x /tmp/dummy_driver && (/tmp/dummy_driver >/dev/null 2>&1 </dev/null &)"
   
   ```

## 6. Verify Execution on the Target

Connect to the target over SSH (`ssh -o MACs=hmac-sha2-256-etm@openssh.com root@<target-ip>`) and run:

* **Confirm the driver process is running:**

  ```
  pidin | grep dummy_driver
  
  ```

* **Stream live logs from `slog2`:**

  ```
  slog2info -w
  
  ```