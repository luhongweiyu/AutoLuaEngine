const vscode = require('vscode');
const childProcess = require('child_process');
const http = require('http');

let outputChannel;
let lastLogId = 0;
let statusItems = [];

function activate(context) {
  outputChannel = vscode.window.createOutputChannel('小鱼精灵');
  statusItems = createStatusBarItems();

  context.subscriptions.push(
    vscode.commands.registerCommand('xiaoyv.checkConnection', checkConnection),
    vscode.commands.registerCommand('xiaoyv.runCurrentLua', runCurrentLua),
    vscode.commands.registerCommand('xiaoyv.pauseScript', pauseScript),
    vscode.commands.registerCommand('xiaoyv.resumeScript', resumeScript),
    vscode.commands.registerCommand('xiaoyv.stopScript', stopScript),
    vscode.commands.registerCommand('xiaoyv.drainLogs', drainLogs),
    vscode.commands.registerCommand('xiaoyv.packageWorkspace', packageWorkspace),
    vscode.commands.registerCommand('xiaoyv.openColorTool', openColorTool),
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
  const checkItem = createStatusBarItem('$(plug) 小鱼精灵', 'xiaoyv.checkConnection', '检查小鱼精灵连接', 100);
  const runItem = createStatusBarItem('$(play) 运行 Lua', 'xiaoyv.runCurrentLua', '发送当前 Lua 文件到小鱼精灵运行', 99);
  const pauseItem = createStatusBarItem('$(debug-pause) 暂停', 'xiaoyv.pauseScript', '请求暂停当前脚本', 98);
  const resumeItem = createStatusBarItem('$(debug-continue) 继续', 'xiaoyv.resumeScript', '继续已暂停的脚本', 97);
  const stopItem = createStatusBarItem('$(debug-stop) 停止', 'xiaoyv.stopScript', '请求停止当前脚本', 96);
  const logsItem = createStatusBarItem('$(output) 日志', 'xiaoyv.drainLogs', '读取小鱼精灵日志', 95);
  const packageItem = createStatusBarItem('$(package) 打包', 'xiaoyv.packageWorkspace', '打包当前小鱼精灵脚本项目', 94);
  const colorToolItem = createStatusBarItem('$(preview) 抓图', 'xiaoyv.openColorTool', '打开小鱼抓图取色器', 93);
  return [checkItem, runItem, pauseItem, resumeItem, stopItem, logsItem, packageItem, colorToolItem];
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
    runItem.text = canRun ? '$(play) 运行 Lua' : '$(circle-slash) 运行 Lua';
    runItem.tooltip = canRun
      ? '发送当前 Lua 文件到小鱼精灵运行'
      : '请先打开一个 Lua 脚本文件';
  }
}

async function checkConnection() {
  outputChannel.show(true);
  outputChannel.appendLine('[连接] 正在检查');

  try {
    const connection = readConnectionConfig();
    outputChannel.appendLine(`[连接] ${connection.host}:${connection.port}`);
    const result = await callJsonRpc('device.info', {});
    outputChannel.appendLine(`[连接] ${JSON.stringify(result)}`);
    vscode.window.showInformationMessage(`小鱼精灵已连接：${result.platform}`);
  } catch (error) {
    outputChannel.appendLine(`[错误] ${error.message}`);
    vscode.window.showErrorMessage(`小鱼精灵连接失败：${error.message}`);
  }
}

async function runCurrentLua() {
  const editor = vscode.window.activeTextEditor;
  if (!editor) {
    vscode.window.showWarningMessage('没有打开的 Lua 文件。');
    return;
  }

  const document = editor.document;
  const code = document.getText();
  if (!code.trim()) {
    vscode.window.showWarningMessage('当前文件为空。');
    return;
  }

  outputChannel.show(true);
  outputChannel.appendLine(`[运行] ${document.fileName}`);

  try {
    const startLogId = await getLastLogId();
    const result = await callJsonRpc('script.run', {
      language: 'lua',
      code
    });
    outputChannel.appendLine(`[结果] ${JSON.stringify(result)}`);

    await drainLogsAfter(startLogId);
  } catch (error) {
    outputChannel.appendLine(`[错误] ${error.message}`);
    vscode.window.showErrorMessage(`小鱼精灵运行失败：${error.message}`);
  }
}

