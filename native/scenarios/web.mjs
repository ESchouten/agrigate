// The web API (docs/clean/API.md) and the web page of the PACKAGED desktop app with its demo robot
// (PIN 4321), on localhost:8080. The packaged app has its own trimmed Java and carries the page
// in its resources, so this also catches a module or a file it lacks. The page runs in a
// phone-sized headless Chrome: a dry run, the panel, a key held by a finger, and a tab closed
// while holding one (the app must let go). Checks the calls the page makes; a screen is printed
// when a step fails.
//   node web.mjs      (needs: npm i playwright && npx playwright install chromium; the app
//                      started with AGRIGATE_DEMO="setup=done api=on")
import { chromium } from 'playwright'

const base = 'http://localhost:8080'
let failed = false
function say(ok, name, detail = '') {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${ok ? '' : ': ' + detail}`)
  failed ||= !ok
}
for (let i = 0; i < 120 && !(await fetch(base + '/').then(r => r.ok, () => false)); i++) await new Promise(r => setTimeout(r, 2_000))

// ---- the web API ----
async function api(name, method, path, want, pin = '4321') {
  const t0 = Date.now()
  const r = await fetch(base + path, { method, headers: pin ? { Authorization: `Bearer ${pin}` } : {} })
  const answer = `${r.status} ${(await r.text()).slice(0, 200)}`
  say(want.test(answer), name, answer)
  return Date.now() - t0
}
await api('api: the status', 'GET', '/api/clean/status', /^200 \{"ok":true,/)
await api('api: no PIN', 'GET', '/api/clean/status', /^401 /, '')
await api('api: the status read now', 'GET', '/api/clean/status/fresh', /^200 \{"ok":true,.*"planning":/)
await api('api: nothing to stop', 'POST', '/api/clean/stop', /^502 \{"ok":false,"fault":"Busy",/)
await api('api: a dry run', 'POST', '/api/clean/route/A/dryRun', /^200 \{"ok":true,/)
await api('api: a link in a browser starts nothing', 'GET', '/api/clean/route/A', /^404 /)

// ---- the web page ----
const seen = []
const browser = await chromium.launch()
const ctx = await browser.newContext({ viewport: { width: 390, height: 844 }, locale: 'nl-NL', isMobile: true, hasTouch: true })
// a phone that was here before: its PIN is in the browser
await ctx.addInitScript(() => {
  localStorage.setItem('clean.robots', '[{"uid":"page","pin":"4321"}]')
  localStorage.setItem('clean.onboarded', 'true')
})
const p = await ctx.newPage()
p.on('pageerror', e => seen.push(`pageerror ${e}`))
p.on('response', r => seen.push(`${r.request().method()} ${new URL(r.url()).pathname} ${r.status()}`))

async function check(name, what, timeout = 30_000) {
  const end = Date.now() + timeout
  while (!seen.some(what) && Date.now() < end) await p.waitForTimeout(250)
  const ok = seen.some(what)
  say(ok, name, seen.slice(-12).join(' | '))
  if (!ok) console.log(`     screen ${(await p.screenshot()).toString('base64')}`)
  seen.length = 0
}

await p.goto(base + '/')
await check('page: loads', s => /^GET \/.*\.wasm 200$/.test(s), 60_000)
await p.waitForTimeout(4_000)
await p.mouse.click(150, 400) // route A
await p.waitForTimeout(2_000)
await p.mouse.click(195, 796) // "Eerst een proefrun"
await check('page: a dry run reads the robot through the app', s => s === 'GET /api/clean/status/fresh 200', 30_000)
await check('page: the dry run is done by the app', s => s === 'POST /api/clean/route/A/dryRun 200', 60_000)

// the panel (a taller phone, so its card is in view): a key held by a finger for a second
const finger = await ctx.newCDPSession(p)
const touch = (type, x, y) => finger.send('Input.dispatchTouchEvent', { type, touchPoints: type === 'touchEnd' ? [] : [{ x, y }] })
await p.setViewportSize({ width: 390, height: 1200 })
await p.waitForTimeout(1_000)
await p.mouse.click(195, 1148) // "Sluiten" (the dry run's sheet)
await p.waitForTimeout(1_500)
await p.mouse.click(195, 860) // "Bedieningspaneel"
await p.waitForTimeout(1_500)
await p.mouse.click(195, 602) // "Verbinden met het paneel"
await check('page: the panel keeps the robot connection', s => s === 'GET /api/clean/panel 200')
const [kx, ky] = [78, 510] // Down
await touch('touchStart', kx, ky)
await p.waitForTimeout(1_200)
await check('page: a held key is repeated', () => seen.filter(s => s === 'POST /api/clean/panel/Down/down 200').length >= 2, 5_000)
await touch('touchEnd')
await check('page: the key comes up', s => s === 'POST /api/clean/panel/Down/up 200', 5_000)

// the tab closed while a finger holds a key: the app lets go by itself, and the next key (which
// waits for that) is answered within seconds
await touch('touchStart', kx, ky)
await check('page: the key is down again', s => s === 'POST /api/clean/panel/Down/down 200', 5_000)
await p.close()
const took = await api('api: a key held by a closed tab is let go', 'POST', '/api/clean/panel/Up', /^200 /)
say(took < 4_000, 'api: within seconds', `${took} ms`)
await browser.close()
process.exit(failed ? 1 : 0)
