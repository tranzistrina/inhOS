#pragma once
#include <Arduino.h>
const char portal[] PROGMEM = R"INHPORTAL(<!doctype html><html lang="ru"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>inhOS — загрузка</title>
<style>body{font:18px system-ui;max-width:650px;margin:30px auto;padding:16px;background:#eee;color:#222}button,input,select{font:inherit;margin:10px 0;max-width:100%}pre{white-space:pre-wrap}.notice{padding:12px;background:#fff}</style>
<h1>inhOS</h1><p id="build" class="notice">Определяю сборку…</p>
<label>Тип файла <select id="kind"><option value="book">Книга TXT / FB2</option><option value="app">Приложение .inh</option></select></label><br>
<input type="file" id="file"><p>В выборе файла доступны все расширения. Для приложений выберите исходный файл .inh, не переименовывайте его в .txt.</p>
<label>Кодировка книги <select id="encoding"><option>utf-8</option><option>windows-1251</option><option>utf-16le</option></select></label><br>
<button id="send">Загрузить</button><pre id="result" role="status"></pre>
<script>
'use strict';
const ui={file:document.getElementById('file'),kind:document.getElementById('kind'),encoding:document.getElementById('encoding'),result:document.getElementById('result'),send:document.getElementById('send'),build:document.getElementById('build')};
let capabilities=null;
function classify(name){if(/\.inh$/i.test(name))return 'app';if(/\.(txt|fb2)$/i.test(name))return 'book';throw Error('Поддерживаются только TXT, FB2 и INH. Проверьте настоящее расширение файла.');}
function uploadName(name,kind){if(kind!=='app')return 'book.txt';let stem=name.replace(/\.inh$/i,'').replace(/[^a-zA-Z0-9_-]/g,'_').slice(0,32);return (stem||'application')+'.inh';}
function normalize(text){return text.replace(/^\uFEFF/,'').replace(/\r\n?/g,'\n');}
function fb2Text(text){const xml=new DOMParser().parseFromString(text,'application/xml');if(xml.getElementsByTagNameNS('*','parsererror').length)throw Error('Файл FB2 содержит ошибку XML.');const bodies=Array.from(xml.getElementsByTagNameNS('*','body'));if(!bodies.length)throw Error('В FB2 отсутствует текст книги.');const lines=[];for(const body of bodies){for(const e of body.getElementsByTagNameNS('*','*')){if(e.localName==='p'||e.localName==='subtitle')lines.push(e.textContent);else if(e.localName==='empty-line')lines.push('');}}return lines.join('\n');}
async function info(){const r=await fetch('/info',{cache:'no-store'});if(!r.ok)throw Error('Не удалось определить сборку. Откройте страницу заново.');capabilities=await r.json();ui.build.textContent='Сборка: '+capabilities.edition+' · '+capabilities.version+(capabilities.apps?' — доступны книги и приложения.':' — только книги. Для .inh нужна прошивка Apps той же панели.');return capabilities;}
function bytesOf(file){if(file.arrayBuffer)return file.arrayBuffer();return new Promise((resolve,reject)=>{const r=new FileReader();r.onload=()=>resolve(r.result);r.onerror=()=>reject(Error('Не удалось прочитать файл'));r.readAsArrayBuffer(file);});}
async function api(path,args){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(args)});const text=await r.text();if(!r.ok)throw Error(text||('Ошибка загрузки HTTP '+r.status));return text;}
ui.file.onchange=()=>{try{if(ui.file.files[0])ui.kind.value=classify(ui.file.files[0].name);ui.result.textContent='';}catch(e){ui.result.textContent=e.message;}};
ui.send.onclick=async()=>{ui.send.disabled=true;try{const f=ui.file.files[0];if(!f)throw Error('Выберите файл.');const kind=classify(f.name);if(kind!==ui.kind.value)throw Error('Выбранный тип не совпадает с расширением файла.');const cfg=await info();if(kind==='app'&&!cfg.apps)throw Error('На устройстве Reader. Установите Apps для вашей панели: иначе приложения не запустятся.');if(typeof TextDecoder==='undefined'||typeof TextEncoder==='undefined')throw Error('Откройте http://192.168.4.1 в полном браузере Chrome, Firefox или Safari.');let text=new TextDecoder(kind==='app'?'utf-8':ui.encoding.value,{fatal:true}).decode(await bytesOf(f));if(/\.fb2$/i.test(f.name))text=fb2Text(text);const bytes=new TextEncoder().encode(normalize(text));const limit=kind==='app'?cfg.max_app_bytes:cfg.max_book_bytes;if(!bytes.length||bytes.length>limit)throw Error('Размер файла должен быть от 1 до '+limit+' байт.');await api('/begin',{name:uploadName(f.name,kind),size:bytes.length});for(let i=0;i<bytes.length;i+=2048){const part=bytes.slice(i,i+2048);await api('/chunk',{offset:i,data:btoa(String.fromCharCode(...part))});ui.result.textContent=Math.floor(100*(i+part.length)/bytes.length)+'%';}ui.result.textContent=await api('/finish',{});}catch(e){ui.result.textContent=e.message;}finally{ui.send.disabled=false;}};
info().catch(e=>{ui.build.textContent=e.message;});
</script></html>
)INHPORTAL";
