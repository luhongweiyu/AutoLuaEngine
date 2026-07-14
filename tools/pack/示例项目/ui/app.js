/* 文件用途：验证 ALPKG 包内 HTML 能按相对路径加载外部 JavaScript，并与 Lua 通讯。 */
(function () {
  const result = document.getElementById('result');
  const sendButton = document.getElementById('send-message');

  if (!window.xiaoyv) {
    result.textContent = 'xiaoyv 网页桥未就绪';
    return;
  }

  window.xiaoyv.onMessage = function (data) {
    result.textContent = data && data.text ? data.text : 'Lua 返回了空消息';
  };

  sendButton.addEventListener('click', function () {
    window.xiaoyv.emit('hello', {});
  });
  result.textContent = '外部 JavaScript 已加载，等待 Lua 消息';
}());
