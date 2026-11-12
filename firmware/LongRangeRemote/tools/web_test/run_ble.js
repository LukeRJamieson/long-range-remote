// Browser test of the Bluetooth tab against mock.py, on a phone-sized screen:
//   python3 mock.py ../../web_page.h &   then   node run_ble.js
const { chromium, devices } = require('/opt/npm-tools/node_modules/playwright');
(async () => {
  const b = await chromium.launch(); const ctx = await b.newContext({ ...devices['Pixel 5'] }); const p = await ctx.newPage();
  const errs = []; p.on('pageerror', e => errs.push(String(e))); p.on('console', m => { if (m.type() == 'error') errs.push(m.text()); });
  const api = async u => (await (await fetch('http://127.0.0.1:8765' + u)).json());
  let fail = 0; const check = (c, t) => { console.log((c ? '  [ok] ' : '  [FAIL] ') + t); if (!c) fail++; };
  await p.goto('http://127.0.0.1:8765/'); await p.waitForTimeout(800);
  const polls0 = (await api('/blepolls')).polls;
  check(polls0 == 0, 'no Bluetooth polling before the tab is opened');
  await p.tap('#tabs button[data-t=ble]'); await p.waitForTimeout(3500);
  const polls1 = (await api('/blepolls')).polls;
  check(polls1 >= 2, 'polls while the tab is open (' + polls1 + ')');
  const rows = await p.$$eval('#blelist .brow', r => r.map(x => x.querySelector('.t').textContent));
  check(rows.length == 6 && rows[0] == 'AirPods Pro 2nd Gen', 'lists the devices, strongest first');
  check(await p.$eval('#blestat', e => e.textContent).then(t => t.startsWith('5 nearby, 1 gone')), 'counts nearby and gone');
  check(!(await p.$eval('#bleoui', e => e.hidden)), 'offers the oui.bin upload when it is missing');
  await p.tap('#blelist .brow:nth-child(3)'); await p.waitForTimeout(300);
  const det = await p.$eval('#blelist .brow:nth-child(3) .bdet', e => e.textContent);
  check(det.includes('MAC maker') && det.includes('public (fixed)'), 'details open on tap');
  await p.waitForTimeout(1800);
  check(await p.$('#blelist .brow:nth-child(3) .bdet') != null, 'details stay open across refreshes');
  await p.screenshot({ path: 'ble_tab.png', fullPage: true });
  await p.check('#bletrk'); await p.waitForTimeout(200);
  check((await p.$$('#blelist .brow')).length == 2, 'trackers only');
  await p.uncheck('#bletrk'); await p.fill('#blefind', 'samsung'); await p.waitForTimeout(200);
  check((await p.$$('#blelist .brow')).length == 1, 'filter by text');
  await p.fill('#blefind', '');
  await p.tap('#tabs button[data-t=remote]'); await p.waitForTimeout(400);
  const a = (await api('/blepolls')).polls; await p.waitForTimeout(3000); const c = (await api('/blepolls')).polls;
  check(c == a, 'stops polling when the tab is closed');
  check(errs.length == 0, 'no page errors ' + errs.join(' | '));
  console.log(fail ? 'SOME CHECKS FAILED' : 'checks ok'); await b.close(); process.exit(fail ? 1 : 0);
})();
