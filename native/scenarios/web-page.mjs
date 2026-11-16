// The web page in a phone-sized headless Chrome against the desktop app with the demo robot
// (its PIN is 4321), which serves it on localhost:8080: wrong PIN, right PIN, PIN remembered,
// a dry run. Checks the messages between page and barn computer; screenshots go to ./web-page.
//   node web-page.mjs      (needs: npm i playwright && npx playwright install chromium)
import { chromium } from 'playwright'
import fs from 'fs'

fs.mkdirSync('web-page', { recursive: true })
const seen = []
const browser = await chromium.launch()
const p = await (await browser.newContext({ viewport: { width: 390, height: 844 }, locale: 'nl-NL', isMobile: true, hasTouch: true })).newPage()
p.on('pageerror', e => seen.push(`pageerror ${e}`))
p.on('response', r => seen.push(`${r.status()} ${new URL(r.url()).pathname}${new URL(r.url()).search}`))
p.on('websocket', ws => {
  seen.push(`ws ${ws.url()}`)
  ws.on('framereceived', f => seen.push(`<- ${f.payload}`))
  ws.on('framesent', f => seen.push(`-> ${f.payload}`))
})

let failed = false
async function check(name, what, timeout = 30_000) {
  const end = Date.now() + timeout
  while (!seen.some(what) && Date.now() < end) await p.waitForTimeout(250)
  const ok = seen.some(what)
  await p.screenshot({ path: `web-page/${name}.png` })
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}`)
  if (!ok) seen.slice(-15).forEach(s => console.log(`     ${s.slice(0, 300)}`))
  failed ||= !ok
  seen.length = 0
}

await p.goto('http://localhost:8080/')
await check('page-loads', s => s === '403 /pin?pin=') // no PIN yet: the PIN form
await p.waitForTimeout(2_000)
await p.keyboard.type('1111')
await p.keyboard.press('Enter')
await check('wrong-pin-refused', s => s === '403 /pin?pin=1111')
for (let i = 0; i < 4; i++) await p.keyboard.press('Backspace')
await p.keyboard.type('4321')
await p.keyboard.press('Enter')
await check('right-pin-shows-the-robot', s => s.startsWith('<- ') && s.includes('Demo'))
await p.reload()
await check('pin-remembered', s => s.startsWith('<- ') && s.includes('Demo'))
await p.waitForTimeout(2_000)
await p.mouse.click(150, 400) // route A
await p.waitForTimeout(2_000)
await p.mouse.click(195, 796) // "Eerst een proefrun"
await check('dry-run-asked', s => s.startsWith('-> ') && s.includes('startRoute'), 5_000)
await check('dry-run-done-on-the-barn-computer', s => s.includes('CallSuccess') && s.includes('startRoute') && s.includes('\\"ok\\":true'), 60_000)
if (seen.some(s => s.startsWith('pageerror'))) failed = true
await browser.close()
process.exit(failed ? 1 : 0)
