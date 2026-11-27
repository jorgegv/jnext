import { atom, read, update } from 'claude-code'
import type { EngineInterface, Register } from 'claude-code'

import type { Hidden, LogEntry } from '../types'

// The conversation keeps what the person asked and Claude's answers to it. Everything that
// arrives unasked (a background task's notification, another agent's message) and the turn
// Claude runs in answer to it goes to the Logs pane instead, hidden from the conversation
// while the split is on (/logs-split toggles it). Rows the person cannot see are never lost:
// the Logs pane holds each one, and a press on an entry scrolls the conversation back to it.

const PANE = 'logs'
const TITLE = 'Logs'
const MAX_ENTRIES = 400
const MAX_HIDDEN = 4000

const entries = atom({ plugin: 'conv-logs', key: 'entries' } as const, [], { shape: 'entries-1' })
const hidden = atom(
  { plugin: 'conv-logs', key: 'hidden' } as const,
  { rows: [], texts: [], tools: [] },
  { shape: 'hidden-1' },
)
const isSplit = atom({ plugin: 'conv-logs', key: 'isSplit' } as const, true)
const seen = atom({ plugin: 'conv-logs', key: 'seen' } as const, 0)

// Origins the person typed or sent themselves; every other kind arrived unasked.
const PERSON = new Set(['composer', 'bridge', 'sdk', 'unclassified'])

