# NimBLE Bluetooth Stack Implementation Plan for Herve

## Overview
This plan outlines the implementation of the Apache Mynewt NimBLE Bluetooth 5.4 stack over the Mynewt OS running on the Herve RISC-V Instruction Set Simulator (ISS). The implementation will include necessary hardware mockups to simulate a Bluetooth radio controller.

## Goals
1. Port Mynewt OS to run on Herve ISS (building upon existing Zephyr support)
2. Implement NimBLE host and controller stack on Mynewt
3. Create hardware mockups for Bluetooth radio registers accessible via MMIO
4. Provide sample BLE applications for testing
5. Validate implementation with basic BLE advertising and connection scenarios

## Prerequisites
- Existing Herve ISS with Verilator DPI-C integration
- Mynewt-core repository (as submodule or dependency)
- NimBLE repository (as submodule or dependency)
- RISC-V toolchain for Mynewt/NimBLE development

## Implementation Phases

### Phase 1: Mynewt OS Port to Herve
1. **Analyze existing Zephyr port** in `dpi-riscv/zephyr/` for reference
2. **Create Mynewt BSP** for Herve in `mynewt/hw/bsp/herve`
3. **Implement hardware abstraction layer** (HAL) for Herve peripherals:
   - Timer/counter (using machine timer at 0x10000010)
   - GPIO interface (using MMIO at 0x10000000-0x1000ffff)
   - Console/HTIF (already implemented at 0x80001000)
4. **Port Mynewt kernel** to Herve architecture
5. **Create default blinky application** to verify basic functionality

### Phase 2: Bluetooth Radio HW Mockup
1. **Design Bluetooth register map** compatible with NimBLE controller expectations:
   - Based on Nordic nRF52832 Bluetooth Low Energy radio registers
   - Key registers: FREQUENCY, DATAWHITEIV, PACKETPTR, TXADDRESS, RXADDRESS, etc.
   - Event registers for RX/TX ready, address received, etc.
   - Interrupt configuration registers
2. **Implement MMIO mockup** in Herve ISS:
   - Extend existing MMIO handling in `rv32_dpi.c`
   - Add Bluetooth controller register space (e.g., 0x10010000-0x1001ffff)
   - Implement register read/write handlers that simulate radio behavior
   - Add radio state machine simulation (idle, RX, TX, etc.)
3. **Create Verilog testbench** for Bluetooth radio verification:
   - Simple testbench to exercise register interface
   - Verify timing and basic TX/RX functionality

### Phase 3: NimBLE Stack Integration
1. **Port NimBLE to Mynewt on Herve**:
   - Adapt NimBLE porting layer (NPL) to Mynewt OS primitives
   - Configure NimBLE for Herve hardware (clock speed, RAM, etc.)
   - Implement HCI transport layer (using RAM transport for host/controller on same CPU)
2. **Integrate Bluetooth radio driver**:
   - Create NimBLE radio driver that talks to our HW mockup
   - Implement radio timing, packet assembly/disassembly
   - Handle interrupts and events from radio mockup
3. **Configure NimBLE stack**:
   - Set up GAP (Generic Access Profile) for advertising/scanning
   - Set up GATT (Generic Attribute Profile) services
   - Configure security manager if needed
4. **Build sample BLE applications**:
   - Beacon transmitter (advertising only)
   - BLE UART service (similar to existing bleuart)
   - Heart rate monitor or other standard profile

### Phase 4: Testing and Validation
1. **Unit tests** for hardware mockup
2. **Integration tests** for Mynewt + NimBLE stack
3. **BLE protocol validation**:
   - Advertising packet transmission/reception
   - Connection establishment and parameter update
   - Basic GATT service discovery and characteristic access
4. **Performance analysis** using Herve's profiling capabilities

## Detailed Implementation Steps

### Phase 1 Details

#### 1.1 Mynewt BSP Creation
- Directory structure: `mynewt/hw/bsp/herve/`
- Files to create:
  - `bsp.h`: BSP configuration
  - `bsp.c`: BSP initialization
  - `hal_bsp.c`: HAL board-specific functions
  - `syscfg.yaml`: System configuration defaults
  - `herve.ld`: Linker script

