import { atom, read, update } from 'claude-code'
import type { BoxProps, ElementConstructor, EngineInterface, Register, RenderNode, TextProps } from 'claude-code'

import type { StatusReport, StatusSection } from '../types'

// A Status pane for the jnext autonomous run, refreshed every 5 minutes and on demand.
// Everything comes from what the run keeps on disk and from the tools, never from the model:
// the newest run dir's agents.md (who runs, what waits) and blocked.md (what waits on the
// owner), the milestone's open issues, the CI runs, the epic branch and the remote hosts.

const PANE = 'run-status'
const TITLE = 'Run status'
const EVERY_MS = 5 * 60 * 1000
// The section title bar: a mid grey that reads on light and dark editor themes alike.
const BAR = '#5f6368'
const BAR_TEXT = '#ffffff'

const EPIC_LABEL = 'epic'
const NOT_CONFIGURED = 'not configured'

const report = atom({ plugin: 'run-status', key: 'report' } as const, null, { shape: 'report-2' })
const isBusy = atom({ plugin: 'run-status', key: 'isBusy' } as const, false)

type Row = { slot: string; agent: string; role: string; issue: string; state: string }

function stamp(at: number): string {
  const d = new Date(at)
  const two = (n: number) => String(n).padStart(2, '0')
  return `${d.getFullYear()}-${two(d.getMonth() + 1)}-${two(d.getDate())} ${two(d.getHours())}:${two(d.getMinutes())}`
}

