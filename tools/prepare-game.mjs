import {readFileSync, writeFileSync, mkdirSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';

const compiler = process.env.MOS_CC || '/opt/llvm-mos-sdk/bin/mos-c64-clang';
const modules = ['ai', 'battle', 'events', 'finish', 'moving', 'planets', 'reports', 'setup', 'sst'];
mkdirSync('build/game-sources', {recursive: true});
const messages = [];
const offsets = [];
let length = 0;
function decode(value) {
    const escapes = {n: '\n', r: '\r', t: '\t', b: '\b', f: '\f', v: '\v', a: '\x07'};
    return value.slice(1, -1).replace(/\\(x[0-9a-fA-F]+|[0-7]{1,3}|.)/g, (_, code) =>
        code[0] === 'x' ? String.fromCharCode(parseInt(code.slice(1), 16)) :
        /^[0-7]/.test(code) ? String.fromCharCode(parseInt(code, 8)) : escapes[code] ?? code);
}
function unwrap(node) {
    while (node?.inner?.length === 1 && node.kind !== 'DeclRefExpr' && node.kind !== 'StringLiteral') node = node.inner[0];
    return node;
}
for (const module of modules) {
    const file = `src/${module}.c`;
    let source = readFileSync(file).toString('latin1');
    const ast = JSON.parse(execFileSync(compiler, ['-std=gnu99', '-DSST64_C64', '-fsyntax-only', '-Xclang', '-ast-dump=json', file], {maxBuffer: 32 * 1024 * 1024}));
    const replacements = [];
    function visit(node) {
        if (node.kind === 'CallExpr') {
            const callee = unwrap(node.inner?.[0])?.referencedDecl?.name;
            const argument = unwrap(node.inner?.[1]);
            const mode = {proutn: 0, prout: 1, prouts: 2}[callee];
            if (mode !== undefined && argument?.kind === 'StringLiteral' && node.range.begin.offset !== undefined) {
                const text = Buffer.from(decode(argument.value), 'latin1');
                const message = offsets.length;
                offsets.push(length);
                messages.push(text, Buffer.from([0]));
                length += text.length + 1;
                replacements.push({start: node.range.begin.offset, end: node.range.end.offset + node.range.end.tokLen, text: `sst64_text(${message}, ${mode})`});
            }
        }
        for (const child of node.inner || []) visit(child);
    }
    visit(ast);
    for (const replacement of replacements.sort((left, right) => right.start - left.start)) {
        source = source.slice(0, replacement.start) + replacement.text + source.slice(replacement.end);
    }
    writeFileSync(`build/game-sources/${module}.c`, source, 'latin1');
}
if (length > 32768) throw new Error('Narratives exceed the reserved 32 KiB REU region');
writeFileSync('build/game-text.bin', Buffer.concat(messages));
const help = readFileSync('data/sst.doc');
const helpText = help.toString('latin1');
const commandTable = readFileSync('src/sst.c', 'latin1').match(/static char \*commands\[\] = \{([\s\S]*?)\};/);
if (!commandTable) throw new Error('Cannot find command table for help index');
const helpSections = [...commandTable[1].matchAll(/"([a-z]+)"/g)].map(([_, command], index) => {
    const marker = index === 23 ? ' ABBREV' : `  Mnemonic:  ${command.toUpperCase()}`;
    const start = helpText.indexOf(`\n${marker}`) + 1;
    if (!start) return {command, offset: 0, length: 0};
    const terminator = helpText.indexOf('******', start);
    const end = helpText.indexOf('\n', terminator) + 1;
    if (terminator < 0 || !end || end - start > 65535)
        throw new Error(`Missing or oversized help section: ${command}`);
    return {command, offset: start, length: end - start};
});
if (helpSections.length > 64 || help.length > 524240 - 425984)
    throw new Error('Help exceeds reserved REU layout');
writeFileSync('build/game-help.json', JSON.stringify(helpSections));
const digest = createHash('sha256');
for (const file of [...modules.map(module => `src/${module}.c`), 'src/sst.h', 'src/c64/game_runtime.c', 'src/c64/game_platform.c', 'src/c64/game_platform.h', 'src/c64/game_math.c', 'src/c64/game_math.h', 'src/c64/game_banking.s', 'src/c64/game_boot.s', 'tools/bank-game.mjs', 'tools/prepare-game.mjs', 'tools/package-game.mjs', 'data/sst.doc', 'Makefile']) digest.update(readFileSync(file));
const tag = Buffer.concat([Buffer.from('SSTG'), digest.digest().subarray(0, 4)]);
writeFileSync('build/game-tag.bin', tag);
writeFileSync('build/game-text.h', `#define SST64_HELP_LENGTH ${help.length}UL\n#define SST64_MESSAGE_COUNT ${offsets.length}\n#define SST64_HELP_COUNT ${helpSections.length}\nstatic const uint8_t build_tag[] = {${[...tag].join(',')}};\nstatic const uint16_t text_offsets[] = {${offsets.join(',')}};\nstatic const uint32_t help_offsets[] = {${helpSections.map(section => `${section.offset}UL`).join(',')}};\nstatic const uint16_t help_lengths[] = {${helpSections.map(section => section.length).join(',')}};\n`);
console.log(`Externalized ${offsets.length} original messages, ${length} bytes; gameplay and double arithmetic unchanged.`);