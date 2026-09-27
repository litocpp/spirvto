#!/usr/bin/env python3
"""Generate SPIR-V declarations and operand schemas from the pinned Khronos grammar."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PIN = json.loads(Path(__file__).with_name('registry.json').read_text())
NOTICE = '// Generated from Khronos SPIR-V JSON grammar; do not edit.\n// clang-format off\n'


def generate(upstream):
    source = upstream / PIN['grammar']
    if hashlib.sha256(source.read_bytes()).hexdigest() != PIN['sha256']:
        raise ValueError('upstream grammar checksum mismatch')
    grammar = json.loads(source.read_text())
    kinds = grammar['operand_kinds']
    indices = {kind['kind']: index for index, kind in enumerate(kinds)}
    module = [NOTICE, '// Copyright 2014-2024 The Khronos Group Inc. SPDX-License-Identifier: MIT\n',
              'export module spirvto:spirv;\n\nexport namespace spirvto::spv {\n',
              f'inline constexpr unsigned MagicNumber = {grammar["magic_number"]}u;\n',
              f'inline constexpr unsigned Version = {(grammar["major_version"] << 16) | (grammar["minor_version"] << 8)}u;\n']
    def enum(name, entries):
        module.append(f'enum class {name} : unsigned {{\n')
        used = set()
        for label, value, aliases in entries:
            for alias in [label, *aliases]:
                if alias[0].isdigit():
                    alias = '_' + alias
                if alias in used:
                    continue
                used.add(alias)
                module.append(f'    {alias} = {value},\n')
        module.append('};\n')
    enum('Op', [(i['opname'][2:], i['opcode'], [a[2:] for a in i.get('aliases', [])])
                for i in grammar['instructions']])
    for kind in kinds:
        if kind['category'] in ('ValueEnum', 'BitEnum'):
            enum(kind['kind'], [(e['enumerant'], e['value'], e.get('aliases', []))
                                for e in kind['enumerants']])
    module.append('}\n')
    (ROOT / 'src/spirv.cppm').write_text(''.join(module))

    operands, enumerants, instructions = [], [], []
    def sequence(items):
        offset = len(operands)
        for item in items:
            operands.append((indices[item['kind']], {'?': 1, '*': 2}.get(item.get('quantifier'), 0)))
        return offset, len(items)
    kind_rows = []
    categories = {'Id': 0, 'Literal': 1, 'ValueEnum': 2, 'BitEnum': 3, 'Composite': 4}
    for kind in kinds:
        start = len(enumerants)
        for entry in kind.get('enumerants', []):
            value = entry['value']
            value = int(value, 0) if isinstance(value, str) else value
            enumerants.append((value, *sequence(entry.get('parameters', []))))
        base, count = sequence([{'kind': k} for k in kind.get('bases', [])])
        special = {'LiteralString': 1, 'LiteralContextDependentNumber': 2, 'IdResult': 3,
                   'IdResultType': 4}.get(kind['kind'], 0)
        kind_rows.append((categories[kind['category']], special, start, len(enumerants)-start, base, count))
    seen = set()
    for inst in sorted(grammar['instructions'], key=lambda i: i['opcode']):
        if inst['opcode'] in seen:
            continue
        seen.add(inst['opcode'])
        instructions.append((inst['opcode'], *sequence(inst.get('operands', []))))
    schema = [NOTICE, '// Copyright 2014-2024 The Khronos Group Inc. SPDX-License-Identifier: MIT\n', 'export module spirvto:grammar;\nexport namespace spirvto::grammar {\n',
              'struct Operand { unsigned kind, quantifier; };\n',
              'struct Enumerant { unsigned value, offset, count; };\n',
              'struct Kind { unsigned category, special, offset, count, base, bases; };\n',
              'struct Instruction { unsigned opcode, offset, count; };\n']
    for typename, name, rows in [('Operand', 'operands', operands), ('Enumerant', 'enumerants', enumerants),
                                  ('Kind', 'kinds', kind_rows), ('Instruction', 'instructions', instructions)]:
        schema.append(f'inline constexpr {typename} {name}[] = {{\n')
        schema.extend('    {' + ', '.join(f'{v}u' for v in row) + '},\n' for row in rows)
        schema.append('};\n')
    schema.append('}\n')
    (ROOT / 'src/grammar.cppm').write_text(''.join(schema))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', type=Path)
    args = parser.parse_args()
    if args.upstream:
        generate(args.upstream)
    else:
        with tempfile.TemporaryDirectory(prefix='spirvto-') as temporary:
            path = Path(temporary)
            subprocess.run(['git', 'init', '-q', str(path)], check=True)
            subprocess.run(['git', '-C', str(path), 'fetch', '--depth=1', PIN['repository'], PIN['commit']], check=True)
            subprocess.run(['git', '-C', str(path), 'checkout', '-q', 'FETCH_HEAD'], check=True)
            generate(path)


if __name__ == '__main__':
    main()
