// Exercise the production page's confirmation and request flow without a device.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname,
  '../../main/boards/lckfb/szpi-esp32s3/wifi_file_page.h'), 'utf8');
const script = source.match(/<script>([\s\S]*?)<\/script>/)[1];

class Element {
  constructor(tag = '') { this.tag = tag; this.children = []; this.textContent = ''; this.disabled = false; }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
}
const elements = Object.fromEntries(['key', 'connect', 'root', 'up', 'mkdir', 'prev', 'next',
  'entries', 'files', 'send', 'result', 'path', 'notice'].map(id => [id, new Element()]));
elements.key.value = '012345';
const replies = [], calls = [], confirmations = [], prompts = [], messages = [];
const document = {
  getElementById: id => elements[id],
  createElement: tag => new Element(tag),
  querySelectorAll: () => Object.values(elements),
};
const context = {
  document, URLSearchParams,
  confirm: text => { messages.push(text); assert(confirmations.length); return confirmations.shift(); },
  prompt: () => { assert(prompts.length); return prompts.shift(); },
  fetch: async (url, options) => {
    assert(replies.length, 'Unexpected request: ' + url);
    assert.equal(options.headers['X-Lele-Key'], '012345');
    calls.push({url, options});
    const reply = replies.shift();
    return {ok: !reply.status, status: reply.status || 200,
      text: async () => reply.status ? reply.error : JSON.stringify(reply)};
  },
};
vm.runInNewContext(script, context);
const file = name => ({name, directory: false, bytes: 4});
const listing = (revision, names = ['已有.mp3']) => ({
  path: '', revision, next: null, limited: false, entries: names.map(file),
});
const action = label => {
  const buttons = elements.entries.children[0].children[2].children;
  return buttons.find(button => button.textContent === label);
};
const operation = () => calls.filter(call => call.url.startsWith('/manage?')).at(-1);

(async () => {
  for (const invalid of ['12345', '12345678', '12A456']) {
    elements.key.value = invalid;
    await elements.connect.onclick();
    assert.equal(calls.length, 0);
    assert.match(elements.result.textContent, /6-digit/);
  }
  elements.key.value = '012345';
  replies.push(listing(1));
  await elements.connect.onclick();
  assert.equal(elements.entries.children[0].children[0].textContent, '已有.mp3');

  // Declining replacement must not send any upload body.
  elements.files.files = [{name: '已有.mp3', size: 8}];
  replies.push({directory: false, bytes: 4, revision: 1}, listing(1));
  confirmations.push(false);
  await elements.send.onclick();
  assert(!calls.some(call => call.url.startsWith('/upload?')));
  assert.match(elements.result.textContent, /Skipped/);

  replies.push({directory: false, bytes: 4, revision: 1},
    {saved: true, bytes: 8, sha256: 'abc', backup_kept: false}, listing(2));
  confirmations.push(true);
  await elements.send.onclick();
  const upload = calls.find(call => call.url.startsWith('/upload?'));
  assert.match(upload.url, /replace=1/);
  assert.equal(upload.options.headers['X-Lele-Confirm'], 'yes');
  assert.equal(upload.options.headers['X-Lele-Revision'], '1');
  assert.equal(upload.options.body, elements.files.files[0]);
  assert.match(elements.result.textContent, /Saved/);

  prompts.push('新名字.mp3');
  replies.push({directory: false, bytes: 8, revision: 2}, {ok: true}, listing(3, ['新名字.mp3']));
  await action('Rename').onclick();
  let move = operation();
  let query = new URL(move.url, 'http://device').searchParams;
  assert.equal(query.get('op'), 'move');
  assert.equal(query.get('to'), '新名字.mp3');
  assert.equal(move.options.headers['X-Lele-Revision'], '2');

  // Declining deletion must not send a mutation.
  const before = calls.filter(call => call.url.startsWith('/manage?')).length;
  confirmations.push(false);
  replies.push({directory: false, bytes: 8, revision: 3});
  await action('Delete').onclick();
  assert.equal(calls.filter(call => call.url.startsWith('/manage?')).length, before);
  confirmations.push(true);
  replies.push({directory: false, bytes: 8, revision: 3}, {ok: true}, listing(4, []));
  await action('Delete').onclick();
  assert.equal(new URL(operation().url, 'http://device').searchParams.get('op'), 'delete');
  assert.equal(operation().options.headers['X-Lele-Confirm'], 'yes');

  prompts.push('新目录');
  replies.push({ok: true}, listing(5, ['原始.mp3']));
  await elements.mkdir.onclick();
  assert.equal(new URL(operation().url, 'http://device').searchParams.get('op'), 'mkdir');
  prompts.push('新目录/移动.mp3');
  replies.push({directory: false, bytes: 4, revision: 5}, {ok: true}, listing(6, []));
  await action('Move').onclick();
  query = new URL(operation().url, 'http://device').searchParams;
  assert.equal(query.get('path'), '原始.mp3');
  assert.equal(query.get('to'), '新目录/移动.mp3');

  // A stale revision is reported, never retried as a blind destructive request.
  replies.push(listing(6, ['stale.mp3']));
  await elements.connect.onclick();
  confirmations.push(true);
  replies.push({directory: false, bytes: 4, revision: 6},
    {status: 409, error: 'Directory changed; refresh first.'});
  const count = calls.length;
  await action('Delete').onclick();
  assert.equal(calls.length, count + 2);
  assert.match(elements.result.textContent, /409/);
  assert.equal(elements.send.disabled, false);
  assert.equal(replies.length, 0);
  assert.equal(confirmations.length, 0);
  assert.equal(prompts.length, 0);
  console.log('Production web page: browse, upload confirmations, rename/move/delete, mkdir and stale revision handling passed.');
})().catch(error => { console.error(error); process.exitCode = 1; });
