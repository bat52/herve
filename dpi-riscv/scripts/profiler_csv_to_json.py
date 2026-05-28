#!/usr/bin/env python3
"""
Convert herve profiler CSV output to hierarchical JSON.

The profiler CSV has these record types:
  META,<key>,<value>             — global metadata (arch, isa, totals)
  FUNC,<name>,total_cycles,<val> — total cycles spent in a function
  FUNC,<name>,calls,<val>        — number of times the function was entered
  INSTR,<name>,<type>,<val>      — cycles attributed to instruction type <type>
  ICOUNT,<name>,<type>,<val>     — instruction count of type <type>
  CALL,<caller>,<callee>,<cnt>   — caller to callee edge with call count

Output JSON structure:
  {
    "meta": { "arch": "...", "isa": "...", "total_cycles": N, "total_instructions": N },
    "call_edges": [
      { "caller": "<name>", "callee": "<name>", "count": N }
    ],
    "functions": {
      "<name>": {
        "total_cycles": N,
        "calls": N,
        "cycles_per_call": N,
        "instructions": {
          "<type>": { "count": N, "cycles": N }
        },
        "sub_functions": {
          "<callee>": { "calls": N },
          ...
        }
      }
    }
  }
"""

import csv
import json
import sys
import argparse
from collections import defaultdict


def parse_csv(input_file):
    """Parse profiler CSV and return (meta, call_edges, func_data) tuple.

    meta: dict with keys arch, isa, total_cycles, total_instructions
    call_edges: list of {'caller': str, 'callee': str, 'count': int}
    func_data: dict[func_name] -> {
        'total_cycles': int,
        'calls': int,
        'insn_cycles': dict[insn_type -> int],
        'insn_counts': dict[insn_type -> int],
    }
    """
    meta = {}
    call_edges = []
    func_data = defaultdict(lambda: {
        'total_cycles': 0,
        'calls': 0,
        'insn_cycles': {},
        'insn_counts': {},
    })

    reader = csv.reader(input_file)
    for row in reader:
        if not row:
            continue
        record_type = row[0].strip()

        if record_type == 'META':
            key = row[1].strip()
            value = row[2].strip()
            if key in ('total_cycles', 'total_instructions'):
                meta[key] = int(value)
            else:
                meta[key] = value

        elif record_type == 'CALL':
            caller = row[1].strip()
            callee = row[2].strip()
            count = int(row[3])
            call_edges.append({'caller': caller, 'callee': callee, 'count': count})

        elif record_type == 'FUNC':
            name = row[1].strip()
            field = row[2].strip()
            value = int(row[3])
            if field == 'total_cycles':
                func_data[name]['total_cycles'] = value
            elif field == 'calls':
                func_data[name]['calls'] = value

        elif record_type == 'INSTR':
            name = row[1].strip()
            insn_type = row[2].strip()
            cycles = int(row[3])
            func_data[name]['insn_cycles'][insn_type] = cycles

        elif record_type == 'ICOUNT':
            name = row[1].strip()
            insn_type = row[2].strip()
            count = int(row[3])
            func_data[name]['insn_counts'][insn_type] = count

    return meta, call_edges, dict(func_data)


def build_hierarchical(meta, call_edges, func_data):
    """Convert flat parsed data into the hierarchical JSON structure.

    Each function entry gains a ``sub_functions`` dict keyed by callee name,
    e.g.  ``{"uart_write": {"calls": 23}}``, populated from the
    CALL records in the CSV.
    """
    # Build caller to callee map from edges
    sub_map = defaultdict(dict)
    for edge in call_edges:
        sub_map[edge['caller']][edge['callee']] = edge['count']

    functions = {}
    for name, data in func_data.items():
        calls = data['calls']
        total_cycles = data['total_cycles']

        # Per-instruction-type breakdown
        instructions = {}
        all_types = set(data['insn_cycles'].keys()) | set(data['insn_counts'].keys())
        for insn_type in sorted(all_types):
            instructions[insn_type] = {
                'count': data['insn_counts'].get(insn_type, 0),
                'cycles': data['insn_cycles'].get(insn_type, 0),
            }

        # Sub-functions called by this function (sorted by call count desc)
        callees = sub_map.get(name, {})
        sorted_callees = dict(sorted(callees.items(),
                                     key=lambda x: x[1] if isinstance(x[1], (int, float)) else 0,
                                     reverse=True))

        func_entry = {
            'total_cycles': total_cycles,
            'calls': calls,
            'cycles_per_call': round(total_cycles / calls, 2) if calls > 0 else 0,
            'instructions': instructions,
            'sub_functions': {callee: {'calls': cnt}
                              for callee, cnt in callees.items()},
        }
        functions[name] = func_entry

    # Sort functions by total_cycles descending
    sorted_funcs = dict(sorted(functions.items(),
                               key=lambda x: x[1]['total_cycles'],
                               reverse=True))

    result = {
        'meta': meta,
        'call_edges': call_edges,
        'functions': sorted_funcs,
    }

    return result


def main():
    parser = argparse.ArgumentParser(
        description='Convert herve profiler CSV to hierarchical JSON.'
    )
    parser.add_argument(
        'input',
        nargs='?',
        default=sys.stdin,
        help='Input CSV file (default: stdin)',
    )
    parser.add_argument(
        '-o', '--output',
        default=None,
        help='Output JSON file (default: stdout)',
    )
    parser.add_argument(
        '--pretty',
        action='store_true',
        default=True,
        help='Pretty-print JSON output (default: true)',
    )
    parser.add_argument(
        '--no-pretty',
        action='store_false',
        dest='pretty',
        help='Compact JSON output',
    )

    args = parser.parse_args()

    # Open input
    if args.input is sys.stdin or args.input == '-':
        input_file = sys.stdin
    else:
        input_file = open(args.input, 'r', encoding='utf-8')

    try:
        meta, call_edges, func_data = parse_csv(input_file)
    finally:
        if input_file is not sys.stdin:
            input_file.close()

    result = build_hierarchical(meta, call_edges, func_data)

    # Write output
    indent = 2 if args.pretty else None
    output_text = json.dumps(result, indent=indent)

    if args.output:
        with open(args.output, 'w', encoding='utf-8') as f:
            f.write(output_text)
            f.write('\n')
        print(f'Wrote {args.output}', file=sys.stderr)
    else:
        sys.stdout.write(output_text)
        sys.stdout.write('\n')


if __name__ == '__main__':
    main()
