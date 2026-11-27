import type { On } from 'claude-code'
import { expect, mock, test } from 'claude-code/testing'

const PANE = {
  plugin: 'run-status',
  component: 'Pane',
  requestId: 'run-status',
  props: {
    title: 'Run status',
    isFocused: false,
    bodyColumns: 80,
    placement: 'dock',
    scroll: { offset: 0, bodyRows: 40 },
    view: {},
  },
} as const

const SLOT = '#!/bin/sh\nexport JNEXT_REMOTE_RUN_HOSTS="host-a host-b" JNEXT_REMOTE_RUN_DIR=~/work-dir/\n'

const AGENTS = [
  '| slot | agent | role | issue | worktree | state |',
  '|---|---|---|---|---|---|',
  '| nonepic-3 | impl-580 | implementer | #580 | ~/tmp/worktrees/jnext/gh580 | dispatched 09:20 |',
  '| - | - | - | #579 | ~/tmp/worktrees/jnext/gh579 | plan GO; HELD until #577 lands |',
].join('\n')

test('a refresh lays out running, waiting, queue and remotes', async ($, on) => {
  on('clock.now', () => ({ value: 1795700000000 }))
  on('session.cwd', () => ({ value: '/work/proj' }))
  on('env.get', () => ({ value: '/home/u' }))
  const probedHosts: string[] = []
  on('fs.list', () => ({ value: [{ name: '2026-11-26', kind: 'dir' as const, size: 0, mtimeMs: 0, isLink: false }] }))
  on('fs.read', (_, e) => {
    const path = String((e as { path?: string }).path ?? '')
    if (path.endsWith('agents.md')) return { value: AGENTS }
    if (path.endsWith('blocked.md')) return { value: '1. #579: rename the raw image?' }
    if (path.endsWith('slot.sh')) return { value: SLOT }
    return { value: '' }
  })
  on('process.run', (_, e) => {
    const argv = (e as { argv: string[] }).argv
    const out = (stdout: string, exitCode = 0) => ({ value: { exitCode, stdout, stderr: '', isStdoutTruncated: false, isStderrTruncated: false } })
    if (argv[0].endsWith('remote-run.sh')) {
      probedHosts.push((e as { init?: { env?: Record<string, string> } }).init?.env?.JNEXT_REMOTE_RUN_HOST ?? '')
      return out('', 75)
    }
    if (argv[0] === 'gh' && argv[1] === 'repo') return out('{"owner":{"login":"owner1"}}')
    if (argv[0] === 'gh' && argv[1] === 'api') return out('[{"number":9,"title":"v1.6"},{"number":7,"title":"v1.5"}]')
    if (argv[0] === 'gh' && argv[1] === 'issue')
      return out(JSON.stringify([
        { number: 580, title: 'CI cache', labels: [{ name: 'bug' }], author: { login: 'owner1' } },
        { number: 579, title: 'SD rename', labels: [{ name: 'feature' }], author: { login: 'owner1' } },
        { number: 590, title: 'A user bug', labels: [{ name: 'bug' }], author: { login: 'someone' } },
      ]))
    if (argv[0] === 'gh' && argv[1] === 'run') return out('[]')
    if (argv[0] === 'cat') return out('3.10 2.00 1.00 1/100 1')
    if (argv[0] === 'git') return out('')
    return out('', 75)
  })

  const ui = await $.ui.mount({ ...PANE, surface: 'vscode' })
  expect(await ui.find({ type: 'Text', text: /not gathered yet/ })).toBeDefined()
  await ui.press({ key: 'refresh' })
  await ui.unmount()

  const after = await $.ui.mount({ ...PANE, surface: 'vscode' })
  const text = (await after.findAll({ type: 'Text' })).map(m => m.text).join('\n')
  expect(await after.find({ type: 'Text', text: /^Blocked on you$/ })).toBeDefined()
  expect(text).toContain('#579: rename the raw image?')
  expect(text).toContain('#580 CI cache: implementer impl-580')
  expect(text).toContain('#579 SD rename: plan GO; HELD until #577 lands')
  expect(text).toContain('#590 A user bug')
  expect(await after.find({ type: 'Text', text: /^1\.$/ })).toBeDefined()
  expect(probedHosts).toEqual(['host-a', 'host-b'])
  expect(text).toContain('host-a: all lanes busy')
  expect(text).toContain('load 3.10 2.00 1.00')
  await after.unmount()
})

