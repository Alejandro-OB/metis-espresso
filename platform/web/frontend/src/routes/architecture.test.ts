// @vitest-environment node
import { readFileSync, readdirSync } from 'node:fs'
import { join } from 'node:path'
import { describe, expect, it } from 'vitest'

describe('arquitectura visual', () => {
  it('impide primitivas interactivas directas fuera de shared/ui', () => {
    const routeRoot = join(process.cwd(), 'src', 'routes')
    const forbidden = /<(button|input|select|textarea|table)\b/
    const offenders = readdirSync(routeRoot)
      .filter((file) => file.endsWith('.tsx'))
      .filter((file) => forbidden.test(readFileSync(join(routeRoot, file), 'utf8')))
    expect(offenders).toEqual([])
  })
})
