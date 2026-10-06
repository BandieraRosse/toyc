#!/usr/bin/env python3
"""Export existing native tactical range JSON as an offline HTML report or CSV."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import sys


HTML = r'''<!doctype html>
<html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Rasterfall 枪械靶场</title>
<style>
:root{color-scheme:light;--ink:#192737;--muted:#536477;--line:#dce3eb;--blue:#2161c1;--orange:#c26316;--red:#b33946}
*{box-sizing:border-box}body{margin:0;background:#f3f6fa;color:var(--ink);font:15px/1.6 system-ui,"Microsoft YaHei",sans-serif}
main{max-width:1340px;margin:0 auto;padding:30px 24px 46px}h1{font-size:28px;line-height:1.2;margin:0 0 10px}h2{font-size:17px;margin:0}
p{margin:5px 0;color:var(--muted)}.meta{font-size:13px;margin:12px 0 22px}.bar{display:flex;gap:22px;align-items:center;flex-wrap:wrap;background:#fff;border:1px solid var(--line);border-radius:9px;padding:15px 18px}
label{display:flex;align-items:center;gap:9px;color:var(--muted)}select{font:inherit;color:var(--ink);padding:6px 32px 6px 10px;background:#fff;border:1px solid #bac8d6;border-radius:5px}
.legend{display:flex;align-items:center;gap:20px;margin-left:auto;font-size:14px}.legend span{display:flex;gap:8px;align-items:center}.swatch{width:22px;height:3px;background:var(--blue)}.smg{background:var(--orange)}
.note{font-size:13px;margin:14px 0 20px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:18px}.card{background:#fff;border:1px solid var(--line);border-radius:9px;padding:16px 18px 12px}.card p{font-size:12px;min-height:20px}
svg{display:block;width:100%;height:auto;overflow:visible;margin-top:6px}.chart-value{cursor:help}.table-card{margin-top:22px;padding:18px;background:#fff;border:1px solid var(--line);border-radius:9px}.table-scroll{overflow-x:auto}
table{width:100%;border-collapse:collapse;font-size:13px;white-space:nowrap;margin-top:12px}th,td{padding:9px 11px;text-align:right;border-bottom:1px solid #e5ebf1}th{color:var(--muted);background:#f7f9fc;font-weight:600}th:first-child,td:first-child{text-align:left}td.weapon-rifle{color:var(--blue);font-weight:600}td.weapon-smg{color:var(--orange);font-weight:600}.censored{color:var(--red);font-weight:600}.definitions{font-size:13px;margin-top:20px;display:grid;gap:7px}button{font:inherit;padding:6px 10px;cursor:pointer;background:#fff;border:1px solid #bac8d6;border-radius:5px;color:var(--ink)}.footer{font-size:12px;margin-top:22px;overflow-wrap:anywhere}
@media(max-width:850px){main{padding:22px 14px}.grid{grid-template-columns:1fr}.legend{margin-left:0}.bar{gap:12px}h1{font-size:24px}.card{padding:13px}}
@media print{body{background:#fff}main{padding:0}.card,.table-card{break-inside:avoid}.bar button{display:none}select{border:0}.grid{gap:8px}}
</style>
<main><h1>Rasterfall 枪械靶场</h1><p id="baseline"></p><p class="meta" id="meta"></p>
<div class="bar"><label>目标 <select id="target"><option value="full">全身</option><option value="upper">半身</option><option value="head">探头</option><option value="moving">横向移动</option></select></label>
<label>射击 <select id="mode"><option value="single">单发</option><option value="burst">三连点射</option><option value="auto" selected>全自动</option></select></label>
<div class="legend"><span><i class="swatch"></i>步枪 Rifle</span><span><i class="swatch smg"></i>冲锋枪 SMG</span></div><button id="print" type="button">打印 / 保存 PDF</button></div>
<p class="note">所有曲线来自同一份原生 C 靶场结果。切换目标和射击模式仅筛选数据；悬停曲线上的点可查看采样与期望。生命损失、风险吸收与弹丸伤害分开统计。</p>
<div class="grid">
<section class="card"><h2>命中率</h2><p>按真实射击时序逐发累计的解析期望</p><svg id="chart-hit" viewBox="0 0 620 300" role="img" aria-label="步枪与冲锋枪命中率随距离变化"></svg></section>
<section class="card"><h2>每发伤害期望</h2><p>包含未命中和实际头部命中倍率；单位 HP / 发</p><svg id="chart-damage" viewBox="0 0 620 300" role="img" aria-label="步枪与冲锋枪每发伤害期望随距离变化"></svg></section>
<section class="card"><h2>输出伤害期望</h2><p>包含弹匣、换弹、模式暂停和 20ms 步长；单位 HP / 秒</p><svg id="chart-dps" viewBox="0 0 620 300" role="img" aria-label="步枪与冲锋枪期望DPS随距离变化"></svg></section>
<section class="card"><h2>击杀时间 TTK</h2><p>成功击杀样本的平均秒数。红圈表示该组存在 120 秒内未击杀的样本。</p><svg id="chart-ttk" viewBox="0 0 620 300" role="img" aria-label="步枪与冲锋枪成功样本平均击杀时间随距离变化"></svg></section>
</div>
<section class="table-card"><h2 id="table-title">距离对照</h2><p class="note">TTK 均值只描述已击杀样本；未击杀比例高时不能将这个均值解释为整组平均击杀速度。</p><div class="table-scroll"><table><thead><tr>
<th>枪种</th><th>距离 m</th><th>命中率期望</th><th>命中率采样</th><th>每发期望 HP</th><th>每发采样 HP</th><th>期望 DPS</th><th>采样 DPS</th><th>TTK 成功均值 s</th><th>120s 未击杀</th><th>击杀 / 尝试</th>
</tr></thead><tbody id="rows"></tbody></table></div></section>
<div class="definitions"><p><strong>期望与采样：</strong>期望值来自原生武器分布的解析查询，逐发使用当时的后坐状态；采样值来自同一分布的实际弹着。它们使用同一段真实用时计算 DPS，便于直接对照。</p>
<p><strong>伤害与生存：</strong>伤害曲线是命中弹丸造成的部位伤害期望，包含头部倍率。TTK 另行使用标准 AI 战士的 HP、可恢复风险资源、压力延迟和换弹过程进行真实结算；HP 不自动恢复。</p>
<p><strong>射击模式：</strong>单发间隔至少 400ms；三连点射每三发后暂停至少 350ms；全自动使用机械间隔，再由原生逻辑步长量化。源 JSON 另外保留冷启动弹匣及机械换弹循环期望，未将其混入这些真实时序曲线。</p>
<p id="missing"></p></div><p class="footer" id="source"></p><noscript>此离线报告需要启用 JavaScript。原生 JSON 和可选 CSV 仍可独立读取。</noscript></main>
<script id="data" type="application/json">__DATA__</script>
<script>
'use strict';
const report=JSON.parse(document.getElementById('data').textContent),data=report.range;
const target=document.getElementById('target'),mode=document.getElementById('mode');
const labels={full:'全身',upper:'半身',head:'探头',moving:'横向移动',single:'单发',burst:'三连点射',auto:'全自动',rifle:'步枪',smg:'冲锋枪'};
const styles={rifle:{color:'#2161c1',dash:''},smg:{color:'#c26316',dash:'6 4'}};
const num=(v,d=2)=>v===null||v===undefined?'—':Number(v).toFixed(d);
const pct=(v,d=1)=>num(v*100,d)+'%';
const b=data.baseline;
document.getElementById('baseline').textContent=`标准 AI 战士：${b.hp} HP + ${b.risk} 可恢复风险资源 · 身体 ${b.width_m} × ${b.height_m} m · 移动 ${b.move_speed_mps} m/s · 受击后 ${b.recovery_delay_ms/1000}s 才开始恢复`;
document.getElementById('meta').textContent=`仿真版本 ${data.simulation_version} · 射击种子 ${data.shot_seed} · 步长 ${data.dt_ms} ms · ${data.rows.length} 组测量 · 每组 ${[...new Set(data.rows.map(r=>r.sampled_shots))].join(' / ')} 发采样`;
document.getElementById('source').textContent=`来源：${report.source} · SHA-256 ${report.sha256} · 报告仅展示已有原生结果，不执行仿真。`;
document.getElementById('print').onclick=()=>window.print();
const ns='http://www.w3.org/2000/svg';
function el(name,attrs={},text){const n=document.createElementNS(ns,name);for(const [key,value] of Object.entries(attrs))n.setAttribute(key,String(value));if(text!==undefined)n.textContent=text;return n}
function niceStep(value){const power=10**Math.floor(Math.log10(value||1)),n=value/power;return (n<=1?1:n<=2?2:n<=5?5:10)*power}
function description(r,key,value){let s=`${labels[r.weapon]} · ${r.distance_m}m\n`;if(key==='ttk')return s+`成功样本平均 TTK ${num(value)}s\n击杀 ${r.ttk_kills}/${r.ttk_trials}，120s 未击杀 ${r.ttk_censored}\n均值仅针对已击杀样本。`;if(key==='hit')return s+`命中率期望 ${pct(value)} / 采样 ${pct(r.sampled_hit_rate)}\n采样命中 ${r.sampled_hits}/${r.sampled_shots}`;if(key==='damage')return s+`每发期望 ${num(value)}HP / 采样 ${num(r.sampled_damage_per_shot)}HP`;return s+`期望 ${num(value)}DPS / 采样 ${num(r.sampled_dps)}DPS`}
function draw(id,rows,key,field,unit,scale=1){
 const svg=document.getElementById(id);svg.replaceChildren();const L=58,R=16,T=22,B=43,W=620,H=300;
 const values=rows.map(r=>r[field]===null?null:r[field]*scale).filter(v=>v!==null&&Number.isFinite(v));
 const maxDistance=Math.max(1,...rows.map(r=>r.distance_m));const maximum=key==='hit'?1:Math.max(1,...values);
 const step=key==='hit'?.2:niceStep(maximum/4),top=Math.ceil(maximum/step)*step;
 const x=v=>L+v/maxDistance*(W-L-R),y=v=>H-B-v/top*(H-T-B);
 for(let v=0;v<=top+step/10;v+=step){svg.append(el('line',{x1:L,x2:W-R,y1:y(v),y2:y(v),stroke:'#e3e9f0'}));svg.append(el('text',{x:L-9,y:y(v)+4,'text-anchor':'end',fill:'#637185','font-size':11},key==='hit'?Math.round(v*100)+'%':num(v,step<1?1:0)))}
 for(let v=0;v<=maxDistance;v+=20){svg.append(el('text',{x:x(v),y:H-B+20,'text-anchor':'middle',fill:'#637185','font-size':11},String(v)))}
 svg.append(el('line',{x1:L,x2:W-R,y1:H-B,y2:H-B,stroke:'#bac6d2'}));svg.append(el('text',{x:W-R,y:H-6,'text-anchor':'end',fill:'#637185','font-size':11},'距离 m'));svg.append(el('text',{x:L,y:12,fill:'#637185','font-size':11},unit));
 for(const weapon of ['rifle','smg']){
  const points=rows.filter(r=>r.weapon===weapon).sort((a,b)=>a.distance_m-b.distance_m),style=styles[weapon];let path='',connected=false;
  for(const r of points){const v=r[field]===null?null:r[field]*scale;if(v===null){connected=false;continue}path+=(connected?' L ':' M ')+x(r.distance_m)+' '+y(v);connected=true}
  svg.append(el('path',{d:path,fill:'none',stroke:style.color,'stroke-width':2.5,'stroke-dasharray':style.dash}));
  for(const r of points){const v=r[field]===null?null:r[field]*scale;
   if(v===null){if(key==='ttk'){const mark=el('text',{x:x(r.distance_m),y:H-B-8,'text-anchor':'middle',fill:style.color,'font-size':18,class:'chart-value'},'×');mark.append(el('title',{},`${labels[weapon]} ${r.distance_m}m：${r.ttk_trials} 次尝试全部未能在120秒内击杀。`));svg.append(mark)}continue}
   if(key==='ttk'&&r.ttk_censored>0)svg.append(el('circle',{cx:x(r.distance_m),cy:y(v),r:7,fill:'none',stroke:'#b33946','stroke-width':1.5}));
   const point=el('circle',{cx:x(r.distance_m),cy:y(v),r:4,fill:'#fff',stroke:style.color,'stroke-width':2,class:'chart-value'});point.append(el('title',{},description(r,key,v)));svg.append(point)
  }
 }
 if(!rows.length)svg.append(el('text',{x:310,y:150,'text-anchor':'middle',fill:'#637185'},'此组合没有原生测量数据'));
 if(key==='ttk')svg.append(el('text',{x:L+4,y:H-B-8,fill:'#637185','font-size':10},'× 全部未击杀'));
}
function render(){
 const rows=data.rows.filter(r=>r.target===target.value&&r.mode===mode.value).sort((a,b)=>a.distance_m-b.distance_m||a.weapon.localeCompare(b.weapon));
 draw('chart-hit',rows,'hit','expected_hit_rate','命中率');draw('chart-damage',rows,'damage','expected_damage_per_shot','HP / 发');draw('chart-dps',rows,'dps','expected_dps','HP / 秒');draw('chart-ttk',rows,'ttk','mean_ttk_ms','秒',.001);
 document.getElementById('table-title').textContent=`${labels[target.value]} · ${labels[mode.value]} · 距离对照`;
 const body=document.getElementById('rows');body.replaceChildren();
 for(const r of rows){const tr=document.createElement('tr'),values=[labels[r.weapon],r.distance_m,pct(r.expected_hit_rate),pct(r.sampled_hit_rate),num(r.expected_damage_per_shot),num(r.sampled_damage_per_shot),num(r.expected_dps),num(r.sampled_dps),r.mean_ttk_ms===null?'未击杀':num(r.mean_ttk_ms*.001),pct(r.ttk_censored/r.ttk_trials)+` (${r.ttk_censored})`,`${r.ttk_kills} / ${r.ttk_trials}`];
  values.forEach((value,i)=>{const td=document.createElement('td');td.textContent=String(value);if(i===0)td.className='weapon-'+r.weapon;if((i===8||i===9)&&r.ttk_censored>0)td.className='censored';tr.append(td)});body.append(tr)
 }
 const missing=['rifle','smg'].filter(w=>!rows.some(r=>r.weapon===w));document.getElementById('missing').textContent=missing.length?`源文件缺少 ${missing.map(w=>labels[w]).join(' / ')} 的此组合测量。`:'步枪与冲锋枪使用相同标准战士、目标条件及测量节奏。';
}
target.onchange=render;mode.onchange=render;render();
</script></html>'''


NUMERIC = ("distance_m", "sampled_shots", "sampled_hits", "sampled_head_hits",
           "sampled_hit_rate", "expected_hit_rate", "sampled_damage_per_shot",
           "expected_damage_per_shot", "sampled_dps", "expected_dps", "spread_rms_m",
           "cold_magazine_expected_hit_rate", "mechanical_reload_cycle_dps",
           "ttk_trials", "ttk_kills", "ttk_censored")


def load(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if data.get("type") != "range" or not isinstance(data.get("rows"), list) or not data["rows"]:
        raise ValueError("Expected an existing nonempty native range JSON result")
    for field in ("simulation_version", "shot_seed", "dt_ms", "baseline"):
        if field not in data:
            raise ValueError(f"Native range metadata missing: {field}")
    seen = set()
    for i, row in enumerate(data["rows"], 1):
        if row.get("weapon") not in ("rifle", "smg") or row.get("target") not in (
                "full", "upper", "head", "moving") or row.get("mode") not in ("single", "burst", "auto"):
            raise ValueError(f"Unsupported native range row {i}")
        for field in NUMERIC:
            value = row.get(field)
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
                raise ValueError(f"Invalid {field} in row {i}")
        if row["sampled_shots"] <= 0 or row["ttk_trials"] <= 0:
            raise ValueError(f"Empty native sample counts in row {i}")
        if row["ttk_kills"] + row["ttk_censored"] != row["ttk_trials"]:
            raise ValueError(f"Inconsistent TTK counts in row {i}")
        mean = row.get("mean_ttk_ms")
        if mean is not None and (not isinstance(mean, (int, float)) or not math.isfinite(mean) or mean < 0):
            raise ValueError(f"Invalid successful-sample mean TTK in row {i}")
        if (mean is None) != (row["ttk_kills"] == 0):
            raise ValueError(f"TTK mean must be null exactly when no trial killed in row {i}")
        key = (row["weapon"], row["target"], row["mode"], row["distance_m"])
        if key in seen:
            raise ValueError(f"Duplicate native range measurement: {key}")
        seen.add(key)
    return data


def export_csv(path: Path, data: dict) -> None:
    fields = ["weapon", "distance_m", "target", "mode", *NUMERIC[1:], "mean_ttk_ms"]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(data["rows"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("range_json", type=Path, help="Existing native range result; no simulation is run")
    parser.add_argument("--output", type=Path, help="Write a self-contained offline HTML report")
    parser.add_argument("--csv", type=Path, help="Export every native measurement row to CSV")
    args = parser.parse_args()
    if args.output is None and args.csv is None:
        parser.error("Provide --output and/or --csv")
    source = args.range_json.resolve()
    destinations = [p.resolve() for p in (args.output, args.csv) if p is not None]
    if source in destinations or len(set(destinations)) != len(destinations):
        raise ValueError("Input JSON, HTML and CSV must have different paths")
    data = load(source)
    if args.output:
        report = {"source": source.name, "sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "range": data}
        payload = json.dumps(report, separators=(",", ":"), ensure_ascii=True).replace("<", "\\u003c")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(HTML.replace("__DATA__", payload), encoding="utf-8", newline="\n")
    if args.csv:
        export_csv(args.csv, data)
    print(json.dumps({"native_rows": len(data["rows"]),
                      "html": str(args.output) if args.output else None,
                      "csv": str(args.csv) if args.csv else None}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, TypeError, KeyError) as exc:
        print(f"tactical_range_report: {exc}", file=sys.stderr)
        raise SystemExit(1)