test('with nothing in blocked.md the section says Nothing', async ($, on) => {
  on('clock.now', () => ({ value: 1795700000000 }))
  on('session.cwd', () => ({ value: '/work/proj2' }))
  on('env.get', () => ({ value: '/home/u' }))
  on('fs.list', () => ({ value: [{ name: '2026-11-26', kind: 'dir' as const, size: 0, mtimeMs: 0, isLink: false }] }))
  on('fs.read', () => ({ value: '' }))
  on('process.run', (_, e) => {
    const argv = (e as { argv: string[] }).argv
    const stdout = argv[0] === 'gh' ? '[]' : ''
    return { value: { exitCode: 0, stdout, stderr: '', isStdoutTruncated: false, isStderrTruncated: false } }
  })
  const ui = await $.ui.mount({ ...PANE, surface: 'vscode' })
  await ui.press({ key: 'refresh' })
  await ui.unmount()
  const after = await $.ui.mount({ ...PANE, surface: 'vscode' })
  expect(await after.find({ type: 'Text', text: /^Blocked on you$/ })).toBeDefined()
  expect(await after.find({ type: 'Text', text: /^Nothing$/ })).toBeDefined()
  await after.unmount()
})

// Settings derivation: which commands and paths the module asks for, from nothing but the session.
test('the settings are derived from the environment, the repo, the run dir, slot.sh and GitHub', async ($, on) => {
  const listed: string[] = []
  const probed: { host?: string; dir?: string }[] = []
  const issueArgs: string[][] = []
  const read: string[] = []
  on('clock.now', () => ({ value: 1795700000000 }))
  const probeArgv0: string[] = []
  let github = 0
  on('session.cwd', () => ({ value: '/work/wt/gh1/src' }))
  on('env.get', () => ({ value: '/home/pat' }))
  on('fs.list', (_, e) => {
    listed.push(String((e as { path?: string }).path))
    return { value: ['2026-11-25', '2026-11-27', '2026-11-26', 'notes'].map(name => ({ name, kind: 'dir' as const, size: 0, mtimeMs: 0, isLink: false })) }
  })
  on('fs.read', (_, e) => {
    const path = String((e as { path?: string }).path ?? '')
    read.push(path)
    return { value: path.endsWith('slot.sh') ? 'export JNEXT_REMOTE_RUN_HOST=solo JNEXT_REMOTE_RUN_DIR=~/rr/' : '' }
  })
  on('process.run', (_, e) => {
    const { argv, init } = e as { argv: string[]; init?: { env?: Record<string, string> } }
    const env = init?.env
    const out = (stdout: string, exitCode = 0) => ({ value: { exitCode, stdout, stderr: '', isStdoutTruncated: false, isStderrTruncated: false } })
    if (argv[0] === 'git' && argv[2] === '--show-toplevel') return out('/work/wt/gh1\n')
    if (argv[0] === 'git' && argv[1] === 'rev-parse') return out('/work/myproj/.git\n')
    if (argv[0] === 'gh' && argv[1] === 'repo') { github++; return out('{"owner":{"login":"acme"}}') }
    // the number order (5 < 7 < 12) and the version order (v1.5 < v1.6, v1.10 last) disagree
    if (argv[0] === 'gh' && argv[1] === 'api') {
      github++
      return out('[{"number":5,"title":"v1.6"},{"number":7,"title":"v1.5"},{"number":9,"title":"v1.10"},{"number":3,"title":"backlog"}]')
    }
    if (argv[0] === 'gh' && argv[1] === 'issue') {
      issueArgs.push(argv)
      return out(JSON.stringify([
        { number: 5, title: 'Maintainer feature', labels: [{ name: 'feature' }], author: { login: 'acme' } },
        { number: 6, title: 'Outside feature', labels: [{ name: 'feature' }], author: { login: 'other' } },
      ]))
    }
    if (argv[0].endsWith('remote-run.sh')) { probeArgv0.push(argv[0]); probed.push({ host: env?.JNEXT_REMOTE_RUN_HOST ?? '', dir: env?.JNEXT_REMOTE_RUN_DIR ?? '' }); return out('') }
    return out('[]')
  })
  const ui = await $.ui.mount({ ...PANE, surface: 'vscode' })
  await ui.press({ key: 'refresh' })
  await ui.unmount()
  const after = await $.ui.mount({ ...PANE, surface: 'vscode' })
  const text = (await after.findAll({ type: 'Text' })).map(m => m.text).join('\n')
  expect(listed).toContain('/home/pat/tmp/autorun/myproj')
  expect(read).toContain('/home/pat/tmp/autorun/myproj/2026-11-27/slot.sh')
  expect(issueArgs[0]).toContain('v1.5')
  expect(probeArgv0).toEqual(['/work/wt/gh1/.claude/skills/remote-run/remote-run.sh'])
  expect(github).toBe(2)
  expect(probed).toEqual([{ host: 'solo', dir: '~/rr/' }])
  expect(text).toContain('solo: a lane is free')
  // the maintainer's feature (author = repo owner) ranks after the outside one
  expect(text.indexOf('#6 Outside feature')).toBeLessThan(text.indexOf('#5 Maintainer feature'))
  await after.press({ key: 'refresh' })
  expect(github).toBe(2) // the owner and the milestone are asked once per session
  await after.unmount()
})

