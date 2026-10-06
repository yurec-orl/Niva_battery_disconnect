# Car Battery Disconnect & Door Alarm Device (STM8S Variant)

## Overview

An automotive device based on **STM8S103F3P6** (TSSOP-20) that:

1. **Door Open Alarm** - Emits a pulsing beep when the door is open (via door switch)
2. **Battery Monitor** - Checks battery voltage once per hour; activates a disconnect
   solenoid if voltage drops below a user-configurable threshold
3. **Ignition Interlock** - Blocks the battery disconnect solenoid while ignition is ON
4. **Settings UI** - 3-digit 7-segment display (TM1637) + two buttons to adjust the
   disconnect voltage threshold in 0.1V steps. Display stays off until a button is
   pressed and turns off after 5 seconds of inactivity. Threshold is saved to EEPROM.

---

## MCU: STM8S103F3P6 (TSSOP-20)

- Mainstream STM8S family, 16 MHz advanced core
- 8 KB Flash, 1 KB RAM, 640 B true data EEPROM (300k write cycles)
- 10-bit ADC (5 channels), **no hardware beeper** (use timer PWM)
- Auto-wakeup unit (AWU) for periodic wake from Active-Halt
- External interrupts on all GPIO ports (wake from halt)
- Supply: 2.95-5.5 V | Temp: -40 to +85 C

---

## Pin Assignment

### STM8S103F3P6 Dev Board Header Pinout

```
STM8S103F3P6 Dev Board

        LEFT                          RIGHT
        ----                          -----
                                  PD4   o  1 +------------------+  1  o  PD3       <- BTN_UP "+0.1V" (EXTI_PORTD)
Battery voltage ADC (AIN5) -->    PD5   o  2 |  STM8S103F3P6    |  2  o  PD2       <- BTN_DOWN "-0.1V" (EXTI_PORTD)
Solenoid driver (N-FET gate) -->  PD6   o  3 |    Dev Board     |  3  o  PD1/SWIM  <- Programming
                                  RST   o  4 |                  |  4  o  PC7       <- Display power (2N7000 N-FET gate, HIGH=ON)
                                  PA1   o  5 |                  |  5  o  PC6       <- TM1637 DIO
                                  PA2   o  6 |                  |  6  o  PC5       <- TM1637 CLK
                                  GND   o  7 |                  |  7  o  PC4       <- Ignition detect
                                   5V   o  8 |                  |  8  o  PC3       <- Door switch (EXTI_PORTC)
                                  3.3V  o  9 |                  |  9  o  PB4       (spare -- open-drain, no pull-up)
                                   A3   o 10 |                  | 10  o  PB5       (spare / onboard LED)
                                             +------------------+

Pin usage (dev board):
  PD5  (left-2)  <- Battery voltage ADC (AIN5)
  PD6  (left-3)  <- Solenoid driver (N-FET gate)
  PD3  (right-1) <- BTN_UP "+0.1V" (EXTI_PORTD, internal pull-up)
  PD2  (right-2) <- BTN_DOWN "-0.1V" (EXTI_PORTD, internal pull-up)
  PD1  (right-3) <- SWIM programming
  PC7  (right-4) <- Display power (2N7000 N-FET gate, HIGH = ON, LOW = OFF)
  PC6  (right-5) <- TM1637 DIO
  PC5  (right-6) <- TM1637 CLK
  PC4  (right-7) <- Ignition detect
  PC3  (right-8) <- Door switch (EXTI_PORTC, internal pull-up)

Pins NOT broken out on this board (connect directly to IC if needed):
  PC2  <- Buzzer PWM (TIM1_CH2)
```

## Power Budget

### Dev board parasitic loads (NOT present on final PCB)

The STM8S103F3P6 dev board has two always-on loads that dominate bench
measurements but are absent from the final custom PCB:

| Component                        | Current     | Notes                                    |
|----------------------------------|-------------|------------------------------------------|
| Dev board power indicator LED    | ~2.3 mA     | Blue SMD LED, ~1kΩ series R, always ON   |
| TM1637 module (idle, no display) | ~0.07 mA    | Module powered but showing nothing       |
| **Dev board parasitic total**    | **~2.4 mA** | Dominates all bench current measurements |

These will not be present on the final PCB. To measure true MCU standby current
on the dev board: desolder the power indicator LED, or feed 3.3V directly into
the 3V3 pin and leave USB disconnected (bypasses the LDO and its indicator LED).

### Normal standby (display OFF, door closed, once-per-hour wakeup)

