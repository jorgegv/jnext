/** One section of a status report. */
export type StatusSection = { title: string; body: string }

/** One status report, as the pane draws it. */
export type StatusReport = {
  /** Milliseconds since the epoch when it was gathered. */
  at: number
  /** The report's sections in the owner's status layout: a title and a markdown body ('' = nothing there). */
  sections: StatusSection[]
  /** Sources that could not be read this time, one line each. */
  errors: string[]
}

declare module 'claude-code' {
  interface PluginState {
    'run-status': {
      report: Shaped<StatusReport | null>
      /** True while a refresh runs. */
      isBusy: boolean
    }
  }
}
