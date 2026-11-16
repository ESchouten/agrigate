import { chromium } from 'playwright'
import fs from 'fs'
fs.mkdirSync('out', { recursive: true })
const log = []
const b = await chromium.launch()
async function phone(code, lang = 'nl-NL') {
  const ctx = await b.newContext({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, locale: lang, isMobile: true, hasTouch: true })
  if (code) await ctx.addInitScript(c => localStorage.setItem('scraper.code', c), code)
  const p = await ctx.newPage()
  p.on('console', m => log.push(`[${m.type()}] ${m.text()}`))
  p.on('pageerror', e => log.push(`[pageerror] ${e}`))
  p.on('websocket', ws => {
    log.push(`ws open ${ws.url()}`)
    ws.on('framereceived', f => log.push(`<- ${String(f.payload).slice(0, 200)}`))
    ws.on('framesent', f => log.push(`-> ${String(f.payload).slice(0, 200)}`))
    ws.on('close', () => log.push('ws close'))
  })
  await p.goto('http://localhost:8080/')
  return p
}
let p = await phone(null)
await p.waitForTimeout(12000)
await p.screenshot({ path: 'out/1-code.png' })
p = await phone('123456')
await p.waitForTimeout(12000)
await p.screenshot({ path: 'out/2-home.png' })
// tap the middle of the screen's first route card region, then wait
await p.mouse.click(195, 560)
await p.waitForTimeout(4000)
await p.screenshot({ path: 'out/3-tap.png' })
fs.writeFileSync('out/log.txt', log.join('\n'))
await b.close()