| Component                  | Current      | Duration / Notes                                |
|----------------------------|--------------|-------------------------------------------------|
| MCU Active-Halt (AWU)      | ~13 uA       | 99.9999% of time                                |
| LDO quiescent (HT7533A-1)  | ~2.5 uA      | Always                                          |
| Voltage divider (switched) | ~0 uA        | OFF during sleep (GPIO-controlled)              |
| TM1637 display             | 0 uA         | Power cut by 2N7000 N-FET (low-side GND switch) |
| **TOTAL STANDBY**          | **~15.5 uA** |                                                 |

### Wakeup phase (once per hour, ~5 ms)

| Component            | Current  | Notes                          |
|----------------------|----------|--------------------------------|
| MCU Run @ 2 MHz      | ~1.5 mA  | ADC read + compare + sleep     |
| Voltage divider      | ~10 uA   | ON only during 5 ms reading    |
| Average contribution | 0.002 uA | 1.5mA x 5ms/3600s = negligible |

### UI mode (display ON, adjusting threshold)

| Component      | Current   | Notes                         |
|----------------|-----------|-------------------------------|
| MCU Run/Wait   | ~1.5 mA   | Active during UI interaction  |
| TM1637 display | ~20-50 mA | On for max ~5-10 seconds      |
| Frequency      | Rare      | Only when user presses button |

### Battery drain summary

```
Standby:           ~15.5 uA

Drain per month:    11.2 mAh
Drain per 6 months: 67 mAh
Drain per 1 year:   135 mAh
% of 60 Ah battery: 0.23% per year

Battery self-discharge: ~1%/month = 12%/year
Device impact: ~53x smaller than self-discharge -> negligible
```

---

## Circuit Blocks

### 1. Power Supply
```
                              +-------[10uF]--- GND  (input bypass cap)
                              |
Car 12V --> [Fuse 1A] --> [HT7533A-1 LDO (30V, 3.3V)] --> 3.3V VDD
                                                    |
                                             [10uF + 100nF]--- GND  (output bypass caps)

⚠ IMPORTANT: Both bypass capacitors are REQUIRED for stable LDO operation.
  - VIN  cap: 10uF to GND — prevents input oscillation and absorbs transients
  - VOUT cap: 10uF to GND — required for internal feedback stability
    (add 100nF in parallel with VOUT 10uF for high-frequency decoupling)
  Without output cap: LDO will oscillate and output voltage will be wrong.
  Ceramic (MLCC) or tantalum caps both work. Place as close to LDO as possible.

LDO: HT7533A-1 (Holtek) - Low-power LDO, high Vin, SOT-89
  - Vin: 2.5V .. 30V  (handles 14.5V charging + transient headroom)
  - Vout: 3.3V fixed, 100 mA max
  - Iq: ~2.5 uA (typ) - excellent for battery-powered standby
  - Dropout: ~0.35V @ 100 mA
  - Package: TO-92 (suffix -2), SOT-89 (suffix -1)
  - Temp: -40 to +85°C
  - Cheap and widely available (AliExpress, LCSC, arduino.ua)
  - Note: "-A" suffix = package variant only, NOT a different Vin rating.

Alternative (higher Vin, automotive-grade):
  - TPS7B6933-Q1 (TI), Vin up to 40V, Iq ~15 uA, AEC-Q100
    SOT-23-5, $0.39 @ 1ku - for severe load dump environments

Alternative (even lower Iq):
  - AP7375-33SA-7 (Diodes Inc), Vin up to 45V, Iq ~5 uA
    SOT-23-3, 300 mA, -40 to +125°C

VCAP pin (MCU pin 5): connect 1uF capacitor to GND (internal regulator).
```

### 2. Battery Voltage Sensing (GPIO-switched)
```
                    [P6KE16A TVS] (16V standoff, DO-15 axial)
                         |
Car 12V ---[1M]---+---[220k]--- GND
                   |
                   +--[100pF]-- GND
                   |
                   +----------- PD5/AIN5 (ADC CH5, 10-bit)

Divider: R1=1M, R2=220k
Ratio: 220k / 1.22M = 0.180

 8.0V -> 1.44V     (deeply discharged)
10.5V -> 1.89V     (disconnect threshold zone)
10.8V -> 1.95V
11.5V -> 2.07V
12.0V -> 2.16V     (nominal resting)
13.0V -> 2.34V
14.5V -> 2.62V     (max charging / engine running)
16.0V -> 2.89V     (transient - still under 3.3V)

10-bit ADC: 3.3V / 1024 = 3.22 mV/step
Battery resolution: 3.22 / 0.180 = ~17.9 mV/step (sufficient)

No zener clamp needed at the divider node: the 1MΩ series resistor
limits fault current to < 27µA even at 30V input. At this current level
the MCU's internal clamp diode (rated ±5mA) handles any overvoltage
safely. A zener here would cause ADC reading errors due to reverse
leakage through the high-impedance node (~181kΩ Thevenin resistance).
The P6KE16A TVS on the 12V rail provides primary transient protection.

Optional: switch divider high side via a spare GPIO + P-FET to
eliminate the ~7 uA constant drain. For simplicity, always-on
divider is acceptable (adds 7 uA to standby).
```

