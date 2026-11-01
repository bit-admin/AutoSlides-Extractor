// Renders background.html to background.png (640x480) and background@2x.png
// (1280x960). Run from anywhere with Playwright available:
//   node installer/dmg/render-background.js
const path = require('path');
const { chromium } = require('playwright');

(async () => {
  const dir = __dirname;
  const browser = await chromium.launch();
  for (const scale of [1, 2]) {
    const page = await browser.newPage({ viewport: { width: 640, height: 480 }, deviceScaleFactor: scale });
    await page.goto('file://' + path.join(dir, 'background.html'));
    await page.screenshot({ path: path.join(dir, scale === 1 ? 'background.png' : 'background@2x.png') });
    await page.close();
  }
  await browser.close();
})();
