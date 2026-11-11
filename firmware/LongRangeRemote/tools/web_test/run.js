// Browser test of the page's Discover tab against mock.py:
//   python3 mock.py ../../web_page.h &   then   node run.js   (needs Playwright; adjust the require path)
const { chromium } = require('/opt/npm-tools/node_modules/playwright');
(async () => {
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 400, height: 860 } });
  const errs = []; p.on('pageerror', e => errs.push(String(e))); p.on('console', m => { if (m.type() == 'error') errs.push(m.text()); });
  await p.goto('http://127.0.0.1:8765/'); await p.waitForTimeout(800);
  await p.click('#tabs button[data-t=manual]'); await p.waitForTimeout(300); await p.screenshot({ path: 'd1_idle.png' });
  const nopt = await p.$$eval('#dfam option', o => o.length); console.log('protocol options', nopt);
  await p.click('#dstart'); await p.waitForTimeout(300); await p.screenshot({ path: 'd2_sweep.png' });
  await p.click('#dreact'); await p.waitForTimeout(300); await p.screenshot({ path: 'd3_narrow.png' });
  await p.click('#dnnext'); await p.waitForTimeout(200);
  await p.click('#dnpick'); await p.waitForTimeout(300); await p.screenshot({ path: 'd4_learn.png' });
  console.log('preselected key', await p.$eval('#dlkey', e => e.value));
  await p.click('#dlname'); await p.waitForTimeout(200);
  await p.click('#dlnext'); await p.waitForTimeout(200);
  await p.selectOption('#dlkey', '0'); await p.fill('#dltext', 'Party mode'); await p.click('#dlname'); await p.waitForTimeout(300);
  await p.click('#dlnext'); await p.waitForTimeout(200); await p.selectOption('#dlkey', '4'); await p.click('#dlname'); await p.waitForTimeout(300);
  await p.screenshot({ path: 'd5_learned.png' });
  console.log('learned chips', await p.$$eval('#dllist .k', o => o.map(x => x.textContent)));
  await p.click('#dllist .del'); await p.waitForTimeout(300);
  console.log('after forget', await p.$$eval('#dllist .k', o => o.length), '| save button:', await p.$eval('#dlsave', e => e.textContent));
  await p.click('#dlsave'); await p.waitForTimeout(400); await p.screenshot({ path: 'd6_saved.png' });
  console.log('message:', await p.$eval('#dlmsg', e => e.textContent));
  console.log('page errors:', errs.length ? errs : 'none');
  const log = await (await p.request.get('http://127.0.0.1:8765/log')).json(); console.log(log.filter(x => !/a=state|a=meta/.test(x)).join(' | '));
  await b.close();
})();
