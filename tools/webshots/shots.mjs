import { chromium } from 'playwright'
import fs from 'fs'
fs.mkdirSync('out', { recursive: true })
const log = []
const b = await chromium.launch()
const ctx = await b.newContext({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, locale: 'nl-NL', isMobile: true, hasTouch: true })
const p = await ctx.newPage()
p.on('console', m => { if (m.type() !== 'warning') log.push(`[${m.type()}] ${m.text()}`) })
p.on('pageerror', e => log.push(`[pageerror] ${e}`))
p.on('response', r => { if (!/\.(wasm|mjs|js|png|ttf|html)$/.test(r.url())) log.push(`${r.status()} ${r.url()}`) })
p.on('websocket', ws => { log.push(`ws open ${ws.url()}`); ws.on('close', () => log.push('ws close')) })
const shot = async name => { await p.waitForTimeout(3000); await p.screenshot({ path: `out/${name}.png` }); log.push(`-- ${name}`) }
await p.goto('http://localhost:8080/')
await p.waitForTimeout(12000)
await shot('1-pin')
await p.keyboard.type('1111')
await p.keyboard.press('Enter')
await shot('2-wrong-pin')
for (let i = 0; i < 4; i++) await p.keyboard.press('Backspace')
await p.keyboard.type('4321')
await p.keyboard.press('Enter')
await p.waitForTimeout(5000)
await shot('3-home')
await p.reload()
await p.waitForTimeout(12000)
await shot('4-remembered')
await p.mouse.click(150, 400) // route A
await shot('5-route-sheet')
await p.mouse.click(195, 796) // dry run
await p.waitForTimeout(8000)
await shot('6-dry-run')
fs.writeFileSync('out/log.txt', log.join('\n'))
await b.close()
