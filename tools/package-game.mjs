import {readFileSync, writeFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {relative, resolve} from 'node:path';
const sdk = process.env.MOS_BIN || '/opt/llvm-mos-sdk/bin';
const banks = JSON.parse(readFileSync('build/game-banks.json'));
const records = [];
const image = Buffer.alloc(524288);
function append(bank, bytes) {
    bytes.copy(image, bank * 16384);
    for (let index = 0; index < 4; ++index) appendHalf(bank * 4 + index, bytes.subarray(index * 4096, (index + 1) * 4096));
}
function appendHalf(bank, bytes) {
    const compressed = compress(bytes);
    const record = Buffer.alloc(3);
    record[0] = bank;
    record.writeUInt16LE(compressed.length, 1);
    records.push(Buffer.concat([record, compressed]));
}
function compress(bytes) {
    const input = Buffer.alloc(4096);
    bytes.copy(input);
    writeFileSync('build/compress-block.bin', input);
    execFileSync('lz4', ['-9', '-B4', '--no-frame-crc', '-f', 'build/compress-block.bin', 'build/compress-block.lz4'], {stdio: 'ignore'});
    const frame = readFileSync('build/compress-block.lz4');
    const headerSize = 7 + ((frame[4] & 8) ? 8 : 0) + ((frame[4] & 1) ? 4 : 0);
    const blockSize = frame.readUInt32LE(headerSize);
    if (blockSize & 0x80000000 || !blockSize || blockSize > 4096)
        throw new Error(`Resource cannot fit compressed buffer (${blockSize})`);
    return frame.subarray(headerSize + 4, headerSize + 4 + blockSize);
}
if (banks.length > 22) throw new Error('Code overlaps REU resident-core cache');
for (let index = 0; index < banks.length; ++index) {
    execFileSync(`${sdk}/llvm-objcopy`, ['--dump-section', `.bank${index}=build/bank${index}.bin`, 'build/sst64.prg.elf']);
    append(index, readFileSync(`build/bank${index}.bin`));
}
const text = readFileSync('build/game-text.bin');
for (let index = 0; index < 2; ++index) append(24 + index, text.subarray(index * 16384, (index + 1) * 16384));
const help = readFileSync('data/sst.doc');
help.copy(image, 425984);
const tag = readFileSync('build/game-tag.bin');
const helpFiles = [];
for (const [command, section] of JSON.parse(readFileSync('build/game-help.json')).entries()) {
    if (!section.length) continue;
    const header = Buffer.alloc(18);
    header.write('SSH1', 2, 'ascii');
    tag.copy(header, 6);
    header[14] = command;
    header.writeUInt16LE(section.length, 15);
    header[17] = Math.ceil(section.length / 4096);
    const blocks = [];
    for (let offset = 0; offset < section.length; offset += 4096) {
        const compressed = compress(help.subarray(section.offset + offset, section.offset + Math.min(offset + 4096, section.length)));
        const size = Buffer.alloc(2);
        size.writeUInt16LE(compressed.length);
        blocks.push(size, compressed);
    }
    const name = `help${String(command).padStart(2, '0')}`;
    writeFileSync(`build/${name}.prg`, Buffer.concat([header, ...blocks]));
    helpFiles.push(`  - {src: ${name}.prg, name: ${name}}`);
    image[524240 + (command >> 3)] |= 1 << (command & 7);
}
const dataManifest = readFileSync('sst64-data.disk.yaml', 'utf8')
    .replace(/src: ([^,}]+)/g, (_, source) => `src: ${JSON.stringify(relative('build', resolve(source.trim())))}`);
writeFileSync('build/sst64-data.disk.yaml', dataManifest.trimEnd() + '\n' + helpFiles.join('\n') + '\n');
const core = readFileSync('build/sst64.prg');
const entry = core.subarray(2, 14).toString('latin1').match(/\x9e(\d+)\0/);
if (core.readUInt16LE() !== 0x0801 || core.length - 2 > 32768 || !entry)
    throw new Error('Unexpected resident core layout');
core.subarray(2).copy(image, 22 * 16384);
writeFileSync('build/game-boot.inc', `.set SST64_CORE_LENGTH, ${core.length - 2}\n.set SST64_CORE_ENTRY, ${Number(entry[1])}\n`);
let split = 0;
let firstSize = 12;
while (split < records.length && firstSize + records[split].length <= 130000) firstSize += records[split++].length;
for (const [index, part] of [records.slice(0, split), records.slice(split)].entries()) {
    const header = Buffer.alloc(12);
    header.write('SST1', 2, 'ascii');
    header[6] = part.length;
    header[7] = index === 0 ? 1 : 0;
    header.writeUInt32LE(help.length, 8);
    writeFileSync(`build/game-data${index ? '2' : ''}.prg`, Buffer.concat([header, ...part]));
}
console.log(`Compressed game/resources: ${firstSize} bytes on disk 1; ${readFileSync('build/game-data2.prg').length} bytes on disk 2.`);
tag.copy(image, 524272);
tag.copy(image, 524256);
writeFileSync('build/sst64.reu', image);