test('what cannot be derived shows as not configured, without a crash', async ($, on) => {
  on('clock.now', () => ({ value: 1795700000000 }))
  on('session.cwd', () => ({ value: '/work/none' }))
  on('env.get', () => ({ value: undefined }))
  on('fs.list', () => { throw new Error('no such dir') })
  on('fs.read', () => { throw new Error('no such file') })
  on('process.run', () => ({ value: { exitCode: 1, stdout: '', stderr: 'nope', isStdoutTruncated: false, isStderrTruncated: false } }))
  const ui = await $.ui.mount({ ...PANE, surface: 'vscode' })
  await ui.press({ key: 'refresh' })
  await ui.unmount()
  const after = await $.ui.mount({ ...PANE, surface: 'vscode' })
  const text = (await after.findAll({ type: 'Text' })).map(m => m.text).join('\n')
  expect(text).toContain('run directory: not configured')
  expect(text).toContain('milestone: not configured')
  expect(text).toContain('remote hosts: not configured')
  // a failing derivation command is "not configured", never a "Could not read" line
  for (const word of ['rev-parse', 'gh repo', 'gh api']) expect(text).not.toContain(word)
  await after.unmount()
})

test('a busy flag left in state by a reload does not block Refresh', async ($, on) => {
  // What an earlier load stored mid-refresh: isBusy true, and no refresh actually running.
  // Every read of isBusy answers the stale true an earlier load left, until this load writes it.
  let stale = true
  on('state.get', (_, e, next) => {
    if ((e as { key?: string }).key === 'isBusy' && stale) return { value: { value: true, version: 0 } }
    return next(e)
  })
  on('state.set', (_, e, next) => {
    if ((e as { key?: string }).key === 'isBusy') stale = false
    return next(e)
  })
  on('clock.now', () => ({ value: 1795700000000 }))
  on('session.cwd', () => ({ value: '/work/stale' }))
  on('env.get', () => ({ value: '/home/u' }))
  on('fs.list', () => ({ value: [{ name: '2026-11-26', kind: 'dir' as const, size: 0, mtimeMs: 0, isLink: false }] }))
  on('fs.read', () => ({ value: '' }))
  let gathered = 0
  on('process.run', (_, e) => {
    const argv = (e as { argv: string[] }).argv
    if (argv[0] === 'gh' && argv[1] === 'run') gathered += 1
    const stdout = argv[0] === 'gh' ? '[]' : ''
    return { value: { exitCode: 0, stdout, stderr: '', isStdoutTruncated: false, isStderrTruncated: false } }
  })
  const ui = await $.ui.mount({ ...PANE, surface: 'vscode' })
  await ui.press({ key: 'refresh' })
  await ui.unmount()
  expect(gathered).toBe(1)
})

// A world whose `gh run list` (one per gather) counts and then holds for holdMs(n) on the mocked clock.
function world(on: On, sleep: (ms: number) => Promise<void>, holdMs: (n: number) => number) {
  on('session.cwd', () => ({ value: '/work/guard' }))
  on('env.get', () => ({ value: '/home/u' }))
  on('fs.list', () => ({ value: [{ name: '2026-11-26', kind: 'dir' as const, size: 0, mtimeMs: 0, isLink: false }] }))
  on('fs.read', () => ({ value: '' }))
  on('command.register', () => ({ value: { command: 'run-status' } }))
  on('ui.open', () => ({ value: { isPlaced: true } }))
  on('session.start', (_, e) => ({ cwd: e.cwd }))
  const count = { gathered: 0 }
  on('process.run', async (_, e) => {
    const argv = (e as { argv: string[] }).argv
    if (argv[0] === 'gh' && argv[1] === 'run') {
      count.gathered += 1
      const ms = holdMs(count.gathered)
      if (ms > 0) await sleep(ms)
    }
    return { value: { exitCode: 0, stdout: argv[0] === 'gh' ? '[]' : '', stderr: '', isStdoutTruncated: false, isStderrTruncated: false } }
  })
  return count
}

