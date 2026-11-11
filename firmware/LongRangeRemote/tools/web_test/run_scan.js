// Browser test of Code search's Auto speed against mock.py:
//   python3 mock.py ../../web_page.h &   then   node run_scan.js
const { chromium } = require('/opt/npm-tools/node_modules/playwright');
(async () => {
  const b = await chromium.launch(); const ctx = await b.newContext({ viewport: { width: 400, height: 860 } }); const p = await ctx.newPage();
  const errs = []; p.on('pageerror', e => errs.push(String(e))); p.on('console', m => { if (m.type() == 'error') errs.push(m.text()); });
  const sends = async () => (await (await fetch('http://127.0.0.1:8765/sends')).json());
  let fail = 0; const check = (c, t) => { console.log((c ? '  [ok] ' : '  [FAIL] ') + t); if (!c) fail++; };
  await p.goto('http://127.0.0.1:8765/'); await p.waitForTimeout(800);
  await p.click('#tabs button[data-t=scan]'); await p.waitForTimeout(500);
  check(await p.$eval('#sspeed', e => e.value) == '1', 'default: one code a second');
  await p.click('#sload'); await p.waitForTimeout(300);
  await p.selectOption('#sspeed', '0.5');
  let n0 = (await sends()).length; await p.click('#sauto'); await p.waitForTimeout(3100); await p.click('#sauto');
  let s = (await sends()).slice(n0); console.log('  0.5 s: ' + s.length + ' codes in 3.1 s');
  check(s.length >= 5 && s.length <= 7, '0.5 s: about 6 codes in 3 s');
  const gaps = s.slice(1).map((x, i) => x[0] - s[i][0]); check(gaps.every(g => g > 0.4 && g < 0.75), 'evenly spaced');
  await p.waitForTimeout(1200); check((await sends()).length == n0 + s.length, 'Stop stops it');
  await p.selectOption('#sspeed', '2'); n0 = (await sends()).length;
  await p.click('#sauto'); await p.waitForTimeout(2500); await p.selectOption('#sspeed', '0.75'); await p.waitForTimeout(2000); await p.click('#sauto');
  s = (await sends()).slice(n0); console.log('  2 s then 0.75 s: ' + s.length + ' codes in 4.5 s');
  check(s.length >= 3 && s.length <= 5, 'a new speed applies while running');
  console.log('  ' + await p.$eval('#scount', e => e.textContent));
  await p.screenshot({ path: 'scan_auto.png', fullPage: true });
  const p2 = await ctx.newPage(); await p2.goto('http://127.0.0.1:8765/'); await p2.waitForTimeout(800);
  check(await p2.$eval('#sspeed', e => e.value) == '0.75', 'speed remembered after reload');
  check(errs.length == 0, 'no page errors ' + errs.join(' | '));
  console.log(fail ? 'SOME CHECKS FAILED' : 'checks ok'); await b.close(); process.exit(fail ? 1 : 0);
})();