function issuesOf(text: string): number[] {
  const found = new Set<number>()
  for (const m of text.matchAll(/(?<![\w/])#(\d{2,5})\b/g)) found.add(Number(m[1]))
  return [...found].sort((a, b) => a - b)
}

function clock(at: number): string {
  const d = new Date(at)
  const two = (n: number) => String(n).padStart(2, '0')
  return `${two(d.getHours())}:${two(d.getMinutes())}`
}

function bodyOf(text: unknown): string {
  const t = typeof text === 'string' ? text : ''
  if (t.trim() === '') return '(no text)'
  return t.length > 6000 ? t.slice(0, 6000) + '\n\n(cut)' : t
}

// Each text block of a row, trimmed: a hidden row is matched block by block, by equality, never by
// substring (a short reply to the person can be a substring of an earlier unrequested one).
function blocksOf(content: readonly unknown[]): string[] {
  return content
    .map(b => (b && typeof b === 'object' && (b as { type?: string }).type === 'text' ? String((b as { text?: string }).text ?? '').trim() : ''))
    .filter(Boolean)
}

async function add($: EngineInterface, entry: Omit<LogEntry, 'id' | 'issues'>) {
  await update($, entries, list => {
    const last = list[list.length - 1]
    const n = last === undefined ? 1 : Number(last.id.slice(1)) + 1
    return [...list, { ...entry, id: `L${n}`, issues: issuesOf(entry.text) }].slice(-MAX_ENTRIES)
  })
  $.ui.invalidate('ui.render')
}

async function hide($: EngineInterface, part: Partial<Hidden>) {
  await update($, hidden, was => ({
    rows: [...was.rows, ...(part.rows ?? [])].slice(-MAX_HIDDEN),
    texts: [...was.texts, ...(part.texts ?? [])].slice(-MAX_HIDDEN),
    tools: [...was.tools, ...(part.tools ?? [])].slice(-MAX_HIDDEN),
  }))
}

async function openPane($: EngineInterface, focus = false) {
  const opened = await $.ui.open(focus ? { id: PANE, title: TITLE, focus: true } : { id: PANE, title: TITLE })
  if (opened.isPlaced) {
    const list = await read($, entries)
    await update($, seen, () => list.length)
  }
  return opened
}

async function isHiddenTool($: EngineInterface, ids: readonly string[]) {
  if (ids.length === 0 || !(await read($, isSplit))) return false
  const hid = await read($, hidden)
  return ids.every(id => hid.tools.includes(id))
}


export const register: Register = on => {
  // The main loop's current turn: whether it answers the person, and the origin that started it.
  let turnIsAuto = false
  let pendingKind: string | undefined
  let autoFrom = ''
  let lastAutoRow: string | undefined

  on('session.start', async ($, e, next) => {
    const started = await next(e)
    await $.command.register({ name: 'logs', description: 'Open the Logs pane (unrequested messages and replies)' })
    await $.command.register({
      name: 'logs-split',
      description: 'Toggle hiding unrequested turns from the conversation (they stay in the Logs pane)',
    })
    void openPane($)
    return started
  })

  on('command.run', { command: 'logs' }, async $ => {
    const opened = await openPane($, true)
    return { text: opened.isPlaced ? 'Logs pane opened.' : `Logs pane not placed: ${opened.reason}` }
  })

  on('command.run', { command: 'logs-split' }, async $ => {
    const now = await update($, isSplit, v => !v)
    $.ui.invalidate('ui.render')
    return {
      text: now
        ? 'Split on: unrequested turns are hidden from the conversation and kept in the Logs pane.'
        : 'Split off: every row shows in the conversation; the Logs pane keeps collecting.',
    }
  })

  // Classify the main loop's turns by what started them.
  on('prompt.submit', async ($, e, next) => {
    const kind = e.origin?.kind ?? 'unclassified'
    if (e.turnId === undefined) pendingKind = kind
    if (!PERSON.has(kind) && kind !== 'auto-continuation' && e.text.trim()) {
      const text = e.text.length > 8000 ? e.text.slice(0, 8000) + '\n\n(cut)' : e.text
      await add($, { at: await $.clock.now(), kind: 'notice', from: kind, text })
    }
    return next(e)
  })

  on('turn.start', async ($, e, next) => {
    if (e.agentId === undefined) {
      const kind = pendingKind ?? 'composer'
      pendingKind = undefined
      if (kind !== 'auto-continuation') {
        turnIsAuto = !PERSON.has(kind)
        autoFrom = kind
      }
    }
    return next(e)
  })

  // Assistant rows and tool calls of an unrequested main-loop turn are hidden from the conversation.
  on('session.append', async ($, e, next) => {
    const kept = await next(e)
    if (turnIsAuto && e.agentId === undefined && e.message.type === 'assistant') {
      const blocks = blocksOf(e.message.content as readonly unknown[])
      lastAutoRow = e.uuid
      await hide($, { rows: [e.uuid], texts: blocks })
    }
    return kept
  })

  on('tool.call', async ($, e, next) => {
    if (turnIsAuto && (e as { agentId?: string }).agentId === undefined) {
      await hide($, { tools: [e.tool_use_id] })
    }
    return next(e)
  })

  on('turn.complete', async ($, e, next) => {
    const done = await next(e)
    if (e.agentId === undefined && turnIsAuto && e.answer.trim()) {
      await add($, { at: await $.clock.now(), kind: 'reply', from: `Claude (after ${autoFrom})`, text: e.answer, ...(lastAutoRow === undefined ? {} : { rowId: lastAutoRow }) })
    }
    if (e.agentId === undefined) {
      turnIsAuto = false
      lastAutoRow = undefined
    }
    return done
  })

  // Notification and message rows (logged at prompt.submit): hidden while the split is on.
  on('ui.render', { component: 'UserMessage' }, async ($, e, next) => {
    if (PERSON.has(e.props.origin.kind) || e.props.isExpanded || !(await read($, isSplit))) return next(e)
    const { Box } = $.ui.resolve(e)
    return <Box />
  })

  on('ui.render', { component: 'AssistantMessage' }, async ($, e, next) => {
    const hid = await read($, hidden)
    const block = e.props.text.trim()
    const isAuto = hid.rows.includes(e.requestId) || (block !== '' && hid.texts.includes(block))
    if (isAuto) {
      if (!(await read($, isSplit))) return next(e)
      const { Box } = $.ui.resolve(e)
      return <Box />
    }
    // A reply to the person: link the log entries about the issues it names.
    const named = issuesOf(e.props.text)
    if (!e.props.isFirstOfReply || named.length === 0) return next(e)
    const list = await read($, entries)
    const related = list.filter(x => x.issues.some(n => named.includes(n))).slice(-6)
    if (related.length === 0) return next(e)
    const { Box, Button, Text } = $.ui.resolve(e)
    const row = await next(e)
    return (
      <Box flexDirection="column">
        {row}
        <Box flexDirection="row" gap={1}>
          <Text dimColor>Logs:</Text>
          {related.map(x => (
            <Button
              key={`to-${x.id}`}
              label={`${x.id} ${x.issues.filter(n => named.includes(n)).map(n => `#${n}`).join(' ')}`}
              plain
              onPress={async () => {
                await openPane($)
                await $.ui.scroll({ to: { key: `entry-${x.id}` }, in: PANE, block: 'start' })
              }}
            />
          ))}
        </Box>
      </Box>
    )
  })

  on('ui.render', { component: 'ToolUse' }, async ($, e, next) => {
    if (!(await isHiddenTool($, [e.props.tool_use_id]))) return next(e)
    const { Box } = $.ui.resolve(e)
    return <Box />
  })

  on('ui.render', { component: 'ToolResult' }, async ($, e, next) => {
    if (!(await isHiddenTool($, [e.props.tool_use_id]))) return next(e)
    const { Box } = $.ui.resolve(e)
    return <Box />
  })

  on('ui.render', { component: 'ToolGroup' }, async ($, e, next) => {
    if (!(await isHiddenTool($, e.props.calls.flatMap(c => (c.tool_use_id === undefined ? [] : [c.tool_use_id]))))) return next(e)
    const { Box } = $.ui.resolve(e)
    return <Box />
  })

  // The band above the prompt: how many log entries arrived since the pane was last opened.
  on('ui.render', { component: 'AbovePrompt' }, async ($, e, next) => {
    const list = await read($, entries)
    const unseen = list.length - (await read($, seen))
    if (unseen <= 0) return next(e)
    const { Box, Button, Text } = $.ui.resolve(e)
    return (
      <Box flexDirection="row" gap={1}>
        <Text dimColor>{`${unseen} new in Logs`}</Text>
        <Button
          key="open-logs"
          label="Open Logs"
          plain
          onPress={async () => {
            await openPane($, true)
            const last = list[list.length - 1]
            if (last !== undefined) await $.ui.scroll({ to: { key: `entry-${last.id}` }, in: PANE, block: 'end' })
          }}
        />
      </Box>
    )
  })

  on('ui.render', { component: 'Pane', requestId: PANE }, async ($, e) => {
    const { Box, Button, Markdown, Text } = $.ui.resolve(e)
    const list = await read($, entries)
    const split = await read($, isSplit)
    return (
      <Box flexDirection="column" gap={1}>
        <Box flexDirection="row" gap={1}>
          <Text dimColor>{`${list.length} entries, split ${split ? 'on' : 'off'}`}</Text>
          <Button
            key="toggle-split"
            label={split ? 'Show all in conversation' : 'Hide unrequested'}
            plain
            onPress={async () => {
              await update($, isSplit, v => !v)
              $.ui.invalidate('ui.render')
            }}
          />
          <Button key="seen" label="Mark all seen" plain onPress={() => update($, seen, () => list.length)} />
          <Button key="clear" label="Clear" plain onPress={async () => {
            await update($, entries, () => [])
            await update($, seen, () => 0)
          }} />
        </Box>
        {list.length === 0 && <Text dimColor>Nothing unrequested yet.</Text>}
        {list.map(x => (
          <Box key={`entry-${x.id}`} flexDirection="column">
            <Box flexDirection="row" gap={1}>
              <Text bold>{x.id}</Text>
              <Text dimColor>{`${clock(x.at)} ${x.from}`}</Text>
              {x.rowId !== undefined && (
                <Button
                  key={`row-${x.id}`}
                  label="in conversation"
                  plain
                  onPress={() => $.ui.scroll({ to: { requestId: x.rowId as string }, block: 'center' })}
                />
              )}
            </Box>
            <Markdown text={bodyOf(x.text)} />
          </Box>
        ))}
      </Box>
    )
  })
}
