# Herve Profiling Subsystem + CLI

## Purpose
Extend Herve into:
- standalone RISC-V ELF runner (Spike-like)
- optional profiling system (runtime enabled)
- CSV output profiling format
- architecture models via C headers

---

## Run example

herve run firmware.elf --arch=ibex_small --profile --out=profile.csv

---

## Key design rules

- zero overhead when profiling disabled
- no recompilation required for profiling
- execution split into fast/profiled paths
- architecture defined via C headers

---

## Architecture model

Each CPU is a header defining cycle costs:

- SERV
- PicoRV32
- Ibex
- VexRiscv
- CVA6
- BOOM
- XiangShan

---

## Output format (CSV)

META,arch,ibex_small  
FUNC,main,total_cycles,120000  
INSTR,matmul_q4,MUL,320000  

---

## Core idea

ELF → execution loop → optional profiler → CSV

# CLI

herve run <elf> [options]

Options:
--arch=<name>
--profile
--no-profile
--out=<file>
--max-cycles=<n>

---

Profiling enabled at runtime only.
No rebuild required.

---

# CSV-to-JSON post-processing

Flat CSV output can be converted into a hierarchical JSON document for
analysis, visualisation, or integration with tooling.

Script: ``dpi-riscv/scripts/profiler_csv_to_json.py``

The tool:
- parses all record types (META, FUNC, INSTR, ICOUNT, CALL)
- groups instruction data per function into an ``instructions`` map
- groups CALL edges into ``sub_functions`` per caller
- enriches each function with ``cycles_per_call``
- sorts functions by ``total_cycles`` descending

Usage::

    herve run firmware.elf --arch=ibex_small --profile --out=profile.csv
    python3 dpi-riscv/scripts/profiler_csv_to_json.py profile.csv -o profile.json

Reads from stdin when no filename is given, making it suitable for piping::

    herve run ... --profile --out=- | python3 dpi-riscv/scripts/profiler_csv_to_json.py > profile.json

## JSON output structure

.. code-block:: text

    {
      "meta": {
        "arch": "ibex_small",
        "isa": "rv32im",
        "total_cycles": 120000,
        "total_instructions": 420000
      },
      "call_edges": [
        { "caller": "main", "callee": "uart_write", "count": 42 }
      ],
      "functions": {
        "matmul_q4": {
          "total_cycles": 800000,
          "calls": 10,
          "cycles_per_call": 80000.0,
          "instructions": {
            "MUL":  { "count": 20000, "cycles": 320000 },
            "LOAD": { "count": 102500, "cycles": 307500 }
          },
          "sub_functions": {
            "memcpy": { "calls": 5 }
          }
        }
      }
    }

## JSON visualization tool

An HTML-based visualizer that consumes the hierarchical JSON output and
renders interactive plots using **three.js**.

Script: ``dpi-riscv/scripts/profiler_visualizer.html``

Pipeline::

    profile.csv  →  profiler_csv_to_json.py  →  profile.json  →  profiler_visualizer.html

Usage::

    herve run firmware.elf --arch=ibex_small --profile --out=profile.csv
    python3 dpi-riscv/scripts/profiler_csv_to_json.py profile.csv -o profile.json
    xdg-open dpi-riscv/scripts/profiler_visualizer.html   # then click "Load JSON"

### Visualizations

| Plot type | Description |
|-----------|-------------|
| Ice cycle | Clock-cycle breakdown across function hierarchy rendered as 3D sunburst chart (three.js) |
| Pie chart | Proportional cycle distribution by function top-level children |
| Flamegraph | Hierarchical call-stack flame chart with instruction breakdown |

### Controls

- **Load JSON** – file picker to load the hierarchical JSON output
- **Root selector** – dropdown to choose which function sits at the root of the hierarchy
- **Depth slider** – limit the number of hierarchy levels shown in the plots
- **Data labels** – each visual element displays absolute cycle count and percentage of total execution time

### Call-hierarchy tab

A dedicated tab presents the full function call tree:

- Top-level roots are functions with no incoming call edges (or all functions if no CALL records exist)
- Leaves are ordered from **largest** to **smallest** total time
- Color coding: **red** (hot / high cycles) → **green** (cool / low cycles)
- Each node shows function name, cycle count, percentage of total, and percentage of parent

# Architecture Models

Each architecture is a C header.

Example:

#define HERVE_ARCH_NAME "ibex_small"
#define HERVE_PIPELINE_STAGES 2

static const uint8_t cycles[] = {
  [ADD] = 1,
  [LOAD] = 3,
  [MUL] = 3
};

---

Targets:

- SERV (bit serial)
- PicoRV32 (minimal FPGA core)
- Ibex (MCU class)
- VexRiscv (configurable)
- CVA6 (Linux capable)
- BOOM (OoO)
- XiangShan (high perf OoO)

META,arch,ibex_small
META,isa,rv32im

FUNC,main,total_cycles,120000
FUNC,main,calls,1

INSTR,matmul_q4,MUL,320000
ICOUNT,matmul_q4,LOAD,102500
CALL,main,uart_write,42