### 3. Door Switch
```
                     [1N4148] -- VDD  (anode to node, cathode to VDD — upper clamp)
                          |
PD2 (internal pull-up) ---+--- [Door Switch] --- GND
                          |
                       [100nF] -- GND
                          |
                       [1N4148] -- GND  (anode to GND, cathode to node — lower clamp)
                          |
                       [1k series] -- PD2 pin
Door closed: PD2 = HIGH    Door open: PD2 = LOW (ext. interrupt)
1k series resistor limits fault current through clamp diodes.
1N4148 pair clamps pin to GND-0.7V .. VDD+0.7V, protecting GPIO from
inductive spikes and ESD on door wiring harness.
```

### 4. Ignition Detect
```
                    [P6KE16A TVS] (16V standoff, DO-15 axial)
                         |
IGN/ACC 12V ---[100k]----+---[39k]--- GND
                         |
                         +--[100nF]-- GND
                         |
                      [BZX55C3V6 zener]--> GND  (overvoltage clamp: cathode to node, anode to GND)
                         |
                         +----------- PC4

Divider: R1=100k, R2=39k, ratio = 0.281
IGN ON  (10.5V) -> 2.94V  (reads HIGH, above Vih 2.31V)
IGN ON  (12.0V) -> 3.37V  (clamped to 3.3V by MCU clamp diode)
IGN ON  (14.5V) -> 4.07V  (clamped to 3.3V by MCU clamp diode)
IGN OFF (0V)    -> 0V      (reads LOW)

Clamp current at 15V: (15V - 3.3V) / 100kΩ = 117µA  (MCU ±5mA limit — 42× margin ✓)
Minimum detectable ignition voltage: ~6V (measured), which conveniently matches
the minimum voltage required for ECU boot (~6-6.5V). Below this voltage the
ignition line is considered OFF — preventing a false "ignition ON" reading during
deep battery discharge or during engine cranking voltage dip.

TVS absorbs load-dump transients on ignition wire.
BZX55C3V6 zener clamps divider output to ~3.6V (cathode to node, anode to GND);
100kΩ series resistance limits fault current to < 270µA even at 30V.

Note: R2 changed from 20k to 39k — original 20k divider (ratio=0.167) only
produced 2.42V at 14.5V, barely above Vih=2.31V, and failed to detect
ignition below ~14V. The 39k value ensures reliable detection from ~8.5V.
```

### 5. Buzzer (Timer PWM)
```
PC2 (TIM1_CH2 PWM) ---- [KY-006 passive buzzer, signal pin] ---- GND

Module: KY-006 passive buzzer (arduino.ua, ~14 UAH)
  - Passive electromagnetic buzzer (no built-in oscillator)
  - Resonant frequency: ~2.5 kHz
  - Driven by TIM1 PWM at ~2730 Hz (car-like warning beep)
  - For prototyping: plug module directly into breadboard
  - For final PCB: desolder buzzer element from module,
    or use a bare passive piezo disc (12-14 mm)

CPU must be in Run or Wait mode while buzzer is active.
```

### 6. Solenoid Driver

**⚠ SAFETY WARNING:** Disconnecting the battery while the alternator is running
(engine ON) can cause severe voltage spikes (40-80V+) that may damage ECUs,
sensors, and other electronics. The firmware's ignition interlock MUST prevent
solenoid activation when ignition is ON. This is critical regardless of which
wiring option is used below.

The battery disconnect switch has a latching solenoid (2 free wires).
Two wiring options are possible. **Option B (high-side) is chosen** — see
rationale below.