const MIN = 60 * 1000

test('one refresh at a time; released at its end; a lost one stops blocking after 3 minutes', async ($, on) => {
  const clock = mock.clock(on, { now: 1795700000000 })
  // the first gather is lost (held for an hour); later ones return at once
  const c = world(on, ms => clock.sleep(ms), n => (n === 1 ? 60 * MIN : 0))
  await $.session.start({ cwd: '/work/guard', surface: 'vscode', isInteractive: true })
  await clock.settle()
  expect(c.gathered).toBe(1)
  const ui = await $.ui.mount({ ...PANE, surface: 'vscode' })
  await ui.press({ key: 'refresh' })
  await clock.settle()
  expect(c.gathered).toBe(1) // held: no second gather
  await clock.advance(3 * MIN - 1000)
  await ui.press({ key: 'refresh' })
  await clock.settle()
  expect(c.gathered).toBe(1) // STALE_MS - 1 s: still held
  await clock.advance(2000)
  await ui.press({ key: 'refresh' })
  await clock.settle()
  expect(c.gathered).toBe(2) // past STALE_MS: starts, completes
  await ui.press({ key: 'refresh' })
  await clock.settle()
  expect(c.gathered).toBe(3) // released at its end: the next one starts
  await ui.unmount()
})

test('session.start refreshes once, then every 5 minutes', async ($, on) => {
  const clock = mock.clock(on, { now: 1795700000000 })
  const c = world(on, ms => clock.sleep(ms), () => 0)
  await $.session.start({ cwd: '/work/guard', surface: 'vscode', isInteractive: true })
  await clock.settle()
  expect(c.gathered).toBe(1)
  await clock.advance(5 * MIN - 1000)
  expect(c.gathered).toBe(1)
  await clock.advance(1000)
  expect(c.gathered).toBe(2)
})

test('a lost refresh that ends late does not release the live one', async ($, on) => {
  const clock = mock.clock(on, { now: 1795700000000 })
  // the first gather is lost and ends at 6 minutes; the timer's second, at 5, is held for an hour
  const c = world(on, ms => clock.sleep(ms), n => (n === 1 ? 6 * MIN : n === 2 ? 60 * MIN : 0))
  await $.session.start({ cwd: '/work/guard', surface: 'vscode', isInteractive: true })
  await clock.settle()
  expect(c.gathered).toBe(1)
  const ui = await $.ui.mount({ ...PANE, surface: 'vscode' })
  await clock.advance(5 * MIN)
  expect(c.gathered).toBe(2) // the first is stale: the timer starts a second
  await clock.advance(MIN) // the first is released
  await ui.press({ key: 'refresh' })
  await clock.settle()
  expect(c.gathered).toBe(2) // the second still holds the guard
  expect(await ui.find({ type: 'Text', text: 'refreshing...' })).toBeDefined()
  await ui.unmount()
})

test('a lost refresh that ends late does not overwrite a newer report', async ($, on) => {
  const T0 = 1795700000000
  const clock = mock.clock(on, { now: T0 })
  // the first gather is lost and ends at 6 minutes; the timer's second, at 5, returns at once
  const c = world(on, ms => clock.sleep(ms), n => (n === 1 ? 6 * MIN : 0))
  await $.session.start({ cwd: '/work/guard', surface: 'vscode', isInteractive: true })
  await clock.settle()
  await clock.advance(5 * MIN)
  expect(c.gathered).toBe(2)
  await clock.advance(MIN) // the first ends
  const two = (n: number) => String(n).padStart(2, '0')
  const stamp = (at: number) => { const d = new Date(at); return `${d.getFullYear()}-${two(d.getMonth() + 1)}-${two(d.getDate())} ${two(d.getHours())}:${two(d.getMinutes())}` }
  const ui = await $.ui.mount({ ...PANE, surface: 'vscode' })
  expect(await ui.find({ type: 'Text', text: stamp(T0 + 5 * MIN) })).toBeDefined()
  await ui.unmount()
})
