// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const api=acquireVsCodeApi(),source=document.getElementById('source'),counter=document.getElementById('counter');
const previous=api.getState();let count=previous?.count??0;
const save=()=>api.setState({count,source:source.value});
function updateCounter(){counter.textContent='Counter: '+count;}
function render(){
  const preview=document.getElementById('preview');preview.innerHTML=source.value;
  // Running HTML is an explicit user action in this trusted service view.
  for(const inert of preview.querySelectorAll('script')){const script=document.createElement('script');for(const attr of inert.attributes)script.setAttribute(attr.name,attr.value);script.textContent=inert.textContent;inert.replaceWith(script);}
  save();
}
async function load(){const result=await(await fetch('api/document')).json();document.getElementById('service').textContent='Node PID '+result.pid;source.value=result.text;render();}
counter.addEventListener('click',()=>{++count;updateCounter();save();});source.addEventListener('input',save);
document.getElementById('run').addEventListener('click',render);document.getElementById('load').addEventListener('click',()=>load().catch(error=>document.getElementById('service').textContent=error.message));
updateCounter();if(previous?.source!==undefined){source.value=previous.source;render();}else load().catch(error=>document.getElementById('service').textContent=error.message);