#### Option A: Low-Side Switch (1 transistor, simpler) — *reference only*
```
Solenoid is rewired: +12V permanently to one terminal,
MOSFET switches the other terminal to GND.

         +12V (car battery)
           |
        [Fuse]
           |
      [Solenoid coil] ---+--- [1N5408 flyback diode] ---+--- +12V
           |             |                              |
           |             +------(cathode)---------------+
           |
           +--- Drain  [IRLZ44N N-FET, TO-220]
                        Source -- GND
                        Gate ---+---[1k]--- PD6 (MCU)
                                |
                             [10k] -- GND  (pull-down: OFF during reset)

How it works:
  PD6 = LOW  -> IRLZ44N OFF -> solenoid open-circuit -> OFF
  PD6 = HIGH -> IRLZ44N ON  -> solenoid current flows -> battery DISCONNECTS

IRLZ44N: Logic-level N-channel MOSFET (TO-220)
  - Vgs(th): 1.0-2.0V (fully enhanced at Vgs=2.7V)
  - Rds(on): 0.022Ω @ Vgs=4.5V (Vgs here = 3.3V, still very low)
  - Id: 47A continuous (far exceeds solenoid needs)
  - Vds: 55V max
  - 3.3V MCU drives gate directly, no level shifter needed
  - Package: TO-220
  - Widely available, ~$0.30-0.50

Pros:
  + Single transistor, simple circuit
  + Direct 3.3V gate drive, no level shifter
  + Fewer components (no IRF4905, no extra 10k)
  + Lower BOM cost

Cons:
  - One solenoid terminal is always at +12V (needs fuse)
  - Wire short to chassis = accidental solenoid activation
    (mitigated: short wire run, fused, only disconnects battery)
  - Requires re-wiring solenoid from factory configuration
  - Manual reconnect button must be wired to GND (extra wire run
    from button location back to battery/chassis ground)
```

#### Option B: High-Side Switch (2 transistors, original wiring) — ✅ CHOSEN
```
Solenoid keeps original wiring: one terminal grounded,
P-FET switches +12V to the other terminal via N-FET level shifter.

            +12V (car battery)
                    |
                [1A Fuse]
                    |
              Source (Pin 1)
                    |
         +----------+
         |
      [IRF4905 P-FET]---- Gate -----[10k pull-up resistor]
         |                       |           |
      Drain (Pin 3)              |           +------ +12V (OFF by default)
         |                       |
    [Solenoid coil]              +-------- 2N7000 Drain
         |
        GND (return terminal)
         
      Gate (Pin 2) ---- [1k resistor] ---- To 2N7000 drain
                                          
      2N7000 N-FET (TO-92):
      ┌───────────────────┐
      │  1(D)  2(G)  3(S) │
      │   |     |     |   │
      │   |  [Gate from]  │
      │   |  [1k resistor]│
      │   |     |     |   │
      └───┼─────┼─────┼───┘
          |     |     |
          |     |    GND
          |     |
          |    PD6 (MCU output)
          |
        IRF4905 gate (pulled LOW when 2N7000 ON)

Logic flow:
  MCU Pin PD6 = LOW (0V)
    → 2N7000 gate = 0V → 2N7000 OFF (high impedance drain)
    → IRF4905 gate floats HIGH (pulled to +12V by 10k resistor)
    → IRF4905 P-FET OFF (Vgs ≈ 0V, not active)
    → Solenoid unpowered → battery CONNECTED

  MCU Pin PD6 = HIGH (3.3V)
    → 2N7000 gate = 3.3V → 2N7000 ON (drain pulls low to ~0V)
    → IRF4905 gate = 0V (pulled LOW by conducting 2N7000)
    → IRF4905 P-FET ON (Vgs = 0V - 12V = -12V, fully enhanced)
    → +12V flows through solenoid → battery DISCONNECTS

How it works:
  PD6 = LOW  -> 2N7000 OFF -> IRF4905 gate pulled to +12V -> P-FET OFF
                (solenoid unpowered, battery stays CONNECTED)
  PD6 = HIGH -> 2N7000 ON  -> IRF4905 gate pulled to GND   -> P-FET ON
                (+12V flows through solenoid, battery DISCONNECTS)

IRF4905: P-channel power MOSFET (TO-220)
  Pin 1 (Source): Connected to +12V (or through fuse to +12V)
  Pin 2 (Gate):   Gate drive input (pulled HIGH by 10k to +12V, pulled LOW by 2N7000)
  Pin 3 (Drain):  Connected to solenoid coil terminal
  
  - Vgs(th): -2.0 to -4.0V (fully enhanced at Vgs=-10V)
  - Rds(on): 0.020Ω @ Vgs=-10V (Vgs here = -12V, excellent)
  - Id: -74A continuous
  - Vds: -55V max
  - Package: TO-220
  - Widely available, ~$0.50-0.80

2N7000: N-channel MOSFET (TO-92) — level shifter only
  Pin 1 (Drain):  Connected to IRF4905 gate
  Pin 2 (Gate):   Connected through 1k resistor to PD6 (MCU output)
  Pin 3 (Source): Connected to GND
  
  - Vgs(th): 0.8-3.0V (logic-level, conducts well at Vgs=3.3V)
  - Rds(on): ~5Ω @ Vgs=4.5V (pulling IRF4905 gate to GND, ~1mA current)
  - Id: 200 mA continuous (far exceeds the gate drive current needed)
  - Vds: 60V max
  - Package: TO-92, ~$0.05-0.10

Flyback diode (1N5408):
  - Cathode (banded end) connects to +12V
  - Anode connects to solenoid coil (protects against inductive kickback)

Gate protection (IRF4905):
  - During severe load-dump transients (40-80V spikes), the P6KE16A TVS clamps
    the 12V rail to ~26V. To protect the IRF4905 gate (Vgs max ±20V) against
    worst-case gate voltage spikes, consider adding a small TVS (P6KE20A, 19V
    breakdown) from gate to +12V (~$0.05). This ensures Vgs stays within safe
    limits even if transient protection fails. Optional but recommended for
    harsh automotive environments.

Pros:
  + No live wire to solenoid when OFF (safer against shorts)
  + Keeps original solenoid wiring (one wire grounded to chassis)
  + Double-safe defaults: both pull-up and pull-down keep solenoid OFF
  + Manual button keeps original wiring: just switches +12V,
    no extra wire run needed. Button and MCU board work independently.

Cons:
  - Two transistors + extra 10k resistor
  - Slightly higher cost (+$0.60 for IRF4905, +$0.05 for 2N7000, +$0.01 for 10k)

Chosen because the manual reconnect button stays in its original
location with original wiring. With low-side (Option A), the button
would need to switch GND instead of +12V, requiring an additional
wire from the button all the way to the battery terminal / chassis
ground — impractical in most installations.
```

