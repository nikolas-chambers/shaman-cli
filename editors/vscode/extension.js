// Shaman for VS Code: runs `shaman serve` for the workspace and shows its web UI in a sidebar view.
// Plain JavaScript, no build step: `npx @vscode/vsce package` in this folder makes a .vsix.
const vscode = require("vscode");
const cp = require("child_process");
const crypto = require("crypto");

let server = null;       // { proc, url, token }
let starting = null;     // Promise of the running server
let view = null;         // the webview view, once resolved
const pending = [];      // messages for the chat UI before it is ready

function workspaceDir() {
  const folder = vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders[0];
  return folder ? folder.uri.fsPath : process.cwd();
}

function startServer(output) {
  if (starting) return starting;
  const cfg = vscode.workspace.getConfiguration("shaman");
  const token = crypto.randomBytes(24).toString("hex");
  const args = ["serve", "--port", "0", "--token", token, ...cfg.get("extraArgs", [])];
  starting = new Promise((resolve, reject) => {
    const proc = cp.spawn(cfg.get("path", "shaman"), args, { cwd: workspaceDir(), env: process.env });
    let seen = "";
    const timer = setTimeout(() => reject(new Error("shaman did not start within 15 s")), 15000);
    proc.stdout.on("data", (chunk) => {
      const text = chunk.toString();
      output.append(text);
      seen += text;
      const m = seen.match(/listening on (http:\/\/\S+)/);
      if (m && !server) {
        clearTimeout(timer);
        server = { proc, url: m[1], token };
        resolve(server);
      }
    });
    proc.stderr.on("data", (chunk) => output.append(chunk.toString()));
    proc.on("error", (err) => {
      clearTimeout(timer);
      reject(new Error(`cannot run '${cfg.get("path", "shaman")}': ${err.message}. Install shaman or set shaman.path.`));
    });
    proc.on("exit", (code) => {
      output.appendLine(`shaman exited (${code})`);
      server = null;
      starting = null;
    });
  });
  starting.catch(() => { starting = null; });
  return starting;
}

function stopServer() {
  if (server) server.proc.kill();
  server = null;
  starting = null;
}

// Send text to the chat input (the page listens for this message).
function sendToChat(text) {
  if (view && view.visible) view.webview.postMessage({ type: "shaman.insert", text });
  else {
    pending.push(text);
    vscode.commands.executeCommand("shaman.chat.focus");
  }
}

class ChatView {
  constructor(context, output) {
    this.context = context;
    this.output = output;
  }

  async resolveWebviewView(webviewView) {
    view = webviewView;
    webviewView.webview.options = { enableScripts: true };
    webviewView.webview.html = this.message("Starting shaman...");
    try {
      const s = await startServer(this.output);
      // Remote workspaces (SSH, containers, Codespaces) need the port forwarded to the UI side.
      const external = await vscode.env.asExternalUri(vscode.Uri.parse(s.url));
      webviewView.webview.html = this.frame(external.toString(true), s.token);
    } catch (err) {
      webviewView.webview.html = this.message(String(err.message || err));
    }
    webviewView.webview.onDidReceiveMessage((msg) => {
      if (msg && msg.type === "ready") while (pending.length) webviewView.webview.postMessage({ type: "shaman.insert", text: pending.shift() });
    });
    webviewView.onDidChangeVisibility(() => {
      if (webviewView.visible) while (pending.length) webviewView.webview.postMessage({ type: "shaman.insert", text: pending.shift() });
    });
  }

  message(text) {
    return `<!doctype html><body style="font-family:var(--vscode-font-family);color:var(--vscode-foreground);padding:12px">${text
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")}</body>`;
  }

  frame(url, token) {
    const origin = new URL(url).origin;
    const src = `${url.replace(/\/$/, "")}/?token=${token}&embed=vscode`;
    return `<!doctype html>
<html><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; frame-src ${origin}; script-src 'unsafe-inline'; style-src 'unsafe-inline';">
<style>html,body,iframe{margin:0;padding:0;width:100%;height:100%;border:0;overflow:hidden}</style></head>
<body><iframe id="chat" src="${src}" allow="clipboard-read; clipboard-write"></iframe>
<script>
  const vscode = acquireVsCodeApi();
  const chat = document.getElementById("chat");
  window.addEventListener("message", (e) => {
    if (e.data && e.data.type === "shaman.insert") chat.contentWindow.postMessage(e.data, "${origin}");
  });
  chat.addEventListener("load", () => vscode.postMessage({ type: "ready" }));
</script></body></html>`;
  }
}

function activate(context) {
  const output = vscode.window.createOutputChannel("Shaman");
  const provider = new ChatView(context, output);
  context.subscriptions.push(
    output,
    vscode.window.registerWebviewViewProvider("shaman.chat", provider, { webviewOptions: { retainContextWhenHidden: true } }),
    vscode.commands.registerCommand("shaman.open", () => vscode.commands.executeCommand("shaman.chat.focus")),
    vscode.commands.registerCommand("shaman.askSelection", () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.selection.isEmpty) return vscode.window.showInformationMessage("Select some code first.");
      const rel = vscode.workspace.asRelativePath(editor.document.uri);
      const start = editor.selection.start.line + 1, end = editor.selection.end.line + 1;
      const lang = editor.document.languageId;
      const code = editor.document.getText(editor.selection);
      sendToChat(`In ${rel} (lines ${start}-${end}):\n\`\`\`${lang}\n${code}\n\`\`\`\n`);
    }),
    vscode.commands.registerCommand("shaman.addFile", () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor) return;
      sendToChat(`@${vscode.workspace.asRelativePath(editor.document.uri)} `);
    }),
    vscode.commands.registerCommand("shaman.openInBrowser", async () => {
      const s = await startServer(output);
      vscode.env.openExternal(await vscode.env.asExternalUri(vscode.Uri.parse(`${s.url}/?token=${s.token}`)));
    }),
    vscode.commands.registerCommand("shaman.restart", async () => {
      stopServer();
      if (view) provider.resolveWebviewView(view);
    }),
    { dispose: stopServer }
  );
}

function deactivate() {
  stopServer();
}

module.exports = { activate, deactivate };
