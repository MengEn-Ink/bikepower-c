const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.resolve(__dirname,'../tools/web/vendor/esp-web-tools-10.4.0');
const upstream=path.resolve(__dirname,'web-flash/node_modules/esp-web-tools');
test('locked same-origin upstream snapshot is exact and dependency graph is closed',()=>{
 const pkg=JSON.parse(fs.readFileSync(path.join(upstream,'package.json'),'utf8'));
 assert.equal(pkg.version,'10.4.0');
 const names=fs.readdirSync(path.join(upstream,'dist/web')).filter(n=>n.endsWith('.js'));
 assert.ok(names.includes('install-dialog-im156JnI.js'));
 for(const name of names){
  const data=fs.readFileSync(path.join(root,name));
  assert.deepEqual(data,fs.readFileSync(path.join(upstream,'dist/web',name)),name);
  const code=data.toString('utf8');
  for(const match of code.matchAll(/(?:from\s*|import\s*\()(["'])(\.\/[^"']+\.js)\1/g)){
   assert.ok(names.includes(path.basename(match[2])),name+' -> '+match[2]);
  }
 }
 assert.deepEqual(fs.readFileSync(path.join(root,'LICENSE')),fs.readFileSync(path.join(upstream,'LICENSE')));
 const session=fs.readFileSync(path.resolve(__dirname,'../tools/web/flash-session.js'),'utf8');
 assert.ok(session.includes('./vendor/esp-web-tools-10.4.0/install-dialog-im156JnI.js'));
 assert.ok(!session.includes('https://unpkg.com'));
});
