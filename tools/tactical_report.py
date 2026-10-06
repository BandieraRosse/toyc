#!/usr/bin/env python3
"""Inspect native tactical JSONL or export a self-contained interactive match replay."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


HTML = r'''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Rasterfall tactical replay</title>
<style>body{margin:20px;background:#111820;color:#e8eef5;font:15px system-ui}
main{max-width:1200px;margin:auto}h1{font-size:22px}canvas{width:100%;background:#1c2730;border:1px solid #485765}
.controls{display:flex;gap:14px;align-items:center;margin:12px 0}input{flex:1}
button,select{background:#293948;color:#fff;padding:7px;border:1px solid #617586}
pre{white-space:pre-wrap;background:#19232d;padding:12px;max-height:420px;overflow:auto}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}table{border-collapse:collapse;width:100%;font-size:13px}
th,td{padding:5px;border-bottom:1px solid #384c5b;text-align:right}th:first-child,td:first-child{text-align:left}
@media(max-width:800px){.grid{display:block}}</style><main>
<h1>Rasterfall native tactical replay</h1><p id="meta"></p>
<canvas id="map" width="960" height="720"></canvas>
<div class="controls"><button id="play">Play</button><input id="tick" type="range" min="0" step="1"><span id="time"></span>
<select id="speed"><option value="1">1×</option><option value="5">5×</option><option value="20">20×</option></select></div>
<div class="controls"><label>Team <select id="team"><option value="0">0</option><option value="1">1</option></select></label>
<label>Unit <select id="unit"></select></label><span>Blue = team 0; orange = team 1. Click a unit to inspect.</span></div>
<div class="grid"><div><h2>Candidate facts and solver scores</h2><div id="candidates"></div></div>
<div><h2>Selected decision</h2><pre id="decision"></pre></div></div>
<h2>Authoritative state</h2><pre id="state"></pre>
<script id="data" type="application/json">__DATA__</script><script>
const data=JSON.parse(document.getElementById('data').textContent),h=data.header;
const ticks=data.ticks,decisions=data.decisions,c=document.getElementById('map'),ctx=c.getContext('2d');
const slider=document.getElementById('tick'),team=document.getElementById('team'),unit=document.getElementById('unit');
slider.max=ticks.length-1;slider.value=0;
for(let i=0;i<h.squad_size;i++){let o=document.createElement('option');o.value=i;o.textContent=i;unit.appendChild(o)}
document.getElementById('meta').textContent=`Version ${h.simulation_version}; map ${h.map_seed}; shots ${h.shot_seed}; `+
`${h.squad_size} vs ${h.squad_size}; weapon ${h.weapon===0?'rifle':'SMG'}; `+
`${h.policies.map(p=>p.name+' ['+p.hash+'] budget '+p.budget).join(' / ')}; final ${data.result?.final_hash||'incomplete'}`;
const sx=c.width/h.width_m,sy=c.height/h.height_m;
let playing=false,last=0,accum=0;
function latest(t,side){let d=null;for(const x of decisions){if(x.tick>t)break;if(x.team===side)d=x}return d}
function render(){const s=ticks[+slider.value],side=+team.value,idx=+unit.value,id=side*h.squad_size+idx;
 for(let t=0;t<2;t++)team.children[t].textContent=`${t} ${s.orders[t]===1?'attack':'defense'}`;
 ctx.clearRect(0,0,c.width,c.height);ctx.strokeStyle='#263746';ctx.lineWidth=1;
 for(let x=0;x<h.width_m;x+=2){ctx.beginPath();ctx.moveTo(x*sx,0);ctx.lineTo(x*sx,c.height);ctx.stroke()}
 for(let y=0;y<h.height_m;y+=2){ctx.beginPath();ctx.moveTo(0,y*sy);ctx.lineTo(c.width,y*sy);ctx.stroke()}
 for(const box of h.covers){ctx.fillStyle=box[4]===2?'#70808a':'#425b67';ctx.fillRect(box[0]*sx,box[1]*sy,(box[2]-box[0])*sx,(box[3]-box[1])*sy)}
 ctx.strokeStyle='#85d59a';ctx.lineWidth=2;ctx.beginPath();ctx.ellipse(h.objective[0]*sx,h.objective[1]*sy,h.objective_radius*sx,h.objective_radius*sy,0,0,Math.PI*2);ctx.stroke();
 const d=latest(s.tick,side),du=d?.units[idx];
 if(du){for(const k of du.candidates){ctx.fillStyle=k.index===du.selected?'#ddf75b':'#bbd29177';ctx.fillRect(k.position[0]*sx-3,k.position[1]*sy-3,6,6)}}
 for(const u of s.units){if(u.shot_delta>0&&u.shot_target>=0){const target=s.units[(1-u.team)*h.squad_size+u.shot_target];if(target){ctx.strokeStyle=u.hit_delta?'#fff090':'#937c47';ctx.lineWidth=2;ctx.beginPath();ctx.moveTo(u.position[0]*sx,u.position[1]*sy);ctx.lineTo(target.position[0]*sx,target.position[1]*sy);ctx.stroke()}}}
 for(const u of s.units){const x=u.position[0]*sx,y=u.position[1]*sy;ctx.globalAlpha=u.alive?1:.3;ctx.fillStyle=u.team===0?'#70beff':'#ffac71';ctx.beginPath();ctx.arc(x,y,u.team===side&&u.id%h.unit_id_stride===idx?9:6,0,Math.PI*2);ctx.fill();ctx.globalAlpha=1;ctx.fillStyle='#fff';ctx.font='12px system-ui';ctx.fillText(String(u.id),x+9,y-7);ctx.fillStyle='#163926';ctx.fillRect(x-9,y+11,18,3);ctx.fillStyle='#90de9d';ctx.fillRect(x-9,y+11,18*Math.max(0,u.effective_health)/(h.baseline.hp+h.baseline.risk),3)}
 document.getElementById('time').textContent=`tick ${s.tick} / ${(s.time_ms/1000).toFixed(2)} s`;
 document.getElementById('state').textContent=JSON.stringify(s.units[id],null,2);
 document.getElementById('decision').textContent=JSON.stringify(du?{tick:d.tick,order:d.order,evaluations:d.evaluations,budget_exhausted:d.budget_exhausted,...du,candidates:undefined}:null,null,2);
 const table=document.createElement('table'),head=document.createElement('tr');
 for(const label of ['candidate','score','out DPS','in DPS','cover','travel s','path dmg','objective m','nav m']){let th=document.createElement('th');th.textContent=label;head.appendChild(th)}table.appendChild(head);
 for(const k of du?.candidates||[]){let tr=document.createElement('tr');if(k.index===du.selected)tr.style.color='#ddf75b';for(const value of [k.index,k.score,k.outgoing_dps,k.incoming_dps,k.cover_quality,k.path.travel_time,k.path.incoming_damage,k.objective_distance,k.objective_path_distance]){let td=document.createElement('td');td.textContent=typeof value==='number'?value.toFixed(2):value;tr.appendChild(td)}table.appendChild(tr)}
 document.getElementById('candidates').replaceChildren(table);
}
slider.oninput=render;team.onchange=render;unit.onchange=render;
document.getElementById('play').onclick=()=>{playing=!playing;document.getElementById('play').textContent=playing?'Pause':'Play'};
c.onclick=e=>{const box=c.getBoundingClientRect(),x=(e.clientX-box.left)/box.width*h.width_m,y=(e.clientY-box.top)/box.height*h.height_m;let closest=ticks[+slider.value].units.reduce((a,b)=>Math.hypot(a.position[0]-x,a.position[1]-y)<Math.hypot(b.position[0]-x,b.position[1]-y)?a:b);team.value=closest.team;unit.value=closest.id%h.unit_id_stride;render()};
function frame(now){if(playing){accum+=(now-last)*+document.getElementById('speed').value;let step=Math.floor(accum/h.dt_ms);if(step){accum-=step*h.dt_ms;slider.value=Math.min(ticks.length-1,+slider.value+step);render();if(+slider.value===ticks.length-1){playing=false;document.getElementById('play').textContent='Play'}}}last=now;requestAnimationFrame(frame)}render();requestAnimationFrame(frame);
</script></main></html>'''


def load(path: Path) -> dict:
    result = {"header": None, "ticks": [], "decisions": [], "result": None}
    with path.open("r", encoding="utf-8") as stream:
        for number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            record = json.loads(line)
            kind = record.get("type")
            if kind == "header":
                if result["header"] is not None:
                    raise ValueError("Replay must contain exactly one match header")
                result["header"] = record
            elif kind == "tick":
                result["ticks"].append(record)
            elif kind == "decision":
                result["decisions"].append(record)
            elif kind == "result":
                result["result"] = record
            else:
                raise ValueError(f"Unknown native record at line {number}: {kind}")
    if result["header"] is None or not result["ticks"]:
        raise ValueError("Missing native match header or tick records")
    if result["header"].get("schema") != 1:
        raise ValueError("Unsupported native replay schema")
    return result


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("log", type=Path)
    p.add_argument("--output", type=Path, help="Write standalone replay HTML")
    p.add_argument("--tick", type=int, help="Print exact authoritative tick and that round's decisions")
    args = p.parse_args()
    data = load(args.log)
    if args.tick is not None:
        states = [s for s in data["ticks"] if s["tick"] == args.tick]
        if not states:
            raise ValueError(f"Tick {args.tick} is absent")
        latest = {}
        for d in data["decisions"]:
            if d["tick"] <= args.tick:
                latest[d["team"]] = d
        print(json.dumps({"header": data["header"], "state": states[0],
                          "latest_decisions": list(latest.values())}, indent=2, ensure_ascii=False))
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        payload = json.dumps(data, separators=(",", ":"), ensure_ascii=True).replace("<", "\\u003c")
        args.output.write_text(HTML.replace("__DATA__", payload), encoding="utf-8", newline="\n")
        print(f"Replay: {args.output}; ticks={len(data['ticks'])}; decisions={len(data['decisions'])}", file=sys.stderr)
    elif args.tick is None:
        print(json.dumps({"header": data["header"], "result": data["result"],
                          "ticks": len(data["ticks"]), "decisions": len(data["decisions"])}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError) as exc:
        print(f"tactical_report: {exc}", file=sys.stderr)
        raise SystemExit(1)