// One body line as plain text: the pane draws lines, not markdown, to stay compact.
function plain(line: string): string {
  return line.replace(/\*\*/g, '').replace(/`/g, '')
}

// Colour for a status word: CI conclusions, lane states, paused or held work.
const TONE: Record<string, 'success' | 'error' | 'warning' | 'subtle'> = {
  success: 'success', failure: 'error', cancelled: 'warning', in_progress: 'warning', queued: 'warning',
  free: 'success', busy: 'warning', unreachable: 'error', PAUSED: 'subtle', PARKED: 'subtle', HELD: 'warning',
}
const TOKEN = /(#\d{2,5}|\b(?:success|failure|cancelled|in_progress|queued|free|busy|unreachable|PAUSED|PARKED|HELD)\b)/

// One body line as rich text: issue numbers bold in the suggestion colour, status words coloured,
// the part after the first ": " (the detail) dimmed.
function rich(Text: ElementConstructor<TextProps>, line: string): RenderNode {
  const cut = line.indexOf(': ')
  const head = cut < 0 ? line : line.slice(0, cut + 1)
  const tail = cut < 0 ? '' : line.slice(cut + 1)
  const parts = (text: string, isDetail: boolean) =>
    text.split(TOKEN).filter(x => x !== '').map(x => {
      if (/^#\d+$/.test(x)) return <Text bold color="suggestion">{x}</Text>
      const tone = TONE[x]
      if (tone !== undefined) return <Text bold={tone !== 'subtle'} color={tone}>{x}</Text>
      return isDetail ? <Text dimColor>{x}</Text> : x
    })
  return <Text>{parts(head, false)}{parts(tail, true)}</Text>
}

// One body line as a list item: `1. x` keeps its number, `- x` becomes a bullet, two spaces of
// indent per nesting level; the marker sits in its own column so wrapped text hangs under the text.
function item(
  Box: ElementConstructor<BoxProps>,
  Text: ElementConstructor<TextProps>,
  raw: string,
  key: string,
): RenderNode {
  const m = /^(\s*)(\d+\.|-)\s+(.*)$/.exec(raw)
  if (m === null) return <Box key={key}>{rich(Text, plain(raw))}</Box>
  const depth = Math.floor((m[1] ?? '').length / 2)
  const marker = m[2] === '-' ? (depth === 0 ? '\u2022' : '\u25e6') : (m[2] ?? '')
  return (
    <Box key={key} flexDirection="row" flexWrap="nowrap" paddingLeft={1 + depth * 2}>
      <Box width={marker.length + 1} minWidth={marker.length + 1} flexShrink={0}>
        <Text dimColor>{marker}</Text>
      </Box>
      <Box flexGrow={1} flexShrink={1} minWidth={0}>
        {rich(Text, plain(m[3] ?? ''))}
      </Box>
    </Box>
  )
}

function rowsOf(agentsMd: string): Row[] {
  return agentsMd
    .split('\n')
    .filter(l => l.startsWith('|') && !l.startsWith('| slot') && !l.startsWith('|---'))
    .map(l => l.split('|').map(c => c.trim()))
    .map(c => ({ slot: c[1] ?? '', agent: c[2] ?? '', role: c[3] ?? '', issue: c[4] ?? '', state: c[6] ?? '' }))
}

// Everything the pane needs to know about where it runs; a piece that cannot be derived stays undefined
// and the pane says "not configured" for what depends on it.
export type Settings = {
  repo: string
  home?: string
  project: string
  owner?: string
  milestone?: string
  autorun?: string
  runDir?: string
  hosts: string[]
  remoteDir?: string
}

const lastSegment = (path: string) => path.replace(/\/+$/, '').split('/').pop() ?? ''

// The remote-run hosts and work dir the run's slot.sh exports:
//   export JNEXT_REMOTE_RUN_HOSTS="a b" JNEXT_REMOTE_RUN_DIR=~/dir/   (or the single-host JNEXT_REMOTE_RUN_HOST)
export function parseSlot(text: string): { hosts: string[]; remoteDir?: string } {
  const unquote = (v: string) => v.replace(/^["']|["']$/g, '')
  const many = /JNEXT_REMOTE_RUN_HOSTS=("[^"]*"|'[^']*'|\S+)/.exec(text)
  const one = /JNEXT_REMOTE_RUN_HOST=("[^"]*"|'[^']*'|\S+)/.exec(text)
  const dir = /JNEXT_REMOTE_RUN_DIR=("[^"]*"|'[^']*'|\S+)/.exec(text)
  const raw = many?.[1] ?? one?.[1]
  const hosts = raw === undefined ? [] : unquote(raw).split(/\s+/).filter(Boolean)
  const remoteDir = dir?.[1] === undefined ? undefined : unquote(dir[1])
  return remoteDir === undefined ? { hosts } : { hosts, remoteDir }
}

async function run($: EngineInterface, repo: string, argv: string[], errors: string[], env?: Record<string, string>, quiet = false) {
  try {
    const r = await $.process.run(argv, { cwd: repo, timeoutMs: 60_000, env: { LANG: 'C', LC_ALL: 'C', ...env } })
    if (r.exitCode !== 0 && !quiet) errors.push(`${argv.slice(0, 3).join(' ')}: exit ${r.exitCode} ${r.stderr.trim().split('\n')[0] ?? ''}`)
    return r
  } catch (err) {
    errors.push(`${argv.slice(0, 3).join(' ')}: ${String(err)}`)
    return undefined
  }
}

async function readText($: EngineInterface, path: string, errors: string[], quiet = false) {
  try {
    return await $.fs.read(path)
  } catch (err) {
    if (!quiet) errors.push(`${path}: ${String(err)}`)
    return ''
  }
}

async function newestRunDir($: EngineInterface, autorun: string) {
  try {
    const dirs = (await $.fs.list(autorun)).map(x => x.name).filter(n => /^\d{4}-\d{2}-\d{2}$/.test(n)).sort()
    const last = dirs[dirs.length - 1]
    return last === undefined ? undefined : `${autorun}/${last}`
  } catch {
    return undefined
  }
}

// The version in a milestone title (`v1.5`, `1.10.2`) as numbers, undefined when the title has none.
function versionOf(title: string): number[] | undefined {
  const m = /^v?(\d+)(?:\.(\d+))?(?:\.(\d+))?/.exec(title)
  return m === null ? undefined : [Number(m[1]), Number(m[2] ?? 0), Number(m[3] ?? 0)]
}

// The working milestone: the lowest version among the open ones; titles without a version come after,
// ordered by milestone number.
export function lowestMilestone(ms: { number: number; title: string }[]): string | undefined {
  const rank = (a: { number: number; title: string }, b: { number: number; title: string }) => {
    const va = versionOf(a.title)
    const vb = versionOf(b.title)
    if (va === undefined || vb === undefined) return va === vb ? a.number - b.number : va === undefined ? 1 : -1
    for (let i = 0; i < 3; i++) if (va[i] !== vb[i]) return (va[i] ?? 0) - (vb[i] ?? 0)
    return a.number - b.number
  }
  return [...ms].sort(rank)[0]?.title
}

// The owner and the milestone come from GitHub and change rarely: asked once per session, per repo.
const fromGitHub = new Map<string, { owner?: string; milestone?: string }>()

// Derive the settings at run time: nothing about the machine or the owner is written in the module.
export async function derive($: EngineInterface, errors: string[]): Promise<Settings> {
  const cwd = await $.session.cwd()
  const outIn = async (dir: string, argv: string[]) => {
    const r = await run($, dir, argv, errors, undefined, true)
    return r !== undefined && r.exitCode === 0 ? r.stdout.trim() : ''
  }
  const repo = (await outIn(cwd, ['git', 'rev-parse', '--show-toplevel'])) || cwd
  const out = (argv: string[]) => outIn(repo, argv)
  const home = (await $.env.get('HOME')) || undefined
  const common = await out(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'])
  const project = common !== '' ? lastSegment(common.replace(/\/\.git\/?$/, '')) : lastSegment(repo)

  const known = fromGitHub.get(repo) ?? {}
  let { owner, milestone } = known
  if (owner === undefined) {
    try { owner = (JSON.parse(await out(['gh', 'repo', 'view', '--json', 'owner'])) as { owner?: { login?: string } }).owner?.login } catch { /* not configured */ }
  }
  if (milestone === undefined) {
    try { milestone = lowestMilestone(JSON.parse(await out(['gh', 'api', 'repos/{owner}/{repo}/milestones?state=open&per_page=100'])) as { number: number; title: string }[]) } catch { /* not configured */ }
  }
  fromGitHub.set(repo, { ...(owner === undefined ? {} : { owner }), ...(milestone === undefined ? {} : { milestone }) })

  const autorun = home === undefined ? undefined : `${home}/tmp/autorun/${project}`
  const runDir = autorun === undefined ? undefined : await newestRunDir($, autorun)
  const slot = runDir === undefined ? '' : await readText($, `${runDir}/slot.sh`, errors, true)
  const { hosts, remoteDir } = parseSlot(slot)
  const settings: Settings = { repo, project, hosts }
  if (home !== undefined) settings.home = home
  if (owner !== undefined) settings.owner = owner
  if (milestone !== undefined) settings.milestone = milestone
  if (autorun !== undefined) settings.autorun = autorun
  if (runDir !== undefined) settings.runDir = runDir
  if (remoteDir !== undefined) settings.remoteDir = remoteDir
  return settings
}

async function gather($: EngineInterface): Promise<StatusReport> {
  const errors: string[] = []
  const at = await $.clock.now()
  const cfg = await derive($, errors)
  const repo = cfg.repo
  const dir = cfg.runDir
  const rows = dir === undefined ? [] : rowsOf(await readText($, `${dir}/agents.md`, errors))
  const blocked = dir === undefined ? '' : (await readText($, `${dir}/blocked.md`, errors, true)).trim()

  const running = rows.filter(r => r.agent !== '' && r.agent !== '-')
  const waiting = rows.filter(r => r.agent === '-' || r.agent === '')

  type Issue = { number: number; title: string; labels: { name: string }[]; author: { login: string } }
  const issuesOut = cfg.milestone === undefined ? undefined : await run($, repo, ['gh', 'issue', 'list', '--milestone', cfg.milestone, '--state', 'open', '--limit', '200',
    '--json', 'number,title,labels,author'], errors)
  let issues: Issue[] = []
  try { issues = issuesOut ? (JSON.parse(issuesOut.stdout) as Issue[]) : [] } catch { errors.push('gh issue list: unreadable JSON') }

  type RunInfo = { databaseId: number; status: string; conclusion: string; headBranch: string; headSha: string }
  const runsOut = await run($, repo, ['gh', 'run', 'list', '--workflow', 'CI', '--limit', '6',
    '--json', 'databaseId,status,conclusion,headBranch,headSha'], errors)
  let runs: RunInfo[] = []
  try { runs = runsOut ? (JSON.parse(runsOut.stdout) as RunInfo[]) : [] } catch { errors.push('gh run list: unreadable JSON') }

  const ahead = await run($, repo, ['git', 'rev-list', '--count', 'origin/main..main'], errors)
  const head = await run($, repo, ['git', 'log', '-1', '--format=%h %s', 'main'], errors)
  const epicBranches = await run($, repo, ['git', 'for-each-ref', '--format=%(refname:short) %(objectname:short)', 'refs/heads/epic*'], errors)

  const load = ((await run($, repo, ['cat', '/proc/loadavg'], errors))?.stdout ?? '').split(' ').slice(0, 3).join(' ')
  const remotes: string[] = []
  for (const host of cfg.hosts) {
    const probe = await run($, repo, [`${repo}/.claude/skills/remote-run/remote-run.sh`, '--probe'], errors,
      { JNEXT_REMOTE_RUN_HOST: host, JNEXT_REMOTE_RUN_DIR: cfg.remoteDir ?? '' }, true)
    const said = probe === undefined ? 'probe failed' : probe.exitCode === 0 ? 'a lane is free' : 'all lanes busy, or unreachable'
    remotes.push(`${host}: ${said}`)
  }
  const admissions = dir === undefined ? '' : await readText($, `${dir}/slots/admissions.log`, errors, true)
  const lastAdmissions = admissions.trim().split('\n').filter(Boolean).slice(-3)

  const inRun = new Set(rows.map(r => r.issue.replace(/[^0-9]/g, '')).filter(Boolean).map(Number))
  const isEpic = (i: Issue) => i.labels.some(l => l.name === EPIC_LABEL)
  const rank = (i: Issue) => {
    const has = (n: string) => i.labels.some(l => l.name === n)
    if (has('bug')) return has('critical') ? 0 : 1
    if (has('feature')) return i.author.login === cfg.owner ? 3 : 2
    if (has('test infra')) return 4
    return 5
  }
  const isAudit = (i: Issue) => /audit and review/i.test(i.title)
  const next = issues
    .filter(i => !inRun.has(i.number) && !isEpic(i) && !isAudit(i))
    .sort((a, b) => rank(a) - rank(b) || a.number - b.number)
    .slice(0, 8)
  const title = (n: string) => {
    const i = issues.find(x => `#${x.number}` === n.trim())
    return i ? `${n} ${i.title}` : n
  }

  const sections: StatusSection[] = []
  const list = (items: string[]) => items.map((x, i) => `${i + 1}. ${x}`).join('\n')
  sections.push({ title: 'Blocked on you', body: blocked })
  const runBody = dir === undefined ? `- run directory: ${NOT_CONFIGURED}` : list(running.map(r => `${title(r.issue)}: ${r.role} ${r.agent}, ${r.state}`))
  sections.push({ title: `Running (${running.length} slots)`, body: runBody })
  const nextBody = cfg.milestone === undefined ? `- milestone: ${NOT_CONFIGURED}` : list(next.map(i => `#${i.number} ${i.title}`))
  sections.push({ title: 'Next, in this order', body: nextBody })
  sections.push({ title: 'Waiting', body: list(waiting.map(r => `${title(r.issue)}: ${r.state}`)) })
  const epic: string[] = []
  for (const e of issues.filter(isEpic)) epic.push(`- #${e.number} ${e.title}`)
  for (const b of (epicBranches?.stdout ?? '').trim().split('\n').filter(Boolean)) epic.push(`- branch \`${b}\``)
  sections.push({ title: 'Epic', body: epic.join('\n') })
  const ci = [`- main \`${head?.stdout.trim() ?? '?'}\`, ${ahead?.stdout.trim() ?? '?'} commits not on origin`]
  for (const r of runs) ci.push(`- ${r.databaseId} \`${r.headBranch}\` ${r.headSha.slice(0, 9)}: **${r.status === 'completed' ? r.conclusion : r.status}**`)
  sections.push({ title: 'main and CI', body: ci.join('\n') })
  const rem = [`- this host: load ${load}`, ...remotes.map(r => `- ${r}`)]
  if (cfg.hosts.length === 0) rem.push(`- remote hosts: ${NOT_CONFIGURED}`)
  if (lastAdmissions.length > 0) {
    rem.push('- last admissions:')
    for (const a of lastAdmissions) rem.push(`  - ${a.length > 160 ? a.slice(0, 160) + '...' : a}`)
  }
  sections.push({ title: 'Remotes', body: rem.join('\n') })
  return { at, sections, errors }
}