#### Common to both options
```
Option A uses IRLZ44N (TO-220) as the power switch.
Option B uses 2N7000 (TO-92) as the IRF4905 gate level shifter.

1N5408: Flyback diode (DO-201 axial, 3A)
  - Solenoid coils can be high-current; 1N4148 (150mA) is too weak
  - 1N5408: 1000V, 3A - handles solenoid flyback energy safely
  - Alternative: 1N5402 (200V, 3A) - sufficient for 12V system

10k pull-down on N-FET gate keeps solenoid OFF during MCU
reset, power-up, or if MCU is unpowered.

Solenoid pulse duration: typically 0.5-2 seconds.
No continuous current needed (solenoid latches mechanically).
```

### 7. Settings UI - Display (TM1637)
```
          VDD 3.3V ──────────────── VCC of TM1637 module
                                         |
                                    TM1637 GND
                                         |
                                      Drain (2N7000, TO-92)
                                      Gate <-- PC7 (HIGH = display ON, LOW = display OFF)
                                     Source
                                         |
                                        GND

      +-------+--------+
      |   TM1637       |
      |  4-digit       |
      |  7-seg module  |
      |                |
      |  CLK --- PC5   |
      |  DIO --- PC6   |
      +----------------+

2N7000: N-channel MOSFET low-side switch (TO-92, already in BOM for solenoid)
  - Switches GND rail of TM1637 instead of VCC (low-side)
  - Vgs(th): 0.8–3V (fully on at Vgs=3.3V from MCU pin)
  - Rds(on): ~5Ω @ Vgs=5V, ~7Ω @ Vgs=3.3V (negligible for TM1637 ~50mA)
  - Id: 200mA continuous
  - No extra transistor needed -- same part as solenoid level shifter (BOM #4)

PC7 HIGH -> 2N7000 ON  -> TM1637 GND connected -> display ON
PC7 LOW  -> 2N7000 OFF -> TM1637 GND floating   -> display OFF

⚠ CLK/DIO leak warning: with GND switched off, CLK and DIO remain at 3.3V
  (driven by MCU). TM1637 may phantom-power dimly through internal ESD diodes.
  Add 10kΩ pull-down resistors on CLK and DIO lines to suppress this.

Note: original design used a BS170 P-FET high-side switch. BS170 is actually
N-channel (not P-channel) -- that design was incorrect. The 2N7000 low-side
switch is the confirmed working replacement using an existing BOM component.

Display shows "XX.X" (e.g., "10.8" = 10.8V threshold).
Display is fully powered OFF during normal operation (0 uA).
```

