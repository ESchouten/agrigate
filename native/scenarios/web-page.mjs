// The web page in a phone-sized headless Chrome against the desktop app with the demo robot
// (its PIN is 4321), which serves it on localhost:8080: wrong PIN, right PIN, PIN remembered,
// a dry run, a panel key held by a finger, and a tab closed while holding one (the barn computer
// must let go). Checks the messages between page and barn computer; screenshots go to ./web-page.
//   node web-page.mjs      (needs: npm i playwright && npx playwright install chromium)
import { chromium } from 'playwright'
import fs from 'fs'

fs.mkdirSync('web-page', { recursive: true })
const seen = []
// the barn computer comes up
for (let i = 0; i < 120 && !(await fetch('http://localhost:8080/').then(r => r.ok, () => false)); i++) await new Promise(r => setTimeout(r, 2_000))
const browser = await chromium.launch()
const ctx = await browser.newContext({ viewport: { width: 390, height: 844 }, locale: 'nl-NL', isMobile: true, hasTouch: true })
async function phone(tag = '', c = ctx) {
  const p = await c.newPage()
  p.on('pageerror', e => seen.push(`pageerror ${e}`))
  p.on('response', r => seen.push(`${tag}${r.status()} ${new URL(r.url()).pathname}${new URL(r.url()).search}`))
  p.on('websocket', ws => {
    ws.on('framereceived', f => seen.push(`${tag}<- ${f.payload}`))
    ws.on('framesent', f => seen.push(`${tag}-> ${f.payload}`))
  })
  return p
}
let p = await phone()

let failed = false
async function check(name, what, timeout = 30_000, show = false) {
  const end = Date.now() + timeout
  while (!seen.some(what) && Date.now() < end) await p.waitForTimeout(250)
  const ok = seen.some(what)
  const png = await p.screenshot({ path: `web-page/${name}.png` })
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}`)
  if (!ok) seen.slice(-15).forEach(s => console.log(`     ${s.slice(0, 300)}`))
  if (!ok || show) console.log(`     screen ${png.toString('base64')}`)
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

// the panel (a taller phone, so its card is in view): a key held by a finger for a second
const finger = await ctx.newCDPSession(p)
const touch = (type, x, y) => finger.send('Input.dispatchTouchEvent', { type, touchPoints: type === 'touchEnd' ? [] : [{ x, y }] })
await p.setViewportSize({ width: 390, height: 1200 })
await p.waitForTimeout(1_000)
await p.mouse.click(195, 1148) // "Sluiten" (the dry run's sheet)
await p.waitForTimeout(1_500)
await p.mouse.click(195, 860) // "Bedieningspaneel"
await check('panel-connects', s => s.startsWith('<- ') && s.includes('connected\\":true'), 30_000, true)
const [kx, ky] = [195, 900] // Down
await touch('touchStart', kx, ky)
await check('key-down', s => s.startsWith('-> ') && s.includes('input') && s.includes('Down'), 5_000)
await p.waitForTimeout(1_000)
await touch('touchEnd')
await check('key-up', s => s.startsWith('<- ') && s.includes('CallSuccess') && s.includes(':input:'), 5_000)

// the tab closed while a finger holds a key: the barn computer lets go (a second tab watches)
const other = await browser.newContext({ viewport: { width: 390, height: 844 }, locale: 'nl-NL' }) // its own window, so both stay in view
await other.addInitScript(() => localStorage.setItem('scraper.pin', '4321'))
const watch = await phone('watch ', other)
await watch.goto('http://localhost:8080/')
await check('second-tab-sees-the-panel-connection', s => s.startsWith('watch <- ') && s.includes('connected\\":true'))
await touch('touchStart', kx, ky)
await check('key-down-again', s => s.startsWith('-> ') && s.includes('input') && s.includes('Down'), 5_000)
await p.close()
p = watch
await check('barn-computer-lets-go', s => s.startsWith('watch <- ') && s.includes('states') && !s.includes('connected\\":true'), 20_000)
if (seen.some(s => s.startsWith('pageerror'))) failed = true
await browser.close()
process.exit(failed ? 1 : 0)
