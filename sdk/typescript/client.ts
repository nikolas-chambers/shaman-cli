// Client for the shaman HTTP API (`shaman serve`). Works in browsers, Node 18+, Bun and Deno.
//
//   const sh = new Shaman("http://127.0.0.1:4096")
//   const s = await sh.createSession()
//   for await (const ev of sh.prompt(s.id, "summarise README.md")) {
//     if (ev.type === "text") process.stdout.write(ev.text)
//     if (ev.type === "permission") await sh.replyPermission(ev.id, "once")
//   }
export type ShamanEvent = { type: string; session: string; [key: string]: any }

export class Shaman {
  constructor(public url = "http://127.0.0.1:4096", public token?: string) {
    this.url = url.replace(/\/+$/, "")
  }

  private async req(method: string, path: string, body?: unknown): Promise<Response> {
    const headers: Record<string, string> = { "Content-Type": "application/json" }
    if (this.token) headers.Authorization = `Bearer ${this.token}`
    const res = await fetch(this.url + path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) })
    if (!res.ok && !res.headers.get("content-type")?.includes("event-stream")) throw new Error(`${method} ${path}: HTTP ${res.status}`)
    return res
  }
  private json = async (method: string, path: string, body?: unknown) => (await this.req(method, path, body)).json()

  health() { return this.json("GET", "/health") }
  models() { return this.json("GET", "/models") }
  agents() { return this.json("GET", "/agents") }
  sessions() { return this.json("GET", "/session") }
  session(id: string) { return this.json("GET", `/session/${id}`) }
  messages(id: string) { return this.json("GET", `/session/${id}/message`) }
  createSession(opts: { agent?: string; model?: string } = {}) { return this.json("POST", "/session", opts) }
  deleteSession(id: string) { return this.json("DELETE", `/session/${id}`) }
  abort(id: string) { return this.json("POST", `/session/${id}/abort`, {}) }
  undo(id: string) { return this.json("POST", `/session/${id}/undo`, {}) }
  compact(id: string) { return this.json("POST", `/session/${id}/compact`, {}) }
  replyPermission(id: string, reply: "once" | "always" | "reject") { return this.json("POST", `/permission/${id}`, { reply }) }

  async *prompt(id: string, text: string, opts: { model?: string; agent?: string; files?: string[] } = {}): AsyncGenerator<ShamanEvent> {
    const res = await this.req("POST", `/session/${id}/message`, { text, ...opts })
    const reader = res.body!.getReader()
    const decoder = new TextDecoder()
    let buf = ""
    for (;;) {
      const { value, done } = await reader.read()
      if (done) return
      buf += decoder.decode(value, { stream: true })
      let i: number
      while ((i = buf.indexOf("\n\n")) >= 0) {
        const raw = buf.slice(0, i)
        buf = buf.slice(i + 2)
        let type = "message"
        const data: string[] = []
        for (const line of raw.split("\n")) {
          if (line.startsWith("event:")) type = line.slice(6).trim()
          else if (line.startsWith("data:")) data.push(line.slice(5).replace(/^ /, ""))
        }
        if (data.length) yield { ...JSON.parse(data.join("\n")), type }
      }
    }
  }
}
