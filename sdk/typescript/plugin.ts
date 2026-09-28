// shaman plugin SDK for TypeScript / JavaScript (Node 18+, Bun, Deno). No dependencies.
//
//   import { Plugin } from "./plugin.ts"
//   const plugin = new Plugin("guard")
//   plugin.hook("tool.before", (ev) =>
//     ev.tool === "bash" && String(ev.input.command).includes("rm -rf") ? { block: "no" } : undefined)
//   plugin.tool("now", "Current time", { type: "object", properties: {} }, () => new Date().toISOString())
//   plugin.run()
//
// See sdk/python/shaman_plugin.py for the hook reference; the protocol is identical.
import * as readline from "node:readline"

type Json = any
type Hook = (params: Json) => Json | undefined | Promise<Json | undefined>
type ToolFn = (input: Json, ctx: Json) => string | { output: string; is_error?: boolean; title?: string } | Promise<any>

export class Plugin {
  private hooks = new Map<string, Hook>()
  private tools = new Map<string, { spec: Json; fn: ToolFn }>()
  root = ""
  constructor(public name: string) {}

  hook(name: string, fn: Hook) { this.hooks.set(name, fn); return this }
  onEvent(fn: Hook) { return this.hook("event", fn) }
  tool(name: string, description: string, parameters: Json, fn: ToolFn, permission?: string) {
    this.tools.set(name, { spec: { name, description, parameters, ...(permission ? { permission } : {}) }, fn })
    return this
  }
  log(...args: unknown[]) { console.error(...args) } // stdout is the protocol channel

  private reply(id: Json, result?: Json, error?: string) {
    const msg: Json = { jsonrpc: "2.0", id }
    if (error !== undefined) msg.error = { code: -32000, message: error }
    else msg.result = result ?? {}
    process.stdout.write(JSON.stringify(msg) + "\n")
  }

  private async dispatch(method: string, params: Json): Promise<Json> {
    if (method === "initialize") {
      this.root = params.root
      return { name: this.name, hooks: [...this.hooks.keys()], tools: [...this.tools.values()].map((t) => t.spec) }
    }
    if (method === "tool.call") {
      const t = this.tools.get(params.name)
      if (!t) throw new Error("unknown tool " + params.name)
      const r = await t.fn(params.input ?? {}, params)
      return typeof r === "object" && r !== null ? r : { output: String(r ?? "") }
    }
    const fn = this.hooks.get(method)
    return (fn ? await fn(params) : undefined) ?? {}
  }

  run() {
    const rl = readline.createInterface({ input: process.stdin })
    rl.on("line", async (line) => {
      let msg: Json
      try { msg = JSON.parse(line) } catch { return }
      if (msg.id === undefined) {
        if (msg.method === "event") await this.hooks.get("event")?.(msg.params)
        return
      }
      try { this.reply(msg.id, await this.dispatch(msg.method, msg.params ?? {})) }
      catch (e) { this.reply(msg.id, undefined, String((e as Error).message ?? e)) }
    })
  }
}
