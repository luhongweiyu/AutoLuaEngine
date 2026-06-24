const vscode = require('vscode');
const childProcess = require('child_process');
const http = require('http');

let outputChannel;
let lastLogId = 0;
let statusItems = [];

function activate(context) {
  outputChannel = vscode.window.createOutputChannel('AutoLuaEngine');
  statusItems = createStatusBarItems();

  context.subscriptions.push(
    vscode.commands.registerCommand('autolua.checkConnection', checkConnection),
    vscode.commands.registerCommand('autolua.runCurrentLua', runCurrentLua),
    vscode.commands.registerCommand('autolua.stopScript', stopScript),
    vscode.commands.registerCommand('autolua.drainLogs', drainLogs),
    outputChannel,
    ...statusItems
  );

  updateStatusBarItems();
  context.subscriptions.push(
    vscode.window.onDidChangeActiveTextEditor(updateStatusBarItems),
    vscode.workspace.onDidOpenTextDocument(updateStatusBarItems),
    vscode.workspace.onDidCloseTextDocument(updateStatusBarItems)
  );
}

function deactivate() {
}

function createStatusBarItems() {
  const checkItem = createStatusBarItem('$(plug) AutoLua', 'autolua.checkConnection', 'Check AutoLuaEngine connection', 100);
  const runItem = createStatusBarItem('$(play) Run Lua', 'autolua.runCurrentLua', 'Send current Lua file to AutoLuaEngine', 99);
  const stopItem = createStatusBarItem('$(debug-stop) Stop', 'autolua.stopScript', 'Request current script stop', 98);
  const logsItem = createStatusBarItem('$(output) Logs', 'autolua.drainLogs', 'Drain AutoLuaEngine logs', 97);
  return [checkItem, runItem, stopItem, logsItem];
}

function createStatusBarItem(text, command, tooltip, priority) {
  const item = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, priority);
  item.text = text;
  item.command = command;
  item.tooltip = tooltip;
  item.show();
  return item;
}

function updateStatusBarItems() {
  const editor = vscode.window.activeTextEditor;
  const canRun = Boolean(editor && editor.document && editor.document.getText().trim());

  for (const item of statusItems) {
    item.show();
  }

  const runItem = statusItems[1];
  if (runItem) {
    runItem.text = canRun ? '$(play) Run Lua' : '$(circle-slash) Run Lua';
    runItem.tooltip = canRun
      ? 'Send current Lua file to AutoLuaEngine'
      : 'Open a Lua script file before running';
  }
}

async function checkConnection() {
  outputChannel.show(true);
  outputChannel.appendLine('[connection] checking');

  try {
    const connection = readConnectionConfig();
    outputChannel.appendLine(`[connection] ${connection.host}:${connection.port}`);
    const result = await callJsonRpc('device.info', {});
    outputChannel.appendLine(`[connection] ${JSON.stringify(result)}`);
    vscode.window.showInformationMessage(`AutoLuaEngine connected: ${result.platform}`);
  } catch (error) {
    outputChannel.appendLine(`[error] ${error.message}`);
    vscode.window.showErrorMessage(`AutoLuaEngine connection failed: ${error.message}`);
  }
}

async function runCurrentLua() {
  const editor = vscode.window.activeTextEditor;
  if (!editor) {
    vscode.window.showWarningMessage('No active Lua file.');
    return;
  }

  const document = editor.document;
  const code = document.getText();
  if (!code.trim()) {
    vscode.window.showWarningMessage('Current file is empty.');
    return;
  }

  outputChannel.show(true);
  outputChannel.appendLine(`[run] ${document.fileName}`);

  try {
    const startLogId = await getLastLogId();
    const result = await callJsonRpc('script.run', {
      language: 'lua',
      code
    });
    outputChannel.appendLine(`[result] ${JSON.stringify(result)}`);

    await drainLogsAfter(startLogId);
  } catch (error) {
    outputChannel.appendLine(`[error] ${error.message}`);
    vscode.window.showErrorMessage(`AutoLuaEngine run failed: ${error.message}`);
  }
}

async function stopScript() {
  outputChannel.show(true);
  outputChannel.appendLine('[stop] requesting script stop');

  try {
    const result = await callJsonRpc('script.stop', {});
    outputChannel.appendLine(`[stop] ${JSON.stringify(result)}`);
    vscode.window.showInformationMessage('AutoLuaEngine stop requested.');
  } catch (error) {
    outputChannel.appendLine(`[error] ${error.message}`);
    vscode.window.showErrorMessage(`AutoLuaEngine stop failed: ${error.message}`);
  }
}

async function drainLogs() {
  outputChannel.show(true);

  try {
    await drainLogsAfter(lastLogId);
  } catch (error) {
    outputChannel.appendLine(`[error] ${error.message}`);
    vscode.window.showErrorMessage(`AutoLuaEngine log drain failed: ${error.message}`);
  }
}

async function drainLogsAfter(afterId) {
  const result = await callJsonRpc('log.drain', { afterId });
  lastLogId = result.lastId || afterId;

  for (const entry of result.entries || []) {
    outputChannel.appendLine(`[log#${entry.id}] ${entry.message}`);
  }

  if (!result.entries || result.entries.length === 0) {
    outputChannel.appendLine('[log] no new entries');
  }
}

async function getLastLogId() {
  const result = await callJsonRpc('log.drain', { afterId: 0 });
  return result.lastId || 0;
}

async function callJsonRpc(method, params) {
  const connection = readConnectionConfig();

  if (connection.useAdbForward) {
    await ensureAdbForward(connection.adbPath, connection.port, connection.remotePort);
  }

  const response = await postJson(connection.host, connection.port, {
    jsonrpc: '2.0',
    id: 1,
    method,
    params
  });

  if (response.error) {
    throw new Error(response.error.message || 'JSON-RPC error');
  }

  return response.result;
}

function readConnectionConfig() {
  const config = vscode.workspace.getConfiguration('autolua');
  return {
    adbPath: config.get('adbPath'),
    host: config.get('host') || '127.0.0.1',
    port: config.get('port') || 18380,
    useAdbForward: config.get('useAdbForward') !== false,
    remotePort: config.get('remotePort') || config.get('port') || 18380
  };
}

function ensureAdbForward(adbPath, localPort, remotePort) {
  return new Promise((resolve, reject) => {
    childProcess.execFile(
      adbPath,
      ['forward', `tcp:${localPort}`, `tcp:${remotePort}`],
      { windowsHide: true },
      (error) => {
        if (error) {
          reject(error);
          return;
        }
        resolve();
      }
    );
  });
}

function postJson(host, port, body) {
  return new Promise((resolve, reject) => {
    const text = JSON.stringify(body);
    const request = http.request({
      hostname: host,
      port,
      path: '/jsonrpc',
      method: 'POST',
      headers: {
        'Content-Type': 'application/json; charset=utf-8',
        'Content-Length': Buffer.byteLength(text)
      }
    }, (response) => {
      let responseText = '';
      response.setEncoding('utf8');
      response.on('data', (chunk) => {
        responseText += chunk;
      });
      response.on('end', () => {
        try {
          resolve(JSON.parse(responseText));
        } catch (error) {
          reject(new Error(`Invalid JSON response: ${responseText}`));
        }
      });
    });

    request.on('error', reject);
    request.write(text);
    request.end();
  });
}

module.exports = {
  activate,
  deactivate
};
