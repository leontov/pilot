const el = id => document.getElementById(id);

async function fetchMetrics(){
  try{
    const r = await fetch('/api/v1/metrics');
    if(!r.ok) throw new Error('no metrics');
    const j = await r.json();
    el('metrics').textContent = JSON.stringify(j, null, 2);
    if (j.uptime) el('uptime').textContent = 'uptime: '+j.uptime+'s';
  }catch(e){ el('metrics').textContent = 'Ошибка загрузки метрик'; }
}

async function solve(task){
  const res = await fetch('/api/v1/solve', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({task})
  });
  const j = await res.json();
  return j;
}

function addHistory(item){
  const li = document.createElement('li');
  li.textContent = `${new Date((item.timestamp||Math.floor(Date.now()/1000))*1000).toLocaleString()}: ${item.task} -> ${item.result}`;
  const h = el('history');
  h.prepend(li);
  while(h.children.length>20) h.removeChild(h.lastChild);
}

el('solveBtn').addEventListener('click', async ()=>{
  const task = el('taskInput').value.trim();
  if(!task) return;
  el('result').textContent = '';
  el('loader').style.display = 'inline-block';
  try{
    const j = await solve(task);
    el('result').textContent = j.result || JSON.stringify(j);
    addHistory({...j, task});
    fetchMetrics();
  }catch(e){ el('result').textContent = 'Ошибка: '+e.message }
  el('loader').style.display = 'none';
});

// SSE connection for live updates
if (window.EventSource) {
  const es = new EventSource('/api/v1/events');
  es.onopen = () => { el('connDot').className='dot dot-on'; };
  es.onmessage = (ev) => {
    try{ const d = JSON.parse(ev.data); if (d.uptime) el('uptime').textContent = 'uptime: '+d.uptime+'s'; }
    catch(e){}
  };
  es.onerror = () => { el('connDot').className='dot dot-off'; };
}

// initial load
fetchMetrics();