// The in-flight guard lives in this module, not in $.state: state survives a reload, and a
// reload during a refresh would otherwise leave "busy" set for good (every later refresh then
// returned at once). A refresh older than STALE_MS is taken as lost and does not block.
const STALE_MS = 3 * 60 * 1000
let inFlightSince = 0

async function refresh($: EngineInterface) {
  const now = await $.clock.now()
  if (inFlightSince !== 0 && now - inFlightSince < STALE_MS) return
  // This refresh's claim: a lost (stale) refresh that ends later must not release a newer one's
  // guard, clear its busy label, or write a report older than the one shown.
  const mine = now
  inFlightSince = mine
  await update($, isBusy, () => true)
  try {
    const r = await gather($)
    if (inFlightSince === mine) await update($, report, () => r)
  } finally {
    if (inFlightSince === mine) {
      inFlightSince = 0
      await update($, isBusy, () => false)
    }
    $.ui.invalidate('ui.render')
  }
}

async function openPane($: EngineInterface, focus: boolean) {
  return $.ui.open(focus ? { id: PANE, title: TITLE, focus: true } : { id: PANE, title: TITLE })
}

export const register: Register = on => {
  on('session.start', async ($, e, next) => {
    const started = await next(e)
    await $.command.register({ name: 'run-status', description: 'Open the Run status pane and refresh it now' })
    // A reload starts with no refresh in flight, whatever the stored flag says.
    await update($, isBusy, () => false)
    void openPane($, false)
    void refresh($)
    $.clock.every(EVERY_MS, () => refresh($))
    return started
  })

  on('command.run', { command: 'run-status' }, async $ => {
    const opened = await openPane($, true)
    void refresh($)
    return { text: opened.isPlaced ? 'Run status pane opened; refreshing.' : `Run status pane not placed: ${opened.reason}` }
  })

  on('ui.render', { component: 'Pane', requestId: PANE }, async ($, e) => {
    const { Box, Button, Text } = $.ui.resolve(e)
    const r = await read($, report)
    const busy = await read($, isBusy)
    return (
      <Box flexDirection="column" gap={1}>
        <Box flexDirection="row" gap={1}>
          <Text bold>{r ? stamp(r.at) : 'Run status'}</Text>
          <Text dimColor>{busy ? 'refreshing...' : r ? 'updated every 5 minutes' : 'not gathered yet'}</Text>
          <Button key="refresh" label="Refresh" plain onPress={() => refresh($)} />
        </Box>
        {r?.sections.map(sec => (
          <Box key={`sec-${sec.title}`} flexDirection="column" borderStyle="round" borderDimColor>
            <Box backgroundColor={BAR} paddingX={1}>
              <Text bold color={BAR_TEXT}>{sec.title}</Text>
            </Box>
            <Box flexDirection="column" paddingX={1} paddingTop={1} paddingBottom={1}>
              {sec.body.trim() === '' ? (
                <Text dimColor>Nothing</Text>
              ) : (
                sec.body.split('\n').map((line, i) => item(Box, Text, line, `l-${i}`))
              )}
            </Box>
          </Box>
        ))}
        {r && r.errors.length > 0 && (
          <Box flexDirection="column" borderStyle="round" borderDimColor paddingX={1}>
            <Text dimColor>Could not read:</Text>
            {r.errors.map((x, i) => (
              <Text key={`err-${i}`} dimColor>{x}</Text>
            ))}
          </Box>
        )}
      </Box>
    )
  })
}
