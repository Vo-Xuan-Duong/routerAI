#pragma once

#include <string_view>

namespace routerai {

inline constexpr std::string_view adminWebUiHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>routerAI Admin</title>
<style>
:root{color-scheme:dark;background:#0b0d10;color:#e8ebf0;font:14px system-ui,sans-serif}body{margin:0}header{display:flex;gap:16px;align-items:center;padding:16px 22px;border-bottom:1px solid #242a31;position:sticky;top:0;background:#0b0d10;z-index:2}header b{font-size:18px}.grow{flex:1}input,select,button,textarea{background:#151a20;color:#e8ebf0;border:1px solid #303844;border-radius:7px;padding:8px}button{cursor:pointer}button.danger{border-color:#72343a;color:#ffb4bb}.wrap{max-width:1280px;margin:auto;padding:20px}.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}.card,.panel{background:#11151a;border:1px solid #242a31;border-radius:10px;padding:14px}.value{font-size:28px;font-weight:700}.muted{color:#8f9aa8}.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-top:14px}@media(max-width:900px){.grid{grid-template-columns:1fr}}table{width:100%;border-collapse:collapse}th,td{text-align:left;border-bottom:1px solid #242a31;padding:8px;vertical-align:top}.bar{height:7px;background:#242a31;border-radius:8px;overflow:hidden}.bar>i{display:block;height:100%;background:#65b7ff}.ok{color:#72db91}.bad{color:#ff818b}.warn{color:#ffd36a}pre{white-space:pre-wrap;word-break:break-word}.toolbar{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px}.setup{display:grid;grid-template-columns:180px 180px minmax(220px,1fr) auto;gap:8px}@media(max-width:850px){.setup{grid-template-columns:1fr}}textarea{width:100%;min-height:180px;box-sizing:border-box}</style>
</head>
<body>
<header><b>routerAI Admin</b><span class="muted">localhost control plane</span><span class="grow"></span><input id="key" type="password" placeholder="router-local-* key"><button onclick="saveKey()">Connect</button></header>
<div class="wrap">
<div id="status" class="muted">Enter the Local API key shown by routerAI.</div>
<div class="cards" id="cards"></div>
<section class="panel" style="margin-top:14px"><h3>Provider setup</h3><div class="setup"><select id="provider" onchange="syncModes()"><option value="codex">Codex</option><option value="antigravity">Antigravity</option><option value="zai">Z.ai</option></select><select id="mode"></select><input id="providerKey" type="password" autocomplete="off" placeholder="API key (only for API-key modes)"><button onclick="addProvider()">Add provider</button></div><p class="muted">Codex and Antigravity consumer login use the providers' official browser sign-in flows. API keys are sent only to this localhost process and are stored through routerAI CredentialStore.</p></section>
<div class="grid">
<section class="panel"><h3>Accounts & usage</h3><div id="accounts"></div></section>
<section class="panel"><h3>Routing groups</h3><div id="groups"></div></section>
</div>
<section class="panel" style="margin-top:14px"><div class="toolbar"><h3 style="margin:6px 12px 0 0">Request history</h3><button onclick="loadAll()">Refresh</button><button class="danger" onclick="clearLogs()">Clear</button></div><div id="logs"></div></section>
<section class="panel" style="margin-top:14px"><h3>Secret-free config</h3><div class="toolbar"><button onclick="exportConfig()">Export to editor</button><button onclick="downloadConfig()">Download JSON</button><button onclick="importConfig()">Import editor JSON</button></div><textarea id="config" placeholder="Exported config contains metadata only. Provider credentials are never included."></textarea></section>
</div>
<script>
const $=id=>document.getElementById(id);const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
$('key').value=localStorage.routerAIKey||'';
function headers(json=false){let h={'Authorization':'Bearer '+$('key').value};if(json)h['Content-Type']='application/json';return h}
async function api(path,opt={}){opt.headers={...headers(!!opt.body),...(opt.headers||{})};let r=await fetch(path,opt);if(!r.ok){let t=await r.text();try{let j=JSON.parse(t);t=j.error||j.detail||t}catch(_){}throw new Error(t||('HTTP '+r.status))}return r}
function saveKey(){localStorage.routerAIKey=$('key').value;loadAll()}
function syncModes(){let p=$('provider').value,m=$('mode');let values=p==='codex'?[['default','ChatGPT / Codex account']]:p==='antigravity'?[['consumer-cli','Consumer CLI session'],['api-project','Gemini API project']]:[['general-api','General API'],['coding-plan','Coding Plan']];m.innerHTML=values.map(x=>`<option value="${x[0]}">${x[1]}</option>`).join('');$('providerKey').placeholder=(p==='codex'||(p==='antigravity'&&m.value==='consumer-cli'))?'No key required; login after adding':'API key (optional now; can configure later)'}
async function loadAll(){try{let o=await (await api('/admin/api/overview')).json();renderOverview(o);let l=await (await api('/admin/api/requests?limit=200')).json();renderLogs(l);$('status').textContent='Connected';$('status').className='ok'}catch(e){$('status').textContent=e.message;$('status').className='bad'}}
function accountButtons(a){let b=`<button onclick="refreshAccount('${esc(a.id)}')">Refresh</button> `;if(a.provider==='codex'||(a.provider==='antigravity'&&a.mode==='consumer-cli'))b+=`<button onclick="loginAccount('${esc(a.id)}')">Login</button> `;if(a.provider==='zai'||(a.provider==='antigravity'&&a.mode==='api-project'))b+=`<button onclick="configureKey('${esc(a.id)}')">Key</button> `;b+=`<button onclick="accountAction('${esc(a.id)}','${a.enabled?'disable':'enable'}')">${a.enabled?'Disable':'Enable'}</button> <button class=danger onclick="accountAction('${esc(a.id)}','remove')">Remove</button>`;return b}
function renderOverview(o){$('cards').innerHTML=[['Accounts',o.summary.accounts],['Ready',o.summary.ready],['Warnings',o.summary.warning],['Requests',o.summary.requests],['Success %',o.summary.success_rate.toFixed(1)]].map(x=>`<div class=card><div class=muted>${x[0]}</div><div class=value>${x[1]}</div></div>`).join('');$('accounts').innerHTML='<table><tr><th>ID</th><th>Provider</th><th>Status</th><th>Usage</th><th>Actions</th></tr>'+o.accounts.map(a=>`<tr><td>${esc(a.id)}<div class=muted>${esc(a.identity)}</div></td><td>${esc(a.provider)}<div class=muted>${esc(a.mode)}</div></td><td>${esc(a.status)}${a.enabled?'':'<div class=muted>routing disabled</div>'}</td><td>${a.usage==null?'-':`<div>${a.usage.toFixed(1)}%</div><div class=bar><i style="width:${Math.min(100,a.usage)}%"></i></div>`}</td><td>${accountButtons(a)}</td></tr>`).join('')+'</table>';$('groups').innerHTML='<table><tr><th>Group</th><th>Strategy</th><th>Members</th></tr>'+o.groups.map(g=>`<tr><td>${esc(g.id)}</td><td>${esc(g.strategy)}</td><td>${g.members}</td></tr>`).join('')+'</table>'}
function renderLogs(o){$('logs').innerHTML='<table><tr><th>Time</th><th>Route</th><th>Status</th><th>ms</th><th>Error</th></tr>'+o.data.map(x=>`<tr><td>${esc(x.created_at)}</td><td>${esc(x.group)} → ${esc(x.provider||'-')} / ${esc(x.account||'-')}<div class=muted>${esc(x.model)}</div></td><td class=${x.success?'ok':'bad'}>${x.status}</td><td>${x.duration_ms}</td><td>${esc(x.error)}</td></tr>`).join('')+'</table>'}
async function addProvider(){let provider=$('provider').value,mode=$('mode').value,api_key=$('providerKey').value;try{let r=await (await api('/admin/api/providers',{method:'POST',body:JSON.stringify({provider,mode,api_key})})).json();$('providerKey').value='';alert(r.detail||'Provider added');await loadAll()}catch(e){alert(e.message)}}
async function loginAccount(id){try{let r=await (await api('/admin/api/login',{method:'POST',body:JSON.stringify({id,use_browser:true})})).json();alert(r.detail||'Login completed');await loadAll()}catch(e){alert(e.message)}}
async function refreshAccount(id){try{let r=await (await api('/admin/api/refresh-account',{method:'POST',body:JSON.stringify({id})})).json();if(r.detail)alert(r.detail);await loadAll()}catch(e){alert(e.message)}}
async function configureKey(id){let api_key=prompt('Enter API key for '+id);if(!api_key)return;try{let r=await (await api('/admin/api/provider-key',{method:'POST',body:JSON.stringify({id,api_key})})).json();alert(r.detail||'Credential updated');await loadAll()}catch(e){alert(e.message)}}
async function accountAction(id,action){if(action==='remove'&&!confirm('Remove '+id+' and its local credential/profile state?'))return;try{await api('/admin/api/account-action',{method:'POST',body:JSON.stringify({id,action})});await loadAll()}catch(e){alert(e.message)}}
async function clearLogs(){if(!confirm('Clear request history?'))return;try{await api('/admin/api/requests',{method:'DELETE'});await loadAll()}catch(e){alert(e.message)}}
async function exportConfig(){try{$('config').value=await (await api('/admin/api/config')).text()}catch(e){alert(e.message)}}
async function downloadConfig(){await exportConfig();let b=new Blob([$('config').value],{type:'application/json'}),a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='routerai-config.json';a.click();URL.revokeObjectURL(a.href)}
async function importConfig(){if(!confirm('Import metadata from editor? Secrets are not imported.'))return;try{await api('/admin/api/config',{method:'POST',body:$('config').value,headers:{'Content-Type':'application/json'}});await loadAll()}catch(e){alert(e.message)}}
syncModes();$('mode').addEventListener('change',syncModes);if($('key').value)loadAll();
</script></body></html>)HTML";

}  // namespace routerai
