/** One entry of the Logs pane: a notification or message that arrived unasked, or Claude's reply to one. */
export type LogEntry = {
  /** `L<n>`, stable for the session. */
  id: string
  /** Milliseconds since the epoch. */
  at: number
  /** `notice` (a task notification or another agent's message) or `reply` (Claude's answer to it). */
  kind: 'notice' | 'reply'
  /** Who it came from: the agent or task name, the origin kind, or `Claude`. */
  from: string
  /** The text, markdown. */
  text: string
  /** GitHub issue numbers the text mentions (`#577` -> 577). */
  issues: number[]
  /** The transcript row it came from, when known, to scroll back to it. */
  rowId?: string
}

/** What the mod hides from the conversation: rows of unrequested turns. */
export type Hidden = {
  /** Message row ids (session.append uuids) of assistant rows in unrequested turns. */
  rows: string[]
  /** Text blocks of those rows, for surfaces whose render id is not the row id. */
  texts: string[]
  /** tool_use_ids of tool calls made in unrequested turns. */
  tools: string[]
}

declare module 'claude-code' {
  interface PluginState {
    'conv-logs': {
      entries: Shaped<LogEntry[]>
      hidden: Shaped<Hidden>
      /** True: unrequested rows are hidden from the conversation (the split). */
      isSplit: boolean
      /** Number of entries the person has seen (opened the pane at). */
      seen: number
    }
  }
}