### 8. Settings UI - Buttons
```
PC7 (internal pull-up) ---+--- [BTN_UP "+0.1V"] --- GND
                          |
                       [100nF] -- GND

PD7/TLI (internal pull-up) ---+--- [BTN_DOWN "-0.1V"] --- GND
                               |
                            [100nF] -- GND

Both buttons wake MCU from Active-Halt via external interrupt.
PD7/TLI is the Top-Level Interrupt input -- can wake from any
low-power mode.
No polling needed. Zero additional sleep current.
```

### 9. Input Protection Summary
```
All external inputs are protected for automotive environment:

Battery sense (PD5):
  - P6KE16A TVS on car 12V line (16V standoff, ~26V clamp, DO-15 axial)
  - 1MΩ series resistance limits fault current to < 27µA @ 30V
    (MCU internal clamp diode rated ±5mA — 200× margin; no zener needed)
  ⚠ No zener at divider node — zener reverse leakage through the high
    Thevenin impedance (~181kΩ) causes significant ADC reading errors.

Ignition detect (PC4):
  - P6KE16A TVS on ignition wire (16V standoff, DO-15 axial)
  - BZX55C3V6 zener clamps divider output to ~3.6V (cathode to node, anode to GND)
  - 100kΩ series resistance limits fault current to < 270µA @ 30V
  - R2 = 39k (changed from 20k): detects ignition reliably from ~8.5V up

Door switch (PD2):
  - 1N4148 pair: upper clamp to VDD, lower clamp to GND (DO-35 axial)
  - 1kΩ series resistor limits fault current through clamp diodes
  - 100nF debounce capacitor

Buttons (PC7, PD7):
  - Internal pull-up + 100nF to GND
  - No external TVS needed (buttons are inside enclosure,
    short traces, no exposure to vehicle wiring harness)

LDO input:
  - 1A fuse protects the entire board
  - HT7533A handles up to 30V input (genuine Holtek; verify supplier)
  - P6KE16A TVS on 12V rail clamps severe transients
```

---

## Firmware Architecture

```
RESET
  |
  v
INIT: CLK(HSI/8=2MHz), GPIO, ADC, TIM1(PWM), AWU(1 hour), EXTI(PD2,PC7,PD7)
      Load threshold from EEPROM
  |
  v
+---> ACTIVE-HALT (~13 uA, display OFF) <-----------------+
|         |              |               |                  |
|   AWU wakeup     Door EXTI       Button EXTI              |
|   (1 hour)       (PD2 edge)     (PC7 or PD7)             |
|         |              |               |                  |
|         v              v               v                  |
|   [Enable ADC]   [Door open?]    [Enter UI Mode]         |
|   [Read Vbatt]    Y: buzzer ON    Turn on display         |
|   [Disable ADC]   N: buzzer OFF   Show threshold "XX.X"  |
|         |              |           Wait for buttons:      |
|   Vbatt < thresh?      |            UP: thresh += 0.1V   |
|    No --+              |            DN: thresh -= 0.1V   |
|         |  Yes         |           Clamp 10.0V..13.0V    |
|         v              |           Timeout 5s no press:  |
|   [Ignition ON?]       |            Save to EEPROM       |
|    Yes: BLOCK!         |            Turn off display      |
|    No:  solenoid ON    |               |                  |
|         |              |               |                  |
+---------+--------------+---------------+------------------+
```

### Settings UI Workflow

Both buttons (BTN_UP = PD3, BTN_DOWN = PD2) share EXTI_PORTD and wake the MCU
from Active-halt. The display (TM1637) is powered off during normal monitoring.

**Entry**
- The first button press wakes the MCU, turns the display on, and shows the
  current threshold (e.g. "10.8"). No threshold change occurs on this press.
- A 5-second activity timeout starts from this point.

**Adjusting the threshold**
- BTN_UP increments by 0.1 V (ceiling 13.0 V); BTN_DOWN decrements by 0.1 V
  (floor 10.0 V).
- If both buttons are pressed simultaneously, BTN_UP wins.
- Pressing UP at max or DOWN at min blinks the display 3 times (25 ms off /
  25 ms on) to signal the boundary. No value change occurs.
