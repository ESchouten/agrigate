import { chromium } from 'playwright'
import fs from 'fs'
fs.mkdirSync('out', { recursive: true })
const log = []
const b = await chromium.launch()
async function phone(code) {
  const ctx = await b.newContext({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, locale: 'nl-NL', isMobile: true, hasTouch: true })
  const p = await ctx.newPage()
  p.on('console', m => { if (m.type() !== 'warning') log.push(`[${m.type()}] ${m.text()}`) })
  p.on('pageerror', e => log.push(`[pageerror] ${e}`))
  p.on('response', r => { if (/wasm/.test(r.url())) log.push(`${r.status()} ${r.headers()['content-type']} ${r.url()}`) })
  p.on('websocket', ws => {
    log.push(`ws open ${ws.url()}`)
    ws.on('framereceived', f => log.push(`<- ${String(f.payload).slice(0, 300)}`))
    ws.on('framesent', f => log.push(`-> ${String(f.payload).slice(0, 300)}`))
    ws.on('close', () => log.push('ws close'))
  })
  await p.goto(`http://localhost:8080/?${code}`)
  return p
}
const shot = async (p, name) => { await p.waitForTimeout(2500); await p.screenshot({ path: `out/${name}.png` }) }
let p = await phone('000000')
await p.waitForTimeout(12000)
await shot(p, '1-wrong-code')
p = await phone('123456')
await p.waitForTimeout(12000)
await shot(p, '2-home')
await p.mouse.click(150, 400) // route A row
await shot(p, '3-route-sheet')
fs.writeFileSync('out/log.txt', log.join('\n'))
await b.close()
