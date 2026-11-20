#pragma once
namespace LeleUpload {
inline constexpr char kFilePage[] = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lele SD Files</title><style>
body{font:16px system-ui;max-width:62em;margin:2em auto;padding:0 1em}
button,input{font:inherit;padding:.45em;margin:.25em;max-width:100%}
table{width:100%;border-collapse:collapse}td,th{padding:.5em;border-bottom:1px solid #bbb;text-align:left}
td:first-child,pre,#path{overflow-wrap:anywhere}td:last-child{min-width:9em}
pre{white-space:pre-wrap}button{cursor:pointer}button:disabled{cursor:default}
</style><h1>Lele SD files</h1>
<p>Keep Wi-Fi File Transfer open on the device. Hidden/system files are protected.
Delete removes files or empty folders only. Uploads: 1 byte–64 MiB per file.</p>
<label>Device code <input id="key" maxlength="8" autocomplete="off"></label>
<button id="connect">Connect / Refresh</button>
<p id="path">Not connected</p><p id="notice" role="status"></p>
<nav><button id="root">SD root</button><button id="up">Up</button>
<button id="mkdir">New folder</button><button id="prev">Previous page</button>
<button id="next">Next page</button></nav>
<table><thead><tr><th>Name</th><th>Size</th><th>Actions</th></tr></thead><tbody id="entries"></tbody></table>
<p>Upload to the current folder:</p><input id="files" type="file" multiple>
<button id="send">Upload files</button><pre id="result" role="status"></pre>
<script>
const byId=id=>document.getElementById(id),out=byId('result');
let cwd='',offset=0,next=null,history=[],revision=0,busy=false,connected=false;
const join=(dir,name)=>dir?dir+'/'+name:name;
function navigation(){
 byId('up').disabled=busy||!cwd;
 byId('prev').disabled=busy||!history.length;
 byId('next').disabled=busy||next===null;
}
function setBusy(value){
 busy=value;document.querySelectorAll('button,input').forEach(el=>el.disabled=value);
 navigation();
}
async function api(url,options={}){
 const key=byId('key').value.trim().toUpperCase();
 if(!/^[0-9A-F]{8}$/.test(key))throw Error('Enter the 8-character code shown on the device.');
 const response=await fetch(url,{...options,headers:{'X-Lele-Key':key,...(options.headers||{})}});
 const text=await response.text();
 if(!response.ok){const error=Error(response.status+' '+text);error.status=response.status;throw error;}
 return JSON.parse(text);
}
function button(label,action){
 const el=document.createElement('button');el.textContent=label;el.disabled=busy;
 el.onclick=()=>execute(action);return el;
}
async function load(dir,start=0){
 const data=await api('/files?path='+encodeURIComponent(dir)+'&offset='+start,
   {headers:start?{'X-Lele-Revision':String(revision)}:{}});
 cwd=dir;offset=start;next=data.next;revision=data.revision;connected=true;
 byId('path').textContent='/'+cwd;
 byId('notice').textContent=data.limited?'Directory scan limit reached; not all entries are shown.':'';
 const rows=byId('entries');rows.replaceChildren();
 for(const entry of data.entries){
  const path=join(cwd,entry.name),row=document.createElement('tr');
  const name=document.createElement('td');name.textContent=entry.name;
  const size=document.createElement('td');size.textContent=entry.directory?'Folder':entry.bytes+' B';
  const actions=document.createElement('td');
  if(entry.directory)actions.append(button('Open',()=>navigate(path)));
  actions.append(button('Rename',()=>move(path,entry.name,true)),
    button('Move',()=>move(path,entry.name,false)),
    button(entry.directory?'Delete empty':'Delete',()=>remove(path)));
  row.append(name,size,actions);rows.append(row);
 }
 navigation();
}
async function navigate(dir){await load(dir);history=[];navigation();}
async function info(path){
 try{return await api('/stat?path='+encodeURIComponent(path));}
 catch(error){if(error.status===404)return null;throw error;}
}
async function execute(action){
 if(busy)return;
 out.textContent='';setBusy(true);
 try{await action();}
 catch(error){out.textContent+='Operation stopped: '+error.message+'\n';}
 finally{setBusy(false);}
}
async function manage(op,path,to,current){
 const query=new URLSearchParams({op,path});
 if(to!==undefined)query.set('to',to);
 const headers=current?{'X-Lele-Confirm':'yes','X-Lele-Revision':String(current.revision)}:{};
 await api('/manage?'+query,{method:'POST',headers});
 await navigate(cwd);
}
async function move(path,name,rename){
 const target=prompt(rename?'New name:':'Destination path relative to the SD root:',rename?name:path);
 if(target===null||!target)return;
 if(rename&&(target.includes('/')||target.includes('\\')))throw Error('Enter one filename.');
 const destination=rename?join(cwd,target):target;
 if(destination===path)return;
 const current=await info(path);
 if(!current)throw Error('Source no longer exists; refresh the folder.');
 await manage('move',path,destination,current);
 out.textContent='Moved /'+path+' to /'+destination+'\n';
}
async function remove(path){
 const current=await info(path);
 if(!current)throw Error('Source no longer exists; refresh the folder.');
 if(!confirm('Delete '+(current.directory?'empty folder':'file')+' /'+path+'? This cannot be undone.'))return;
 await manage('delete',path,undefined,current);
 out.textContent='Deleted /'+path+'\n';
}
byId('connect').onclick=()=>execute(()=>navigate(cwd));
byId('root').onclick=()=>execute(()=>navigate(''));
byId('up').onclick=()=>execute(()=>navigate(cwd.slice(0,Math.max(0,cwd.lastIndexOf('/')))));
byId('next').onclick=()=>execute(async()=>{
 if(next===null)return;const previous=offset;await load(cwd,next);history.push(previous);navigation();
});
byId('prev').onclick=()=>execute(async()=>{
 if(!history.length)return;const previous=history[history.length-1];await load(cwd,previous);history.pop();navigation();
});
byId('mkdir').onclick=()=>execute(async()=>{
 if(!connected)throw Error('Connect first.');
 const name=prompt('New folder name:');
 if(name===null||!name)return;
 if(name.includes('/')||name.includes('\\'))throw Error('Enter one folder name.');
 await manage('mkdir',join(cwd,name));
 out.textContent='Folder created.\n';
});
byId('send').onclick=()=>execute(async()=>{
 if(!connected)throw Error('Connect first.');
 const files=[...byId('files').files],directory=cwd;
 if(!files.length)throw Error('Choose files to upload.');
 for(const file of files){
  if(!file.size||file.size>64*1024*1024)throw Error(file.name+': file must be 1 byte to 64 MiB.');
  const path=join(directory,file.name),current=await info(path);
  let query='/upload?path='+encodeURIComponent(path),headers={};
  if(current){
   if(current.directory)throw Error(file.name+': a folder already uses this name.');
   if(!confirm('Replace /'+path+' ('+current.bytes+' bytes) with the selected file?')){
    out.textContent+='Skipped '+file.name+'\n';continue;
   }
   query+='&replace=1';headers={'X-Lele-Confirm':'yes','X-Lele-Revision':String(current.revision)};
  }
  out.textContent+='Sending '+file.name+'...\n';
  const saved=await api(query,{method:'POST',headers,body:file});
  out.textContent+='Saved '+file.name+' ('+saved.bytes+' bytes)\nSHA-256: '+saved.sha256+'\n';
  if(saved.backup_kept)out.textContent+='Old recovery copy retained on SD; inspect it with USB.\n';
 }
 await navigate(directory);
});
navigation();
</script></html>)HTML";
}  // namespace LeleUpload