#### 1.2 HAL Implementation
- Implement `hal_timer.c` using machine timer
- Implement `hal_gpio.c` using existing GPIO MMIO registers
- Implement `hal_flash.c` (may be stubbed initially as Herve has no flash)
- Implement `hal_uart.c` for HTIF console

#### 1.3 Kernel Port
- Verify RISC-V architecture support in Mynewt kernel
- Configure tickless idle if supported
- Set up interrupt handling

### Phase 2 Details

#### 2.1 Bluetooth Register Map
Based on nRF52832 reference, key registers to implement:
```
0x10010000: TASKS_TXEN        (Write-only)  Enable radio transmitter
0x10010004: TASKS_RXEN        (Write-only)  Enable radio receiver
0x10010008: TASKS_START       (Write-only)  Start radio
0x1001000C: TASKS_STOP        (Write-only)  Stop radio
0x10010010: TASKS_DISABLE     (Write-only)  Disable radio
0x10010014: TASKS_RSSISTART   (Write-only)  Start RSSI measurement
0x10010018: TASKS_RSSISTOP    (Write-only)  Stop RSSI measurement
0x1001001C: TASKS_BCSTART     (Write-only)  Start bit counter
0x10010020: TASKS_BCSTOP      (Write-only)  Stop bit counter
0x10010024: TASKS_EDSTART     (Write-only)  Start energy detect
0x10010028: TASKS_EDSTOP      (Write-only)  Stop energy detect
0x1001002C: TASKS_CCASTART    (Write-only)  Clear channel assess start
0x10010030: TASKS_CCASTOP     (Write-only)  Clear channel assess stop
0x10010034: TASKS_RAMPUP      (Write-only)  TX ramp up
0x10010038: TASKS_RAMPDOWN    (Write-only)  TX ramp down
0x1001003C: TASKS_TXRU        (Write-only)  TX ready upload
0x10010040: TASKS_TXRDY       (Write-only)  TX ready
0x10010044: TASKS_TXIDLE      (Write-only)  TX idle
0x10010048: TASKS_RXRU        (Write-only)  RX ready upload
0x1001004C: TASKS_RXRDY       (Write-only)  RX ready
0x10010050: TASKS_RXIDLE      (Write-only)  RX idle
0x10010054: TASKS_CRCOK       (Write-only)  Packet received with CRC OK
0x10010058: TASKS_CRCERR      (Write-only)  Packet received with CRC error
0x1001005C: TASKS_FRAMESTART  (Write-only)  First byte received
0x10010060: TASKS_EDEND       (Write-only)  Energy detect measurement ended
0x10010064: TASKS_ADDR        (Write-only)  Address received
0x10010068: TASKS_PAYLOAD     (Write-only)  Packet payload received
0x1001006C: TASKS_BUSY        (Write-only)  Radio busy
0x10010070: TASKS_CCAIDLE     (Write-only)  Clear channel idle
0x10010074: TASKS_CCBUSY      (Write-only)  Clear channel busy
0x10010078: TASKS_CCASTOPPED  (Write-only)  CCA stopped
0x1001007C: TASKS_RATEBOOST   (Write-only)  Data rate boost
0x10010080: TASKS_TXPOWERUP   (Write-only)  TX power up
0x10010084: TASKS_TXPOWERDOWN (Write-only)  TX power down
0x10010088: EVENTS_READY      (Read/write)  Radio ready event
0x1001008C: EVENTS_ADDRESS    (Read/write)  Address sent/received event
0x10010090: EVENTS_PAYLOAD    (Read/write)  Payload sent/received event
0x10010094: EVENTS_END        (Read/write)  Packet sent/received event
0x10010098: EVENTS_DISABLED   (Read/write)  Radio disabled event
0x1001009C: EVENTS_DEVMATCH   (Read/write)  Device address match event
0x100100A0: EVENTS_DEVMISS    (Read/write)  Device address miss event
0x100100A4: EVENTS_RSSIEND    (Read/write)  RSSI sampling ended
0x100100A8: EVENTS_BCMATCH    (Read/write)  Bit counter match
0x100100AC: EVENTS_CRCOK      (Read/write)  Packet received with CRC OK
0x100100B0: EVENTS_CRCERR     (Read/write)  Packet received with CRC error
0x100100B4: EVENTS_FRAMESTART (Read/write)  First byte received
0x100100B8: EVENTS_EDEND      (Read/write)  Energy detect measurement ended
0x100100BC: EVENTS_ADDR       (Read/write)  Address received event
0x100100C0: EVENTS_PAYLOAD    (Read/write)  Payload received event
0x100100C4: EVENTS_TXSTART    (Read/write)  Transmit started
0x100100C8: EVENTS_TXSTOP     (Read/write)  Transmit stopped
0x100100CC: EVENTS_TXLAST     (Read/write)  Last byte transmitted
0x100100D0: EVENTS_RXSTART    (Read/write)  Receive started
0x100100D4: EVENTS_RXSTOP     (Read/write)  Receive stopped
0x100100D8: EVENTS_RXTO       (Read/write)  Receiver timeout
0x100100DC: SHORTS            (Read/write)  Shortcut between events and tasks
0x100100E0: INTENSET          (Read/write)  Interrupt enable set
0x100100E4: INTENCLR          (Read/write)  Interrupt enable clear
0x100100E8: BREAKPT           (Read/write)  Breakpoint register
0x100100EC: TEST              (Read/write)  Test register
0x100100F0: FREQUENCY         (Read/write)  Frequency channel
0x100100F4: TXPOWER           (Read/write)  Output power
0x100100F8: MODE              (Read/write)  Data rate and modulation
0x100100FC: PCNF0             (Read/write)  Packet configuration 0
0x10010100: PCNF1             (Read/write)  Packet configuration 1
0x10010104: BASE0             (Read/write)  Base address 0
0x10010108: BASE1             (Read/write)  Base address 1
0x1001010C: PREFIX0           (Read/write)  Prefixes byte 0-3 for address 0
0x10010110: PREFIX1           (Read/write)  Prefixes byte 0-3 for address 1
0x10010114: TXADDRESS         (Read/write)  Transmit address select
0x10010118: RXADDRESSES       (Read/write)  Receive address select
0x1001011C: CRCCNF            (Read/write)  CRC configuration
0x10010120: CRCPOLY           (Read/write)  CRC polynomial
0x10010124: CRCINIT           (Read/write)  CRC initial value
0x10010128: TIFS              (Read/write)  Interframe spacing
0x1001012C: RSSISAMPLE        (Read-only)   RSSI sample
0x10010130: STATE             (Read-only)   Current radio state
0x10010134: DATAWHITEIV       (Read/write)  Data whitening IV initial value
```