- Every button press — including boundary presses — resets the 5-second timeout.

**Exit**
- When the timeout expires the display is turned off.
- The threshold is written to EEPROM only if it was changed during this session.
- `wakeup_count` is reset to zero so the next solenoid check occurs after a
  full AWU interval from the moment the UI closes.

**Solenoid interlock**
- The solenoid is not activated while the UI is active, even if battery voltage
  is below the threshold.

---

### Key Logic (pseudocode)

```c
// EEPROM storage
#define EEPROM_THRESH_ADDR  0x4000
#define THRESH_DEFAULT      121     // 12.1V default
#define THRESH_MIN          100     // 10.0V minimum
#define THRESH_MAX          130     // 13.0V maximum

uint8_t threshold_x10;  // stored as 10ths of volt (108 = 10.8V)

// Convert threshold to ADC value
// ADC = threshold_V * divider_ratio / Vref * 1024
// ADC = (threshold_x10/10) * 0.180 / 3.3 * 1024
#define THRESH_TO_ADC(t)  ((uint16_t)((t) * 0.180 / 3.3 * 1024 / 10))

void load_threshold(void) {
    threshold_x10 = *(uint8_t*)EEPROM_THRESH_ADDR;
    if (threshold_x10 < THRESH_MIN || threshold_x10 > THRESH_MAX)
        threshold_x10 = THRESH_DEFAULT;
}

void save_threshold(void) {
    FLASH_Unlock(FLASH_MEMTYPE_DATA);
    FLASH_ProgramByte(EEPROM_THRESH_ADDR, threshold_x10);
    FLASH_Lock(FLASH_MEMTYPE_DATA);
}

// --- Battery monitoring (AWU wakeup, once per hour) ---
// Note: Solenoid is latching (mechanically holds position).
// A pulse on PD6 activates the disconnect. Reconnecting
// requires manual action or a separate reconnect solenoid.
//
// Fault tolerance: normally the MCU loses power along with the battery
// once the disconnect fires. However, if the solenoid or switch malfunctions
// and the battery remains connected, the MCU will keep waking via AWU.
// Two guards prevent damage in this case:
//   1. solenoid_pulse() limits coil energisation to 1 second per attempt,
//      preventing solenoid and gate transistor overheating.
//   2. solenoid_pulse_count caps retries at SOLENOID_MAX_RETRIES (3). After
//      that the solenoid is not pulsed again until the MCU is reset (i.e.
//      the battery is eventually reconnected manually and power is cycled).
//      Retries are spaced one full AWU interval (~1 hour) apart.

#define SOLENOID_MAX_RETRIES  3

uint8_t solenoid_pulse_count = 0;  // incremented on each pulse; cleared only by reset

void solenoid_pulse(void) {
    // PD6 HIGH -> 2N7000 ON -> IRF4905 gate LOW -> P-FET ON -> +12V to solenoid
    PD_ODR |= (1 << 6);
    delay_ms(1000);                 // 1 second pulse (adjust per solenoid spec)
    PD_ODR &= ~(1 << 6);           // PD6 LOW -> solenoid OFF
    solenoid_pulse_count++;
}

void on_awu_wakeup(void) {
    uint16_t vbatt = adc_read(ADC_CH5);
    uint16_t low  = THRESH_TO_ADC(threshold_x10);

    if (vbatt < low && !ignition_on() && solenoid_pulse_count < SOLENOID_MAX_RETRIES) {
        solenoid_pulse();
    }
    // Reconnection is manual (turn the disconnect switch back)
}

// --- Door alarm (EXTI on PD2) ---

void on_door_exti(void) {
    if (door_is_open()) {
        tim1_pwm_start();
    } else {
        tim1_pwm_stop();
    }
}

// --- Settings UI (EXTI on PC7 / PD7) ---
// Called from main loop when btn_up_pressed or btn_down_pressed is set.
// Simultaneous press: UP wins (checked first).

void display_blink_boundary(void) {
    uint8_t i;
    for (i = 0; i < 3; i++) {
        display_power_off(); delay_ms(25);
        display_power_on();  delay_ms(25);
    }
}

void ui_run(void) {
    uint8_t  changed = 0;
    uint16_t timeout = 0;

    // First press: wake display, show value, do NOT change threshold.
    btn_up_pressed   = FALSE;
    btn_down_pressed = FALSE;
    display_power_on();
    display_show_threshold();

    while (timeout < 5000) {
        delay_ms(50);
        timeout += 50;

        if (btn_up_pressed || btn_down_pressed) {
            timeout = 0;                        // reset on any press

            if (btn_up_pressed) {               // UP wins if both pressed
                if (threshold_x10 < THRESH_MAX) {
                    threshold_x10++;
                    changed = 1;
                    display_show_threshold();
                } else {
                    display_blink_boundary();
                    display_show_threshold();
                }
            } else {
                if (threshold_x10 > THRESH_MIN) {
                    threshold_x10--;
                    changed = 1;
                    display_show_threshold();
                } else {
                    display_blink_boundary();
                    display_show_threshold();
                }
            }
            btn_up_pressed   = FALSE;
            btn_down_pressed = FALSE;
        }
    }

    if (changed)
        save_threshold();                       // write only if value changed
    display_power_off();
    wakeup_count = 0;                           // full AWU interval before next check
}

// --- Main ---

void main(void) {
    init_clock_hsi_div8();          // 2 MHz from 16 MHz HSI
    init_gpio();
    init_adc();
    init_tim1_pwm(2730);
    init_awu(3600);                 // 1 hour auto-wakeup
    init_exti();                    // PD2 (door), PC7 (btn_up), PD7 (btn_dn)
    load_threshold();
    display_power_off();            // ensure display is off

    while (1) {
        enter_active_halt();
    }
}
```

