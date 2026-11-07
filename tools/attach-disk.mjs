import {execFileSync} from 'node:child_process';
import {createServer, createConnection} from 'node:net';
import {resolve} from 'node:path';

const [session, image] = process.argv.slice(2);
if (!session || !image) throw new Error('Usage: node tools/attach-disk.mjs SESSION IMAGE');
const server = createServer();
await new Promise(resolveReady => server.listen(0, '127.0.0.1', resolveReady));
const port = server.address().port;
await new Promise(resolveClosed => server.close(resolveClosed));
function monitor(statement) {
    execFileSync('.venv/bin/python', ['-c', `from c64lib.session import Session; monitor=Session.attach(${JSON.stringify(session)}).monitor(); ${statement}`]);
}
monitor(`monitor.resource_set("MonitorServerAddress","127.0.0.1:${port}"); monitor.resource_set("MonitorServer",1)`);
try {
    await new Promise((resolveDone, reject) => {
        const connection = createConnection({host: '127.0.0.1', port});
        let response = '';
        connection.setTimeout(10000);
        connection.on('connect', () => connection.write(`attach "${resolve(image)}" 8\n`));
        connection.on('data', data => {
            response += data.toString();
            if (/ERROR|Cannot|Failed/i.test(response)) { connection.destroy(); reject(new Error(response)); }
            else if (!connection.writableEnded && /\(C:\$[0-9a-f]+\)\s*$/i.test(response)) connection.end('x\n');
        });
        connection.on('timeout', () => connection.destroy(new Error('Disk attach monitor timeout')));
        connection.on('error', reject);
        connection.on('close', () => resolveDone());
    });
} finally {
    monitor('monitor.resource_set("MonitorServer",0)');
}