#### 2.2 MMIO Implementation Changes
Modify `rv32_dpi.c` to:
1. Add Bluetooth register space definition
2. Implement Bluetooth register read/write handlers
3. Add radio state simulation
4. Generate appropriate interrupts/events

### Phase 3 Details

#### 3.1 NimBLE Porting Layer (NPL) Adaptation
- Map Mynewt OS primitives to NimBLE NPL requirements:
  - Event queues and events
  - Memory buffers (os_mbuf)
  - Timers and clock functions
  - Interrupt handling
  - Semaphores and mutexes
- Implement NPL functions in `porting/mynewt/` directory

#### 3.2 Radio Driver Implementation
Create `nimble/host/controllers/transport/ram/` adapted for Herve:
- Implement `ble_hci_ram.c` for host-to-controller communication
- Create Bluetooth radio driver that interfaces with our MMIO mockup
- Implement packet assembly/disassembly according to BLE spec
- Handle timing-critical operations (IFS, etc.)

#### 3.3 Stack Configuration
- Configure NimBLE for Herve's resources (RAM, clock speed)
- Set up default GAP roles (peripheral, central, broadcaster, observer)
- Configure ATT/GATT services if needed
- Set up security manager (just works mode initially)

## Hardware Mockup Details

### Register Access via MMIO
The Herve ISS already supports MMIO at 0x10000000-0x100fffff. We'll allocate:
- 0x10000000-0x1000ffff: Existing GPIO registers
- 0x10010000-0x1001ffff: Bluetooth radio controller registers
- 0x10020000-0x1002ffff: Bluetooth controller RAM (packet buffer, etc.)