async function stopScript() {
  outputChannel.show(true);
  outputChannel.appendLine('[停止] 正在请求停止脚本');

  try {
    const result = await callJsonRpc('script.stop', {});
    outputChannel.appendLine(`[停止] ${JSON.stringify(result)}`);
    vscode.window.showInformationMessage('已请求停止小鱼精灵脚本。');
  } catch (error) {
    outputChannel.appendLine(`[错误] ${error.message}`);
    vscode.window.showErrorMessage(`小鱼精灵停止失败：${error.message}`);
  }
}

async function pauseScript() {
  await sendControlCommand('script.pause', '[暂停] 正在请求暂停脚本', '已请求暂停小鱼精灵脚本。', '暂停');
}

async function resumeScript() {
  await sendControlCommand('script.resume', '[继续] 正在请求继续脚本', '已请求继续小鱼精灵脚本。', '继续');
}

async function sendControlCommand(method, logLine, okMessage, logPrefix) {
  outputChannel.show(true);
  outputChannel.appendLine(logLine);

  try {
    const result = await callJsonRpc(method, {});
    outputChannel.appendLine(`[${logPrefix}] ${JSON.stringify(result)}`);
    vscode.window.showInformationMessage(okMessage);
  } catch (error) {
    outputChannel.appendLine(`[错误] ${error.message}`);
    vscode.window.showErrorMessage(`小鱼精灵${logPrefix}失败：${error.message}`);
  }
}

async function drainLogs() {
  outputChannel.show(true);

  try {
    await drainLogsAfter(lastLogId);
  } catch (error) {
    outputChannel.appendLine(`[错误] ${error.message}`);
    vscode.window.showErrorMessage(`小鱼精灵读取日志失败：${error.message}`);
  }
}

async function packageWorkspace() {
  const workspaceFolder = vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders[0];
  if (!workspaceFolder) {
    vscode.window.showWarningMessage('请先用 VS Code 打开脚本项目文件夹。');
    return;
  }

  const projectDirectory = workspaceFolder.uri.fsPath;
  const toolPath = resolvePackToolPath();
  outputChannel.show(true);
  outputChannel.appendLine(`[打包] ${projectDirectory}`);
  outputChannel.appendLine(`[打包] 工具：${toolPath}`);

  try {
    await ensurePackTool(toolPath);
    const result = await execFile(toolPath, [projectDirectory]);
    if (result.stdout) {
      outputChannel.appendLine(result.stdout.trim());
    }
    if (result.stderr) {
      outputChannel.appendLine(result.stderr.trim());
    }
    vscode.window.showInformationMessage('小鱼精灵 脚本包已生成到项目 dist 文件夹。');
  } catch (error) {
    outputChannel.appendLine(`[错误] ${error.message}`);
    vscode.window.showErrorMessage(`小鱼精灵 打包失败: ${error.message}`);
  }
}

/**
 * 启动独立 Qt 抓图取色器。
 *
 * Qt 工具会自行申请 ADB 动态端口或直接连接局域网设备，不复用也不删除 VSCode 的端口转发。
 */
async function openColorTool() {
  const toolPath = resolveDesktopToolPath();
  outputChannel.show(true);
  outputChannel.appendLine(`[抓图工具] ${toolPath}`);
  try {
    await ensureDesktopTool(toolPath);
    const connection = readConnectionConfig();
    const args = connection.useAdbForward
      ? [
          '--connection-mode', 'adb',
          '--adb', connection.adbPath,
          '--remote-port', String(connection.remotePort)
        ]
      : [
          '--connection-mode', 'lan',
          '--host', connection.host,
          '--port', String(connection.port)
        ];
    if (connection.useAdbForward && connection.deviceSerial) {
      args.push('--device', connection.deviceSerial);
    }
    const editor = vscode.window.activeTextEditor;
    if (editor && /\.(png|jpe?g|bmp|webp|gif|tiff?|ico)$/i.test(editor.document.fileName)) {
      args.push('--open', editor.document.fileName);
    }
    const process = childProcess.spawn(toolPath, args, {
      detached: true,
      stdio: 'ignore',
      windowsHide: false
    });
    process.unref();
  } catch (error) {
    outputChannel.appendLine(`[错误] ${error.message}`);
    vscode.window.showErrorMessage(`打开小鱼抓图取色器失败：${error.message}`);
  }
}

