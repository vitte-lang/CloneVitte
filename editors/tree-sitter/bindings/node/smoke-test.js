const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const Parser = require('tree-sitter');
const language = require('../..');
const parser = new Parser();
parser.setLanguage(language);
const tree = parser.parse('proc main() { let value: i32 = 42; }');
assert.equal(tree.rootNode.hasError, false);
assert.ok(language.nodeTypeInfo.length > 0);
assert.equal(parser.parse('form Buffer<T: Copy + Eq, const N: usize> { value: ?T } proc main() { consume(Result<i32>::Ready,); }').rootNode.hasError, false);
for (const name of ['highlights', 'injections']) {
  const query = new Parser.Query(language, fs.readFileSync(path.join(__dirname, '../../queries', name + '.scm'), 'utf8'));
  query.captures(tree.rootNode);
}
console.log('Node binding and queries OK');
