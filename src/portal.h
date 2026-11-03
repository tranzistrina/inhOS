#pragma once
#include <Arduino.h>
const char portal[] PROGMEM = R"INHPORTAL(<!doctype html><html lang="ru"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>inhOS — загрузка</title>
<style>body{font:18px system-ui;max-width:650px;margin:30px auto;padding:16px;background:#eee;color:#222}button,input,select{font:inherit;margin:10px 0;max-width:100%}pre{white-space:pre-wrap}.notice{padding:12px;background:#fff}</style>
<h1>inhOS</h1><p id="build" class="notice">Определяю сборку…</p>
<label>Тип файла <select id="kind"><option value="book">Книга TXT / FB2</option><option value="app">Приложение .inh</option><option value="image">Фото .ginh</option></select></label><br>
<input type="file" id="file"><p>В выборе файла доступны все расширения. Для приложений выберите исходный файл .inh, не переименовывайте его в .txt.</p>
<label>Кодировка книги <select id="encoding"><option>utf-8</option><option>windows-1251</option><option>utf-16le</option></select></label><br>
<button id="send">Загрузить</button><pre id="result" role="status"></pre>
<h2>Управление файлами</h2><button id="refreshFiles">Обновить список</button><br><select id="deleteName"></select><button id="deleteOne">Удалить выбранный</button><button id="deleteAll">Удалить всё</button>
<script>
'use strict';
const ui={file:document.getElementById('file'),kind:document.getElementById('kind'),encoding:document.getElementById('encoding'),result:document.getElementById('result'),send:document.getElementById('send'),build:document.getElementById('build')};
let capabilities=null;
function classify(name){if(/\.ginh$/i.test(name))return 'image';if(/\.inh$/i.test(name))return 'app';if(/\.(txt|fb2)$/i.test(name))return 'book';throw Error('Поддерживаются только TXT, FB2, INH и GINH. Проверьте настоящее расширение файла.');}
function uploadName(name,kind){if(kind==='book')return 'book.txt';const ext=kind==='image'?'.ginh':'.inh';let stem=name.slice(0,-ext.length).replace(/[^a-zA-Z0-9_-]/g,'_').slice(0,36-ext.length);return (stem||'file')+ext;}
function normalize(text){return text.replace(/^\uFEFF/,'').replace(/\r\n?/g,'\n');}
function crc32(bytes){let crc=0xffffffff;for(const b of bytes){crc^=b;for(let k=0;k<8;k++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}return (crc^0xffffffff)>>>0;}
function hex8(n){return n.toString(16).padStart(8,'0');}
function fb2Book(text){
 if(/<!DOCTYPE|<!ENTITY/i.test(text))throw Error('DTD и сущности в FB2 запрещены.');
 const xml=new DOMParser().parseFromString(text,'application/xml');
 if(xml.getElementsByTagNameNS('*','parsererror').length)throw Error('Файл FB2 содержит ошибку XML.');
 const root=xml.documentElement;if(!root||root.localName!=='FictionBook')throw Error('Это не FictionBook/FB2.');
 const children=e=>Array.from(e.childNodes||[]).filter(c=>c.nodeType===1);
 const bodies=children(root).filter(e=>e.localName==='body');if(!bodies.length)throw Error('В FB2 отсутствует текст книги.');
 const encoder=new TextEncoder(),parts=[],entries=[];let size=0;
 function line(text){const b=encoder.encode(normalize(text)+'\n');parts.push(b);size+=b.length;if(size>3145728)throw Error('Книга превышает 3 МиБ.');}
 for(const body of bodies){const stack=[[body,0]];while(stack.length){const [e,depth]=stack.pop(),tag=e.localName;
  if(tag==='section'){
   const title=children(e).find(c=>c.localName==='title');
   const label=title?title.textContent.replace(/\s+/gu,' ').trim():'';
   if(label){entries.push([size,Math.min(depth,15),label]);if(entries.length>1024)throw Error('В книге более 1024 разделов.');}
   for(const c of children(e).reverse())stack.push([c,c.localName==='section'?depth+1:depth]);
  }else if(tag==='p'||tag==='v'||tag==='subtitle')line(e.textContent);
  else if(tag==='empty-line')line('');
  else if(tag!=='binary'&&tag!=='description'&&tag!=='image')for(const c of children(e).reverse())stack.push([c,depth]);
 }}
 if(!size)throw Error('В FB2 отсутствует текст книги.');
 const bodyBytes=new Uint8Array(size);let at=0;for(const b of parts){bodyBytes.set(b,at);at+=b.length;}
 let table='';
 for(const [offset,depth,title]of entries){let label='',length=0;for(const c of title.replace(/[\x00-\x1f\x7f]/g,'')){const n=encoder.encode(c).length;if(length+n>192)break;label+=c;length+=n;}if(label)table+=hex8(offset)+'\t'+depth.toString(16).padStart(2,'0')+'\t'+label+'\n';}
 const tableBytes=encoder.encode(table);if(tableBytes.length>65536)throw Error('Оглавление превышает 64 КиБ.');
 const start=46+tableBytes.length;
 const header=encoder.encode('INHBOOK/1\n'+hex8(start)+' '+hex8(size)+' '+hex8(crc32(bodyBytes))+' '+hex8(crc32(tableBytes))+'\n');
 const output=new Uint8Array(start+size);output.set(header);output.set(tableBytes,46);output.set(bodyBytes,start);return output;
}
function decodeBook(bytes,isFb2){let encoding=ui.encoding.value;
 if(isFb2){const head=String.fromCharCode(...bytes.slice(0,256));const m=head.match(/encoding\s*=\s*["']([^"']+)["']/i);
  if(bytes[0]===0xff&&bytes[1]===0xfe||bytes[0]===0x3c&&bytes[1]===0)encoding='utf-16le';
  else if(bytes[0]===0xfe&&bytes[1]===0xff||bytes[0]===0&&bytes[1]===0x3c)encoding='utf-16be';
  else if(bytes[0]===0xef&&bytes[1]===0xbb&&bytes[2]===0xbf)encoding='utf-8';
  else if(m)encoding=m[1];
 }return new TextDecoder(encoding,{fatal:true}).decode(bytes);
}
async function info(){const r=await fetch('/info',{cache:'no-store'});if(!r.ok)throw Error('Не удалось определить сборку. Откройте страницу заново.');capabilities=await r.json();ui.build.textContent='Сборка: '+capabilities.edition+' · '+capabilities.version+(capabilities.apps?' — доступны книги и приложения.':' — только книги. Для .inh нужна прошивка Apps той же панели.');return capabilities;}
function bytesOf(file){if(file.arrayBuffer)return file.arrayBuffer();return new Promise((resolve,reject)=>{const r=new FileReader();r.onload=()=>resolve(r.result);r.onerror=()=>reject(Error('Не удалось прочитать файл'));r.readAsArrayBuffer(file);});}
async function api(path,args){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(args)});const text=await r.text();if(!r.ok)throw Error(text||('Ошибка загрузки HTTP '+r.status));return text;}
ui.file.onchange=()=>{try{if(ui.file.files[0])ui.kind.value=classify(ui.file.files[0].name);ui.result.textContent='';}catch(e){ui.result.textContent=e.message;}};
ui.send.onclick=async()=>{ui.send.disabled=true;try{const f=ui.file.files[0];if(!f)throw Error('Выберите файл.');const kind=classify(f.name);if(kind!==ui.kind.value)throw Error('Выбранный тип не совпадает с расширением файла.');const cfg=await info();if(kind==='app'&&!cfg.apps)throw Error('На устройстве Reader. Установите Apps для вашей панели: иначе приложения не запустятся.');if(typeof TextDecoder==='undefined'||typeof TextEncoder==='undefined')throw Error('Откройте http://192.168.4.1 в полном браузере Chrome, Firefox или Safari.');let bytes;if(kind==='image'){bytes=new Uint8Array(await bytesOf(f));if(bytes.length!==3833)throw Error('GINH должен содержать 3833 байта. Используйте конвертер tools/ginh.py.');}else{const raw=new Uint8Array(await bytesOf(f));const fb2=/\.fb2$/i.test(f.name);let text=kind==='app'?new TextDecoder('utf-8',{fatal:true}).decode(raw):decodeBook(raw,fb2);bytes=fb2?fb2Book(text):new TextEncoder().encode(normalize(text));}const limit=kind==='app'?cfg.max_app_bytes:kind==='image'?cfg.max_image_bytes:cfg.max_book_bytes;if(!bytes.length||bytes.length>limit)throw Error('Размер файла должен быть от 1 до '+limit+' байт.');await api('/begin',{name:uploadName(f.name,kind),size:bytes.length});for(let i=0;i<bytes.length;i+=2048){const part=bytes.slice(i,i+2048);await api('/chunk',{offset:i,data:btoa(String.fromCharCode(...part))});ui.result.textContent=Math.floor(100*(i+part.length)/bytes.length)+'%';}ui.result.textContent=await api('/finish',{});}catch(e){ui.result.textContent=e.message;}finally{ui.send.disabled=false;}};
async function refreshFiles(){const r=await fetch('/files',{cache:'no-store'});if(!r.ok)throw Error('Ошибка списка файлов');const files=await r.json();const select=document.getElementById('deleteName');select.replaceChildren();for(const f of files){const option=document.createElement('option');option.value=f.name;option.textContent=f.name+' ('+f.size+' байт)';select.appendChild(option);}}
async function deleteFiles(all){try{const name=all?'*':document.getElementById('deleteName').value;if(!name)throw Error('Выберите файл');if(!confirm(all?'Удалить ВСЕ приложения, фото и книгу?':'Удалить '+name+'?'))return;ui.result.textContent=await api('/delete',{name,confirm:'delete'});await refreshFiles();}catch(e){ui.result.textContent=e.message;}}
document.getElementById('refreshFiles').onclick=()=>refreshFiles().catch(e=>ui.result.textContent=e.message);
document.getElementById('deleteOne').onclick=()=>deleteFiles(false);
document.getElementById('deleteAll').onclick=()=>deleteFiles(true);
info().catch(e=>{ui.build.textContent=e.message;});
</script></html>
)INHPORTAL";