function resolveDesktopToolPath() {
  const configured = vscode.workspace.getConfiguration('xiaoyv').get('desktopToolPath');
  if (configured && String(configured).trim()) {
    return String(configured).trim();
  }
  const path = require('path');
  return path.resolve(__dirname, '..', '..', 'xiaoyv-tools', 'build', 'xiaoyv_tools.exe');
}

function ensureDesktopTool(toolPath) {
  const fs = require('fs');
  if (fs.existsSync(toolPath)) {
    return Promise.resolve();
  }
  const path = require('path');
  const buildScript = path.resolve(__dirname, '..', '..', 'xiaoyv-tools', '构建.ps1');
  return runPowerShellScript(buildScript, ['-Configuration', 'Release']);
}

function resolvePackToolPath() {
  const configPath = vscode.workspace.getConfiguration('xiaoyv').get('packToolPath');
  if (configPath && String(configPath).trim()) {
    return String(configPath).trim();
  }

  const path = require('path');
  return path.resolve(__dirname, '..', '..', '..', 'tools', 'pack', 'build', 'xiaoyv_pack.exe');
}

function ensurePackTool(toolPath) {
  const fs = require('fs');
  if (fs.existsSync(toolPath)) {
    return Promise.resolve();
  }

  const path = require('path');
  const buildScript = path.resolve(__dirname, '..', '..', '..', 'tools', 'pack', '构建打包器.ps1');
  return runPowerShellScript(buildScript, ['-Release']);
}

/**
 * 优先使用原生 UTF-8 的 PowerShell 7，确保中文脚本和构建日志不乱码。
 * 只有系统未安装 pwsh.exe 时才使用 Windows PowerShell，脚本执行失败不会被误判为缺少程序。
 */
async function runPowerShellScript(scriptPath, scriptArguments) {
  const commonArguments = ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', scriptPath];
  try {
    return await execFile('pwsh.exe', [...commonArguments, ...scriptArguments]);
  } catch (error) {
    if (error.code !== 'ENOENT') {
      throw error;
    }
    return execFile('powershell.exe', [...commonArguments, ...scriptArguments]);
  }
}

function execFile(file, args) {
  return new Promise((resolve, reject) => {
    childProcess.execFile(file, args, { windowsHide: true }, (error, stdout, stderr) => {
      if (error) {
        const detail = stderr || stdout || error.message;
        const failure = new Error(detail);
        failure.code = error.code;
        reject(failure);
        return;
      }
      resolve({ stdout: stdout || '', stderr: stderr || '' });
    });
  });
}

async function drainLogsAfter(afterId) {
  const result = await callJsonRpc('log.drain', { afterId });
  lastLogId = result.lastId || afterId;

  for (const entry of result.entries || []) {
    outputChannel.appendLine(`[log#${entry.id}] ${entry.message}`);
  }

  if (!result.entries || result.entries.length === 0) {
    outputChannel.appendLine('[日志] 没有新内容');
  }
}

async function getLastLogId() {
  const result = await callJsonRpc('log.drain', { afterId: 0 });
  return result.lastId || 0;
}

async function callJsonRpc(method, params) {
  const connection = readConnectionConfig();

  if (connection.useAdbForward) {
    await ensureAdbForward(
      connection.adbPath,
      connection.deviceSerial,
      connection.port,
      connection.remotePort
    );
  }

  const response = await postJson(connection.host, connection.port, {
    jsonrpc: '2.0',
    id: 1,
    method,
    params
  });

  if (response.error) {
    throw new Error(response.error.message || 'JSON-RPC 响应错误');
  }

  return response.result;
}

function readConnectionConfig() {
  const config = vscode.workspace.getConfiguration('xiaoyv');
  return {
    adbPath: config.get('adbPath'),
    deviceSerial: String(config.get('deviceSerial') || '').trim(),
    host: config.get('host') || '127.0.0.1',
    port: config.get('port') || 18380,
    useAdbForward: config.get('useAdbForward') === true,
    remotePort: config.get('remotePort') || config.get('port') || 18380
  };
}

function ensureAdbForward(adbPath, deviceSerial, localPort, remotePort) {
  return new Promise((resolve, reject) => {
    const arguments = [];
    if (deviceSerial) {
      arguments.push('-s', deviceSerial);
    }
    arguments.push('forward', `tcp:${localPort}`, `tcp:${remotePort}`);
    childProcess.execFile(
      adbPath,
      arguments,
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
          reject(new Error(`无效的 JSON 响应：${responseText}`));
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
