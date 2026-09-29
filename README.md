![picokit-45-fault-analysis](https://raw.githubusercontent.com/mytechnotalent/picokit-45-fault-analysis/main/picokit-45-fault-analysis.png)

<br>

## FREE Reverse Engineering Self-Study Course [HERE](https://github.com/mytechnotalent/reverse-engineering)
## FREE Embedded Hacking Course [HERE](https://github.com/mytechnotalent/Embedded-Hacking)

<br>

# PICOKIT-45 FAULT ANALYSIS

### Controlled Fault and Cortex-M33 Fault Status Registers
#### Lesson 45 of the Picokit Series

<br>

***
**LEGAL DISCLAIMER:**
The information, tools, and code provided in this repository and course are strictly for educational, research, and defensive purposes only.

You are explicitly prohibited from using any materials contained herein to access, test, modify, or exploit any device, network, or system that you do not own 100% or for which you do not have explicit, documented, and legally binding authorization to interact with.

By using this repository and course, you acknowledge and agree that:

1. Any illegal, unauthorized, or malicious use of this information is solely your responsibility.
2. The author(s) and contributor(s) of this repository and course shall not be held liable for any damages, legal repercussions, criminal charges, or unauthorized actions resulting from the use, misuse, or abuse of the contents herein.
3. You will comply with all applicable local, state, national, and international laws regarding cybersecurity and computer fraud.

**IF YOU DO NOT AGREE WITH THESE TERMS, DO NOT USE THIS REPOSITORY AND COURSE.**
***

<br>
<br>

## Overview

<br>

The forty-fifth Picokit lesson. The node reads the Cortex-M33
Configurable Fault Status Register, reports it in an authenticated heartbeat,
and clears it after each report. The debug lab triggers a controlled fault and
reads the fault status registers over SWD.

<br>

## What it teaches

<br>

- Reading the Cortex-M33 CFSR at 0xE000ED28 from firmware.
- Clearing a write-one-to-clear fault register after reporting it.
- Triggering a controlled fault under the debugger.
- Reading CFSR and SHCSR over SWD when the fault fires.

<br>

## Hardware

| Peripheral | Pico 2 pin | Role |
| --- | --- | --- |
| Red / Yellow / Green | GP16 / GP18 / GP17 | annunciator status |
| Onboard LED | GP25 | heartbeat, one blink per transmit |
| RYLR998 | GP8 TX / GP9 RX | LoRa heartbeat |
| Debug Probe | SWCLK/SWDIO/GND, GP0/GP1 | SWD and the console |

<br>

## How it works

<br>

The node runs `monitor_step` in a loop. Every 5 seconds it reads and
clears the fault status register, seals `{"n":45,"s":<seq>,"f":<fault>}` with
the shared field key, and sends it over LoRa. An intact run reports `f=0`.

<br>

## Build and flash

```bash
cd firmware
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350-arm-s
cmake --build build
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
  -c "program build/picokit_45_fault_analysis verify reset exit"
```

<br>

## Watch the node

Open the console at 115200 and reset:

```text
BOOT
=== PICOKIT-45 FAULT ANALYSIS // CFSR READ + DEBUG LAB ===
FAULT n=45 f=0x00000000 seq=1
FAULT n=45 f=0x00000000 seq=2
RX from 0x0001, N bytes
```

<br>

## The gateway

```bash
cd gateway
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python3 listen.py --port /dev/cu.usbserial-A50285BI --hub 0001 --network 18 --db gateway.db
```

It prints `OK node=45 rssi=...` per authenticated heartbeat. The terminal
dashboard `python3 tui.py --db gateway.db` and the web dashboard
`python3 web/app.py --db gateway.db` show the same rows.

<br>

## Debug lab: controlled fault and fault status registers

The Debug Probe attaches with CMSIS-DAP and OpenOCD. Build with debug info,
start OpenOCD, and attach GDB:

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg
arm-none-eabi-gdb build/picokit_45_fault_analysis.elf
(gdb) target extended-remote localhost:3333
(gdb) monitor reset halt
(gdb) break monitor_fault_poll
(gdb) continue
```

Force a controlled UsageFault by trapping divide by zero, then let it run:

```text
(gdb) set *(unsigned int *)0xE000ED14 = 0x10   # CCR.DIV_0_TRP = 1
(gdb) continue
```

When the fault fires and the handler halts, read the fault status registers:

```text
(gdb) x/1xw 0xE000ED28   # CFSR
0xe000ed28: 0x02000000
(gdb) x/1xw 0xE000ED24   # SHCSR
(gdb) print g_fault
```

Firmware reads the same CFSR at 0xE000ED28 in `monitor_fault_read` and reports
it as `f` in `{"n":45,"s":<seq>,"f":<fault>}`.

<br>

## Verify

```bash
python3 .opencode/skill/embedded-c-standard/audit_c_standard.py
python3 .opencode/skill/embedded-python-standard/audit_python_standard.py
python3 .opencode/skill/iot-readme-standard/validate_readme.py
python3 .opencode/skill/iot-banner-standard/validate_banner.py
python3 scripts/run_tests.py
python3 scripts/check_coverage.py
```

<br>

# Next
[picokit-46-binary-recon](https://github.com/mytechnotalent/picokit-46-binary-recon)

<br>

# License
[MIT License](https://github.com/mytechnotalent/picokit-45-fault-analysis/blob/main/LICENSE)