### TM1637 Display Driver Notes

- TM1637 uses a 2-wire protocol (not I2C, but similar bit-bang)
- Send display-on command + 3 digit segment data
- Digits map: threshold_x10=108 -> digit0=1, digit1=0, digit2=8, DP after digit1
- Display brightness: set to low level to save current
- Display off: simply cut GND via 2N7000 N-FET (no need for software off command)

---

## Bill of Materials

| #  | Component          | Part                                                          | Qty | Est. Cost   |
|----|--------------------|---------------------------------------------------------------|-----|-------------|
| 1  | MCU                | STM8S103F3P6 (TSSOP-20)                                       | 1   | 0.50        |
| 2  | LDO                | HT7533 (30V, 3.3V)                                            | 1   | 0.15        |
| 3  | Buzzer             | KY-006 passive buzzer module                                  | 1   | 0.35        |
| 4  | N-FET (solenoid)   | IRLZ44N (TO-220) *Option A* / 2N7000 (TO-92) *Option B*       | 1   | 0.40 / 0.08 |
| 5  | P-FET (solenoid)   | IRF4905 (TO-220) *Option B only*                              | 1   | 0.60        |
| 6  | N-FET (display)    | 2N7000 (TO-92) — low-side GND switch, shared part with BOM #4 | 1   | 0.08        |
| 7  | TM1637 display     | 4-digit 7-seg module, 0.36"                                   | 1   | 0.85        |
| 8  | Tactile buttons    | 6x6 mm through-hole                                           | 2   | 0.10        |
| 9  | Flyback diode      | 1N5408 (DO-201 axial, 3A)                                     | 1   | 0.10        |
| 10 | Zener diode        | BZX55C3V6 (DO-35) — IGN clamp only                            | 1   | 0.03        |
| 11 | Signal diodes      | 1N4148 (DO-35 axial) — door switch×2                          | 2   | 0.03        |
| 12 | TVS diodes         | P6KE16A (batt+ign, DO-15 axial)                               | 2   | 0.40        |
| 13 | Resistors          | 1M, 220k, 100k, 39k, 10k×2, 1k×2                              | 8   | 0.20        |
| 14 | Capacitors         | 100nF x5, 10uF, 1uF(VCAP), 100pF                              | 8   | 0.25        |
| 15 | Fuse               | 1A automotive blade                                           | 1   | 0.20        |
| 16 | PCB                | 40x30 mm, 2-layer                                             | 1   | 0.50        |
|    | **Option A total** | *(low-side, no IRF4905)*                                      |     | **~3.70**   |
|    | **Option B total** | *(high-side, with IRF4905)*                                   |     | **~4.30**   |

---

## Development Tools

- **Programmer:** ST-Link V2 (SWIM interface, pin PD1)
- **IDE:** STM8CubeMX + IAR / SDCC / Cosmic
- **Dev board (proto):** STM8S103F3P6 dev board (~82 UAH / ~2 USD)
  - Available at: arduino.ua and similar shops
  - Breadboard-friendly, includes reset button and power LED
  - Note: some boards have GND not connected on SWIM header (add jumper)
  - TM1637 module plugs directly into breadboard for prototyping
