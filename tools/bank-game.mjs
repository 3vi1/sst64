import {readFileSync, writeFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';

const sdk = process.env.MOS_BIN || '/opt/llvm-mos-sdk/bin';
const metadata = JSON.parse(execFileSync(`${sdk}/llvm-readobj`, ['--sections', '--elf-output-style=JSON', 'build/game-native.o'], {maxBuffer: 8 * 1024 * 1024}))[0];
const sizes = new Map(metadata.Sections.map(({Section: section}) => [section.Name.Name, section.Size]));
const resident = new Set(['sst64_boot', 'sst64_load_help', 'sst64_fail', 'sst64_read_byte', 'reu_store', 'reu_fetch', 'reu_detect_512k', 'transfer', 'LZ4_decompress_safe', 'LZ4_decompress_generic', 'memcpy', 'memmove', 'memset', '__memset', 'strlen', 'cbm_k_setnam', 'cbm_k_setlfs', 'cbm_k_open', 'cbm_k_chkin', 'cbm_k_clrch', 'cbm_k_close', 'cbm_k_chrin', 'cbm_k_readst', 'cbm_k_chrout', 'cbm_k_chkout', 'cbm_k_getin']);
let assembly = readFileSync('build/game-native.s', 'utf8');
const sectionStarts = [...assembly.matchAll(/^\s*\.section\s+(\.text\.[^,\s]+)[^\n]*$/gm)];
const functions = sectionStarts.map((section, index) => {
    const body = assembly.slice(section.index, sectionStarts[index + 1]?.index ?? assembly.length);
    const type = body.match(/^\s*\.type\s+([^,]+),@function/m);
    return type ? {section: section[1], name: type[1].trim(), size: sizes.get(section[1]), start: section.index} : null;
}).filter(Boolean);
resident.add('__zero_bss');
resident.add('__copy_data');
resident.add('__zero_zp_bss');
resident.add('__copy_zp_data');
for (let changed = true; changed;) {
    changed = false;
    for (const [index, func] of functions.entries()) {
        if (!resident.has(func.name)) continue;
        const body = assembly.slice(func.start, functions[index + 1]?.start ?? assembly.length);
        for (const call of body.matchAll(/^\s*(?:jsr|jmp)\s+([\w.$]+)/gm)) {
            if (functions.some(candidate => candidate.name === call[1]) && !resident.has(call[1])) {
                resident.add(call[1]);
                changed = true;
            }
        }
    }
}
const banks = [];
const consoleNames = new Set([
    'sst64_puts', 'sst64_text', 'sst64_prouts', 'sst64_output_chunk',
    'sst64_write_text', 'sst64_output_text', 'sst64_output_byte',
    'sst64_convert_ascii', 'sst64_word_break', 'sst64_start_line_indent',
    'sst64_word_color', 'sst64_flush_pending', 'sst64_more_prompt',
    'proutn', 'prout', 'prouts', 'skip', 'pause', 'write', 'read',
    'getch', 'clock', 'fgets', 'read_char', 'fill_buffer', 'putc', 'write_char',
    'flush_buffer', 'prep_write'
]);
const candidates = functions.filter(func => !resident.has(func.name) && !func.name.startsWith('__do_'));
const consoleFunctions = candidates.filter(func => consoleNames.has(func.name));
const consoleSize = consoleFunctions.reduce((sum, func) => sum + func.size, 0);
if (consoleSize > 16384 - 64) throw new Error(`Console call group exceeds one bank: ${consoleSize}`);
for (const func of [...consoleFunctions, ...candidates.filter(func => !consoleNames.has(func.name)).sort((left, right) => right.size - left.size)]) {
    if (!Number.isInteger(func.size) || func.size > 16384) throw new Error(`Function cannot fit execution window: ${func.name} (${func.size})`);
    let bank = banks.findIndex(candidate => candidate.size + func.size <= 16384 - 64);
    if (bank < 0) { bank = banks.length; banks.push({size: 0, functions: []}); }
    func.bank = bank;
    banks[bank].size += func.size;
    banks[bank].functions.push(func);
}
if (banks.length > 22) throw new Error('Game code exceeds the 352 KiB REU code allocation');
const banked = banks.flatMap(bank => bank.functions);
const escapes = value => value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
for (const func of banked) {
    assembly = assembly.replace(new RegExp(`(^\\s*\\.section\\s+)${escapes(func.section)}(?=[,\\s])`, 'm'), `$1.bank${func.bank}`);
    assembly = assembly.replace(new RegExp(`(^\\s*\\.type\\s+)${escapes(func.name)}(?=,)`, 'm'), `$1sst64_real_${func.name}`);
    assembly = assembly.replace(new RegExp(`^${escapes(func.name)}:`, 'm'), `sst64_real_${func.name}:`);
    assembly = assembly.replace(new RegExp(`(^\\s*\\.size\\s+)${escapes(func.name)},([^\\n]*)-${escapes(func.name)}(?=\\s|$)`, 'm'), `$1sst64_real_${func.name},$2-sst64_real_${func.name}`);
}
const stubs = ['.section .text.sst64_stubs,"ax",@progbits'];
for (const func of banked) {
    stubs.push(`.globl ${func.name}`, `${func.name}:`, 'php', 'pha', 'txa', 'pha', 'tya', 'pha', `lda #${func.bank}`, 'sta sst64_requested_bank', `lda #mos16lo(sst64_real_${func.name})`, 'sta sst64_target', `lda #mos16hi(sst64_real_${func.name})`, 'sta sst64_target+1', 'jmp sst64_enter');
}
assembly += '\n' + stubs.join('\n') + '\n';
assembly += '.section .init.225,"ax",@progbits\njsr sst64_boot\n';
writeFileSync('build/game-banked.s', assembly);
const regions = banks.map((_, index) => `bank${index} : ORIGIN = ${0x18000 + index * 65536}, LENGTH = 16384`).join('\n');
const sections = banks.map((_, index) => `.bank${index} : { KEEP(*(.bank${index})) } >bank${index}`).join('\n');
writeFileSync('build/game.ld', `__basic_zp_start = 2;\n__basic_zp_end = 0x90;\nMEMORY { ram (rw) : ORIGIN = 0x0801, LENGTH = 0x67ff\n${regions}\n}\nINCLUDE commodore.ld\nINPUT(unmap-basic.o)\n__stack = 0xd000;\n__stack_reserve_size = 0x5000;\nSECTIONS { ${sections} }\nOUTPUT_FORMAT { SHORT(ORIGIN(ram)) TRIM(ram) }\n`);
writeFileSync('build/game-banks.json', JSON.stringify(banks.map(bank => ({size: bank.size, functions: bank.functions.map(func => func.name)})), null, 2));
console.log(`Mapped ${banked.length} game/runtime functions into ${banks.length} REU code banks.`);