import {execFileSync} from 'node:child_process';
import {copyFileSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import assert from 'node:assert/strict';

const cli = process.env.C64 || '.venv/bin/c64';
const session = `sst64-game-test-${process.pid}`;
const directory = mkdtempSync(join(tmpdir(), 'sst64-game-'));
const cold = process.argv.includes('--cold');
let expectHelpDisk = false;
function invoke(...args) {
    return JSON.parse(execFileSync(cli, [...args, '--json'], {encoding: 'utf8', maxBuffer: 1024 * 1024}));
}
function game(...args) { return invoke('-s', session, ...args); }
function readMemory(address, length = 1) {
    return game('mem', 'read', address, String(length)).bytes;
}
function monitor(statement, ...args) {
    return execFileSync('.venv/bin/python', ['-c', `import sys; from c64lib.session import Session; monitor=Session.attach(sys.argv[1]).monitor(); ${statement}`, session, ...args], {encoding: 'utf8'});
}
function configureReu(image) {
    monitor('monitor.resource_set("REU",0); monitor.resource_set("REUsize",512); monitor.resource_set("REUfilename",sys.argv[2]); monitor.resource_set("REUImageWrite",0); monitor.resource_set("REU",1)', image);
}
function screen() { return game('screen').text; }
function waitFor(marker, pages = false, attempts = 30) {
    for (let attempt = 0; attempt < attempts; ++attempt) {
        game('continue');
        try { game('wait', '--text', marker, '--timeout', '3'); return screen(); }
        catch {
            const text = screen();
            if (text.includes('SELFTEST FAILED') || text.includes('SST64 LOAD ERROR')) throw new Error(text);
            if (text.includes('INSERT DISK 2 FOR HELP')) {
                assert(expectHelpDisk, 'cached help should not request a disk');
                expectHelpDisk = false;
                execFileSync('node', ['tools/attach-disk.mjs', session, resolve('build/sst64-data.d64')]);
                game('key', 'type', ' ');
            }
            if ((pages && text.includes('HIT SPACE BAR')) || text.includes('-- MORE --')) game('key', 'type', ' ');
        }
    }
    throw new Error(`Missing ${marker}:\n${screen()}`);
}
function command(text, marker, pages = false) {
    monitor('monitor.keyboard_feed(bytes([147]))');
    game('key', 'type', `${text}\n`);
    return waitFor(marker, pages);
}
function waitForSetup(cold) {
    if (cold) {
        waitFor('INSERT DISK 2 THEN PRESS SPACE');
        execFileSync('node', ['tools/attach-disk.mjs', session, resolve('build/sst64-data.d64')]);
        game('key', 'type', ' ');
        waitFor('REGULAR');
        execFileSync('node', ['tools/attach-disk.mjs', session, join(directory, 'saves.d64')]);
    }
    return waitFor('REGULAR');
}
function assertTextColor(text, color) {
    const codes = [...text].map(character => {
        const code = character.toUpperCase().charCodeAt(0);
        return code >= 65 && code <= 90 ? code - 64 : code;
    });
    const cells = readMemory('$0400', 1000);
    const start = cells.findIndex((_, index) =>
        codes.every((code, offset) => cells[index + offset] === code));
    assert(start >= 0, `missing colored text: ${text}\n${screen()}`);
    const colors = readMemory('$D800', 1000);
    assert(codes.every((_, offset) => (colors[start + offset] & 15) === color),
        `${text} should have color ${color}`);
}
function testLoadingErrors() {
    const image = readFileSync('build/sst64.reu');
    image[524256] ^= 1;
    const stale = join(directory, 'stale-core.reu');
    writeFileSync(stale, image);
    configureReu(stale);
    game('disk', 'boot', join(directory, 'saves.d64'));
    waitFor('SST64 BOOT LOAD ERROR');
    console.log('PASS: stale core cache does not run without CORE.');
    const coreOnly = join(directory, 'core-only.d64');
    copyFileSync('build/sst64.d64', coreOnly);
    invoke('disk', 'rm', coreOnly, 'game');
    configureReu(stale);
    game('disk', 'boot', coreOnly);
    assert.match(waitForSetup(false), /ARITHMETIC SELFTEST PASSED/,
        'stale core cache should fall back to CORE without reloading valid game data');
    console.log('PASS: stale core cache falls back to CORE.');

    const helpImage = readFileSync('build/sst64.reu');
    helpImage[524240 + (12 >> 3)] &= ~(1 << (12 & 7));
    const uncached = join(directory, 'uncached-help.reu');
    writeFileSync(uncached, helpImage);
    const missing = join(directory, 'missing-help.d64');
    copyFileSync('build/sst64-data.d64', missing);
    invoke('disk', 'rm', missing, 'help12');
    for (const kind of ['missing', 'stale', 'truncated']) {
        const disk = join(directory, `${kind}-help.d64`);
        if (disk !== missing) {
            copyFileSync(missing, disk);
            let resource = readFileSync('build/help12.prg');
            if (kind === 'stale') resource[6] ^= 1;
            else resource = resource.subarray(0, resource.length - 1);
            const file = join(directory, `${kind}-help.prg`);
            writeFileSync(file, resource);
            invoke('disk', 'put', disk, file, 'help12');
        }
        configureReu(uncached);
        game('disk', 'boot', join(directory, 'saves.d64'));
        waitForSetup(false);
        game('key', 'type', 'tournament 1 short novice test\n');
        waitFor('COMMAND>', true);
        monitor('monitor.keyboard_feed(bytes([147]))');
        game('key', 'type', 'help status\n');
        waitFor('INSERT DISK 2 FOR HELP');
        execFileSync('node', ['tools/attach-disk.mjs', session, disk]);
        game('key', 'type', ' ');
        waitFor('SST64 LOAD ERROR');
        console.log(`PASS: ${kind} help resource reports a load error.`);
    }
    console.log('PASS: stale core cache fallback, missing CORE error, and missing/stale/truncated help errors.');
}
function testPasswords() {
    const symbols = execFileSync('/opt/llvm-mos-sdk/bin/llvm-nm', ['build/sst64.prg.elf'], {encoding: 'utf8'});
    const passwordAddress = symbols.match(/^([0-9a-f]+) [bB] passwd$/m);
    const kaboomAddress = symbols.match(/^([0-9a-f]+) T kaboom$/m);
    assert(passwordAddress && kaboomAddress, 'password test symbols should be available');
    const passwords = ['a', '1', '1abc', '1e2', '12345678', '123456789'];
    for (const [index, password] of passwords.entries()) {
        if (index) {
            configureReu(resolve('build/sst64.reu'));
            game('disk', 'boot', join(directory, 'saves.d64'));
            waitForSetup(false);
        }
        game('key', 'type', 'tournament 1 short novice\n');
        waitFor('SECRET PASSWORD');
        if (!index) assert.match(command('', 'SECRET PASSWORD'), /SECRET PASSWORD/,
            'empty passwords should still be rejected');
        command(password, 'COMMAND>', true);
        assert.deepEqual(readMemory(`$${passwordAddress[1]}`, password.length + 1),
            [...Buffer.from(password), 0], 'password should retain its literal text');
    }
    command('warp 3.5', 'CAPTAIN.');
    command('warp 4e0', 'CAPTAIN.');
    assert.match(command('status', 'TIME LEFT'), /WARP FACTOR\s+4\.0/,
        'normal decimal/exponent command parsing should be preserved');
    command('destruct', 'SELF-DESTRUCT-SEQUENCE-WILL-BE-ABORTED', true);
    assert.match(command('12345678', 'COMMAND>', true), /PASSWORD-REJECTED/,
        'a shorter numeric password should not match the saved password');
    command('destruct', 'SELF-DESTRUCT-SEQUENCE-WILL-BE-ABORTED', true);
    game('break', 'add', `$${kaboomAddress[1]}`);
    monitor('monitor.keyboard_feed(bytes([147]))');
    game('key', 'type', '123456789\n');
    game('wait', '--text', 'PASSWORD-ACCEPTED', '--timeout', '30');
    assert.match(screen(), /PASSWORD-ACCEPTED/,
        'self-destruct should accept the exact numeric password');
    console.log('PASS: empty-password rejection, short/numeric/digit-prefixed passwords, exact numeric self-destruct confirmation, and numeric command parsing.');
}
try {
    invoke('session', 'start', '--name', session, '--headless', '--warp');
    configureReu(cold ? '' : resolve('build/sst64.reu'));
    copyFileSync('build/sst64.d64', join(directory, 'saves.d64'));
    if (!cold) invoke('disk', 'rm', join(directory, 'saves.d64'), 'core');
    const bootStarted = performance.now();
    game('disk', 'boot', join(directory, 'saves.d64'));
    assert.match(waitForSetup(cold), /ARITHMETIC SELFTEST PASSED/);
    console.log(`Startup (${cold ? 'cold disks' : 'cached REU, no CORE file'}): ${((performance.now() - bootStarted) / 1000).toFixed(2)}s wall time in warped VICE.`);
    const vicColors = readMemory('$D020', 2);
    assert.equal(vicColors[0] & 0x0f, 0, 'VIC-II border should be black');
    assert.equal(vicColors[1] & 0x0f, 0, 'VIC-II background should be black');
    assert(readMemory('$D800', 1000).some(color => (color & 0x0f) === 5),
        'standard text should be green');
    assert.equal(readMemory('$0286')[0] & 0x0f, 14, 'waiting for input should use light blue');
    if (process.argv.includes('--passwords')) {
        testPasswords();
    } else {
    game('key', 'type', 'tournament 1 short novice test\n');
    const setup = waitFor('COMMAND>', true);
    assert.match(setup, /3 SUPPORTING STARBASES/);
    assert.match(setup, /7 - 2\s+SECTOR 3 - 7/);
    const cells = readMemory('$0400', 1000);
    const promptCodes = [3, 15, 13, 13, 1, 14, 4, 62];
    const promptStart = cells.findIndex((_, index) =>
        promptCodes.every((code, offset) => cells[index + offset] === code));
    assert(promptStart >= 0, 'command prompt should be visible');
    const promptColors = readMemory('$D800', 1000);
    assert(promptCodes.every((_, offset) => (promptColors[promptStart + offset] & 0x0f) === 15),
        'command prompt should be light grey');
    game('key', 'type', 'x');
    game('continue');
    game('wait', '--text', 'X', '--timeout', '3');
    assert(readMemory('$D800', 1000).some(color => (color & 0x0f) === 14),
        'player input should appear in light blue');
    monitor('monitor.keyboard_feed(bytes([20]))');
    const status = command('status', 'TIME LEFT');
    assert.match(status, /ENERGY\s+5000\.00/);
    command('warp 3.5', 'CAPTAIN.');
    command('warp 4e0', 'CAPTAIN.');
    command('chart', 'STAR CHART FOR THE KNOWN GALAXY');
    waitFor('COMMAND>', true);
    monitor('monitor.keyboard_feed(bytes([147]))');
    expectHelpDisk = cold;
    const helpStarted = performance.now();
    game('key', 'type', 'help status\n');
    const firstPage = waitFor('-- MORE --', false, 90);
    assert(!expectHelpDisk, 'cold help should load its section from disk');
    console.log(`HELP STATUS first page: ${((performance.now() - helpStarted) / 1000).toFixed(2)}s wall time in warped VICE.`);
    assert(!firstPage.includes('COMMAND>'), 'output should wait at the MORE prompt');
    game('continue');
    game('wait', '--text', '-- MORE --', '--timeout', '3');
    assert.equal(screen(), firstPage, 'screen should not change until a key is pressed');
    let help = '', indented = false, red = false, yellow = false;
    function observeHelpPage(text) {
        if (/^ {17}UNITS\)/m.test(text)) indented = true;
        if (/RED +- /.test(text)) {
            assertTextColor('RED', 2);
            red = true;
        }
        if (/YELLOW +- /.test(text)) {
            assertTextColor('YELLOW', 7);
            yellow = true;
        }
    }
    observeHelpPage(firstPage);
    game('key', 'type', ' ');
    for (let page = 0; page < 40 && !help; ++page) {
        game('continue');
        try { game('wait', '--text', 'COMMAND>', '--timeout', '3'); help = screen(); break; }
        catch {
            const text = screen();
            observeHelpPage(text);
            if (text.includes('HIT SPACE BAR')) game('key', 'type', ' ');
            if (!text.includes('-- MORE --')) continue;
            assert.notEqual(text, firstPage, 'a key should resume output');
            game('key', 'type', ' ');
        }
    }
    assert(help, 'help should finish after paging:\n' + screen());
    assert(indented, 'wrapped help lines should keep a hanging indent');
    assert(red && yellow, 'RED and YELLOW should be shown in their own colors');
    assert(!help.includes('-- MORE --'), 'MORE prompt should be erased after continuing');
    assert.match(help, /DO NOTHING IN THE\s+MEANTIME\./,
        '40-column wrapping should keep words intact');
    assert.match(help, /SRSCAN\s+COMMAND/, 'words should not split across output chunks');
    execFileSync('node', ['tools/attach-disk.mjs', session, join(directory, 'saves.d64')]);
    command('help status', '-- MORE --');
    waitFor('COMMAND>', true);
    expectHelpDisk = cold;
    const shipHelp = command('help abandon', 'COMMAND>', true);
    assert.match(shipHelp, /FAERIE QUEENE/);
    assertTextColor('ENTERPRISE', 1);
    assertTextColor('FAERIE', 1);
    assertTextColor('QUEENE.', 1);
    assertTextColor('CANNOT', 5);
    assertTextColor('COMMAND>', 15);
    assert(!expectHelpDisk, 'abandon help should load its own section on a cold machine');
    execFileSync('node', ['tools/attach-disk.mjs', session, join(directory, 'saves.d64')]);
    const saved = command('freeze test', 'COMMAND>');
    assert.match(saved, /SAVING GAME\.\.\./, 'saving should announce itself');
    assert(readMemory('$0400', 1000).filter(code => code === 160).length >= 20, 'progress bar should fill completely');
    const listing = invoke('disk', 'ls', join(directory, 'saves.d64'));
    assert(listing.files.some(file => file.name.toLowerCase() === 'test.trk'), 'save file missing');
    game('run', 'build/sst64.prg');
    assert.match(waitForSetup(cold), /ARITHMETIC SELFTEST PASSED/);
    game('key', 'type', 'frozen test\n');
    const restored = waitFor('COMMAND>', true);
    assert(!restored.includes("CAN'T FIND GAME FILE"), 'save reload failed');
    const restoredStatus = command('status', 'TIME LEFT');
    assert.match(restoredStatus, /ENERGY\s+5000\.00/);
    assert.match(restoredStatus, /WARP FACTOR\s+4\.0/);
    waitFor('COMMAND>');
    const symbols = execFileSync('/opt/llvm-mos-sdk/bin/llvm-nm', ['build/sst64.prg.elf'], {encoding: 'utf8'});
    const mathTest = symbols.match(/^([0-9a-f]+) T sst64_math_selftest$/m);
    assert(mathTest, 'extended math diagnostic should remain callable');
    monitor('monitor.keyboard_feed(bytes([147]))');
    game('call', `$${mathTest[1]}`, '--timeout', '120');
    assert.match(screen(), /MATH SELFTEST PASSED/);
    if (!cold) testLoadingErrors();
    console.log('PASS: cached boot without CORE, setup, reports, numeric input, charts, indexed/cached help, white ship names, save/reload and extended C64 math diagnostic.');
    }
} finally {
    try { invoke('session', 'stop', session); } finally { rmSync(directory, {recursive: true, force: true}); }
}