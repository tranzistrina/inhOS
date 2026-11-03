// XML parser is a controlled DOM double populated from Python's real XML parser.
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const root=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
const elements=new Proxy({}, {get:(o,k)=>o[k]||(o[k]={})});
const script=fs.readFileSync('src/portal.h','utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
const context=vm.createContext({document:{getElementById:k=>elements[k]},TextEncoder,TextDecoder,DOMParser:class{parseFromString(){return {documentElement:root,getElementsByTagNameNS(){return [];}};}},fetch:async()=>({ok:true,json:async()=>({edition:'Apps',version:'test',apps:true})})});
vm.runInContext(script,context);
const bytes=vm.runInContext('fb2Book("fixture")',context);
assert.deepEqual(Buffer.from(bytes),fs.readFileSync(process.argv[3]));
// Declaration and BOM detection must override the default UTF-8 selector.
context.sample=Uint8Array.from(Buffer.from('<?xml version="1.0" encoding="windows-1251"?><x>','ascii').subarray());
elements.encoding.value='utf-8';
assert.match(vm.runInContext('decodeBook(sample,true)',context),/windows-1251/);
console.log('PASS: portal/Python FB2 envelope byte parity, UTF-8 offsets/CRC/title depth, encoding declaration');
