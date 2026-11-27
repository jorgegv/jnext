import { expect, test } from 'claude-code/testing'

const NOTICE = {
  plugin: 'conv-logs',
  component: 'UserMessage',
  requestId: 'row-1',
  props: {
    text: 'Agent "Plan GH #580" finished',
    origin: { kind: 'task-notification' },
    isExpanded: false,
    task: { id: 'a1', status: 'completed' },
  },
} as const

const PANE = {
  plugin: 'conv-logs',
  component: 'Pane',
  requestId: 'logs',
  props: {
    title: 'Logs',
    isFocused: false,
    bodyColumns: 60,
    placement: 'dock',
    scroll: { offset: 0, bodyRows: 30 },
    view: {},
  },
} as const

test('a notification row leaves the conversation and lands in the Logs pane', async ($, on) => {
  on('ui.open', () => ({ value: { isPlaced: true } }))
  on('clock.now', () => ({ value: 1795700000000 }))
  on('ui.render', ($, e) => {
    const { Text } = $.ui.resolve(e)
    return <Text>engine row</Text>
  })

  on('prompt.submit', (_, e) => ({ text: e.text }))
  await $.prompt.submit({ text: 'Agent "Plan GH #580" finished: GO', origin: { kind: 'task-notification' }, wait: false })

  for (const surface of ['vscode', 'desktop'] as const) {
    const row = await $.ui.mount({ ...NOTICE, requestId: `row-${surface}`, surface })
    expect(await row.find({ type: 'Text', text: /engine row/ })).toBeUndefined()
    await row.unmount()

    const pane = await $.ui.mount({ ...PANE, surface })
    expect(await pane.find({ type: 'Text', text: /^L1$/ })).toBeDefined()
    await pane.unmount()
  }
})

test("the person's own prompt stays in the conversation", async ($, on) => {
  on('ui.render', ($, e) => {
    const { Text } = $.ui.resolve(e)
    return <Text>engine row</Text>
  })
  const row = await $.ui.mount({
    ...NOTICE,
    requestId: 'row-person',
    surface: 'vscode',
    props: { text: 'status?', origin: { kind: 'composer' }, isExpanded: false },
  })
  expect((await row.find({ type: 'Text', text: /engine row/ }))?.text).toBe('engine row')
  await row.unmount()
})

test('a reply to the person links the log entries about the issues it names', async ($, on) => {
  on('clock.now', () => ({ value: 1795700000000 }))
  on('prompt.submit', (_, e) => ({ text: e.text }))
  on('ui.render', ($, e) => {
    const { Text } = $.ui.resolve(e)
    return <Text>engine row</Text>
  })
  await $.prompt.submit({ text: 'Agent "Plan GH #580" finished: GO', origin: { kind: 'task-notification' }, wait: false })
  const reply = await $.ui.mount({
    plugin: 'conv-logs',
    component: 'AssistantMessage',
    requestId: 'msg-1',
    surface: 'vscode',
    props: { text: 'The #580 fix is in review.', isFirstOfReply: true },
  })
  expect(await reply.find({ type: 'Text', text: /engine row/ })).toBeDefined()
  expect(await reply.find({ type: 'Button', key: 'to-L1' })).toBeDefined()
  await reply.unmount()
})

// An unrequested turn's assistant row is hidden from the conversation, by row id or by whole text block;
// a block of a reply to the person that is only a SUBSTRING of a hidden one stays visible.
const append = (uuid: string, ...texts: string[]) =>
  ({ uuid, message: { type: 'assistant', content: texts.map(text => ({ type: 'text', text })) } }) as never

for (const surface of ['vscode', 'desktop'] as const) {
  test(`an unrequested turn's blocks are hidden, a substring in a person turn is not (${surface})`, async ($, on) => {
    on('clock.now', () => ({ value: 1795700000000 }))
    on('prompt.submit', (_, e) => ({ text: e.text }))
    on('turn.start', (_, e) => ({ turnId: e.turnId }) as never)
    on('ui.render', ($, e) => {
      const { Text } = $.ui.resolve(e)
      return <Text>engine row</Text>
    })
    await $.prompt.submit({ text: 'Agent "Plan GH #580" finished: GO', origin: { kind: 'task-notification' }, wait: false })
    await $.turn.start({ text: '', turnId: 't1' } as never)
    await $.session.append(append('row-hidden', '  Pushed the branch and ran the whole gate.  ', ' Done. '))

    const reply = (requestId: string, text: string) =>
      $.ui.mount({ plugin: 'conv-logs', component: 'AssistantMessage', requestId, surface, props: { text, isFirstOfReply: true } })

    const byRow = await reply('row-hidden', 'anything')
    expect(await byRow.find({ type: 'Text', text: /engine row/ })).toBeUndefined()
    await byRow.unmount()

    const byBlock = await reply('row-other', '  Done.  ')
    expect(await byBlock.find({ type: 'Text', text: /engine row/ })).toBeUndefined()
    await byBlock.unmount()

    const sub = await reply('row-person', 'Pushed')
    expect((await sub.find({ type: 'Text', text: /engine row/ }))?.text).toBe('engine row')
    await sub.unmount()

    const plain = await reply('row-person-2', 'Yes.')
    expect((await plain.find({ type: 'Text', text: /engine row/ }))?.text).toBe('engine row')
    await plain.unmount()
  })
}