### Radio State Machine Simulation
The mockup will simulate a simplified BLE radio:
- States: IDLE, TX, RX, SETUP
- Basic packet transmission/reception
- CRC computation/verification
- Whitening/de-whitening
- Address matching
- Simple timing model (not cycle-accurate but protocol-correct)

### Integration with Herve ISS
1. Extend `rv32_dpi.c` with Bluetooth register handling
2. Add Bluetooth state tracking variables
3. Implement radio timing simulation (using instruction counts)
4. Generate MMIO interrupts to Herve CPU via existing interrupt mechanism

## Testing Approach

### Unit Tests
- Register read/write tests
- State machine tests
- Packet encoding/decoding tests

### Integration Tests
- Mynewt boot and basic peripheral tests
- NimBLE stack initialization
- HCI communication tests

### BLE Functional Tests
1. **Advertising Test**:
   - Configure device in advertising mode
   - Transmit advertising packets
   - Verify packets can be captured (by mockup or external sniffer)

2. **Scanning Test**:
   - Configure device in scanning mode
   - Detect advertising packets from another device
   - Report received advertisements

3. **Connection Test**:
   - Establish connection between two Herve instances
   - Exchange data over GATT
   - Test connection parameter updates

4. **Profile Test**:
   - Implement Heart Rate Service
   - Notify measurements
   - Read characteristics

## Dependencies and Setup

### Required Repositories
1. Herve (current repository)
2. Mynewt-core: https://github.com/apache/mynewt-core
3. NimBLE: https://github.com/apache/mynewt-nimble

### Build Setup
1. Install RISC-V toolchain (riscv64-unknown-elf-gcc)
2. Install Mynewt newt tool
3. Clone repositories
4. Create Mynewt project targeting Herve BSP
5. Include NimBLE as a dependency
6. Build and run applications

### Development Workflow
1. Develop and test hardware mockup in isolation
2. Port Mynewt BSP and validate with blinky
3. Integrate NimBLE stack
4. Develop sample applications
5. Test with Bluetooth protocol analyzer or second Herve instance

## Risks and Mitigations

### Risk 1: Timing Issues
BLE has strict timing requirements (150μs T_IFS, etc.)
- Mitigation: Use instruction-count based timing in mockup
- Mitigation: Allow some timing flexibility in initial implementation

### Risk 2: Resource Constraints
Herve has limited RAM compared to typical BLE chips
- Mitigation: Start with minimal NimBLE configuration
- Mitigation: Use external RAM if needed via mmap

### Risk 3: Radio Simulation Accuracy
Simple mockup may not capture all radio nuances
- Mitigation: Focus on protocol correctness over electrical accuracy
- Mitigation: Add features incrementally based on test requirements

## Success Criteria
1. Mynewt OS boots successfully on Herve ISS
2. NimBLE stack initializes without errors
3. Device can transmit BLE advertising packets
4. Device can scan and receive BLE advertising packets
5. Device can establish BLE connection and exchange data
6. Sample applications (beacon, UART) work correctly
7. Basic profiling shows reasonable performance

## Future Enhancements
1. Add hardware encryption (AES) mockup for LE Secure Connections
2. Implement coexistence with other radios (if modeling multiple)
3. Add power consumption estimation
4. Support for Bluetooth Mesh
5. Long Range (Coded PHY) and 2Mbps modes
6. Direction Finding (AoA/AoD) support

## References
1. Herve ISS Documentation: Existing README and docs/
2. Mynewt OS Documentation: https://mynewt.apache.org/latest/tutorials/
3. NimBLE Documentation: https://mynewt.apache.org/latest/nimble/
4. Nordic nRF52832 Product Specification (for register reference)
5. Bluetooth Core Specification v5.4

## Effort Estimation
Implementation effort is estimated at approximately 67,500 LLM tokens distributed across phases:
- Phase 1 (Mynewt OS Port): 18,000 tokens
- Phase 2 (Bluetooth Radio HW Mockup): 15,000 tokens
- Phase 3 (NimBLE Stack Integration): 22,500 tokens
- Phase 4 (Testing and Validation): 12,000 tokens