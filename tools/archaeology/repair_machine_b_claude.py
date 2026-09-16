#!/usr/bin/env python3
"""Repair Claude event/session boundaries and model global prompt history."""
from __future__ import annotations
import datetime as dt, hashlib, json, re
from collections import Counter, defaultdict
from pathlib import Path
from archive_agent_history import sanitize, sanitize_string, classify, git_hashes, git_head, VERSION

MACHINE = 'machine-b'
TARGET = re.compile(r'(?i)(toyc|toy.?ccompiler|tinylibc|/compiler|bootstrap|standalone|self-host|/tcc|/tas|/tld)')
GENERIC = {'执行','提交','继续','可以','好的','好','确认','完成','ok','yes','go'}

def digest_bytes(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
    return h.hexdigest()
def iso(v):
    if isinstance(v,str): return v
    if isinstance(v,(int,float)):
        sec=v/1000 if v>100000000000 else v
        return dt.datetime.fromtimestamp(sec,dt.timezone.utc).isoformat(timespec='milliseconds').replace('+00:00','Z')
    return None
def load_jsonl(p):
    out=[]
    for n,line in enumerate(p.open(encoding='utf8',errors='replace'),1):
        try:
            x=json.loads(line)
            if isinstance(x,dict): out.append((n,x))
        except json.JSONDecodeError: pass
    return out
def event_sid(x):
    for k in ('sessionId','session_id'):
        if x.get(k): return str(x[k])
    return None
def event_time(x): return iso(x.get('timestamp') or (x.get('message') or {}).get('timestamp') if isinstance(x.get('message'),dict) else x.get('timestamp'))
def event_text(x):
    if isinstance(x.get('message'),dict):
        c=x['message'].get('content','')
        if isinstance(c,str): return c
        if isinstance(c,list): return '\n'.join(str(i.get('text','')) for i in c if isinstance(i,dict) and i.get('text'))
    for k in ('content','text','input','command','stdout','stderr'):
        if k in x and isinstance(x[k],str): return x[k]
    return ''
def event_role(x):
    if x.get('type')=='user': return 'user'
    if x.get('type')=='assistant': return 'assistant'
    if isinstance(x.get('message'),dict) and x['message'].get('role') in ('user','assistant'): return x['message']['role']
    return None
def event_tool(x):
    if isinstance(x.get('message'),dict):
        c=x['message'].get('content',[])
        return sum(1 for i in c if isinstance(i,dict) and i.get('type') in ('tool_use','tool_result')) if isinstance(c,list) else 0
    return 0
def cwd(x, fallback):
    return x.get('cwd') or x.get('project') or (x.get('message') or {}).get('cwd') if isinstance(x.get('message'),dict) else (x.get('cwd') or x.get('project') or fallback)
def norm(s): return ' '.join((s or '').split())
def source_ref(src):
    parts=Path(src).parts
    try:
        i=parts.index('projects')
        return '<MACHINE_B_SOURCE>/claude-projects/'+'/'.join(parts[i+1:])
    except ValueError:
        return '<MACHINE_B_SOURCE>/claude-projects/'+Path(src).name
def parse_history(p, env):
    rows=[]
    for n,line in enumerate(p.open(encoding='utf8',errors='replace'),1):
        try:x=json.loads(line)
        except:continue
        prompt=x.get('display')
        project=x.get('project')
        if not isinstance(prompt,str) or not TARGET.search(prompt+' '+str(project or '')): continue
        rows.append({'source_machine':MACHINE,'source_environment':env,'source_type':'claude_global_history_prompt','evidence_scope':'prompt_only','timestamp':iso(x.get('timestamp')),'project':sanitize_string(project) if project else None,'cwd':sanitize_string(project) if project else None,'prompt':sanitize_string(prompt),'source_file':f'<MACHINE_B_SOURCE>/{env}/claude/history.jsonl','source_record_index':n,'source_sha256':digest_bytes(p)})
    return rows
def write_jsonl(p, rows):
    p.parent.mkdir(parents=True,exist_ok=True)
    with p.open('w',encoding='utf8',newline='\n') as f:
        for x in rows:f.write(json.dumps(x,ensure_ascii=False,sort_keys=True)+'\n')

def main():
    import argparse
    ap=argparse.ArgumentParser(); ap.add_argument('--output',default='local/agent-history'); ap.add_argument('--claude-root',required=True); ap.add_argument('--wsl-history',required=True); ap.add_argument('--windows-history',required=True)
    a=ap.parse_args(); root=Path(a.output); sdir=root/'sanitized'/MACHINE/'sessions'
    oldprov={'previous_model':'one Claude project JSONL file = one session','problem':'some project JSONL files contain multiple sessionId values','status':'previous Machine B session counts invalidated; pending event-level session normalization','previous_source_files':370,'previous_sessions':370,'previous_prompts':2684}
    (root/'reports'/MACHINE/'provisional-audit-correction.json').parent.mkdir(parents=True,exist_ok=True)
    (root/'reports'/MACHINE/'provisional-audit-correction.json').write_text(json.dumps(oldprov,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    sdir.mkdir(parents=True,exist_ok=True)
    for p in sdir.glob('claude-toyc-*.jsonl'): p.unlink()
    groups=defaultdict(list); source_meta={}; unbound=[]; source_count=0; multi=[]
    for p in sorted(Path(a.claude_root).rglob('*.jsonl')):
        source_count+=1; rr=load_jsonl(p); ids={event_sid(x) for _,x in rr if event_sid(x)}
        if len(ids)>1: multi.append({'source_file':str(p),'session_ids':sorted(ids)})
        ph=digest_bytes(p); source_meta[str(p)]={'sha256':ph,'size':p.stat().st_size}
        for n,x in rr:
            sid=event_sid(x)
            item=(str(p),n,x)
            if sid: groups[sid].append(item)
            else: unbound.append(item)
    sessions=[]; full_prompts=[]; links=[]; manifest=[]; seen_global=set(); stats=Counter()
    for sid,items in sorted(groups.items()):
        # Exact duplicate event objects across overlapping files are removed; conflicts remain.
        unique={}; conflicts=[]
        for src,n,x in items:
            key=hashlib.sha256(json.dumps(sanitize(x),ensure_ascii=False,sort_keys=True).encode()).hexdigest()
            if key in unique: continue
            unique[key]=(src,n,x)
        ordered=sorted(unique.values(),key=lambda q:(event_time(q[2]) or '',q[0],q[1]))
        ns='wsl-claude-projects'; aid=f'claude-toyc-{sid}-{hashlib.sha256(ns.encode()).hexdigest()[:10]}'
        out=sdir/(aid+'.jsonl')
        with out.open('w',encoding='utf8',newline='\n') as f:
            for src,n,x in ordered:f.write(json.dumps(sanitize(x),ensure_ascii=False,sort_keys=True)+'\n')
        sh=digest_bytes(out); raw_srcs=sorted({src for src,_,_ in ordered}); srcs=[source_ref(src) for src in raw_srcs]; times=[event_time(x) for _,_,x in ordered if event_time(x)]
        prompts=[(i,n,event_time(x),event_text(x)) for i,(src,n,x) in enumerate(ordered) if event_role(x)=='user' and event_text(x)]
        project=next((sanitize_string(cwd(x,'wsl-claude-projects')) for _,_,x in ordered if cwd(x,'')),'wsl-claude-projects')
        alltext='\n'.join(event_text(x) for _,_,x in ordered); hashes=git_hashes(alltext)
        for i,(event_i,n,t,pr) in enumerate(prompts):
            full_prompts.append({'source_machine':MACHINE,'archive_id':aid,'session_id':sid,'turn_index':i,'timestamp':t,'project':project,'cwd':project,'prompt':sanitize_string(pr),'source_pointer':f'sanitized/{MACHINE}/sessions/{aid}.jsonl#L{event_i+1}','source_sha256':sh,'tags':classify(pr),'evidence_scope':'full_session'})
        for h in hashes: links.append({'source_machine':MACHINE,'session_id':sid,'archive_id':aid,'commit':h,'evidence':'POSSIBLE','basis':'hash appears in git-related Claude session context; no direct commit proof','source_pointer':f'sanitized/{MACHINE}/sessions/{aid}.jsonl'})
        sessions.append({'source_machine':MACHINE,'session_id':sid,'archive_id':aid,'start_time':min(times) if times else None,'end_time':max(times) if times else None,'project':project,'cwd':project,'user_prompt_count':len(prompts),'assistant_turn_count':sum(1 for _,_,x in ordered if event_role(x)=='assistant'),'tool_call_count':sum(event_tool(x) for _,_,x in ordered),'first_prompt':sanitize_string(prompts[0][3]) if prompts else None,'last_prompt':sanitize_string(prompts[-1][3]) if prompts else None,'keywords':sorted(set(re.findall(r'(?i)toyc|toy.?c|tinylibc|toycc|sc7|rasterfall|compiler',alltext))),'tags':classify(alltext),'starting_git_head':git_head(alltext),'ending_git_head':git_head(alltext),'commits_explicitly_created':[],'commits_observed':hashes,'historical_value':'candidate','privacy_status':'sanitized; review required','original_sha256':None,'source_files':srcs,'source_sha256':[source_meta[x]['sha256'] for x in raw_srcs],'sanitized_sha256':sh,'sanitized_at':dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace('+00:00','Z'),'sanitization_version':VERSION,'normalization':'event-level sessionId grouping; exact duplicate events removed; conflicts retained','sanitized_session_path':f'sanitized/{MACHINE}/sessions/{aid}.jsonl'})
    # Keep the Codex layer from the previous valid rollout-boundary archive.
    oldidx=[json.loads(x) for x in (root/'indexes'/MACHINE/'sessions.jsonl').open()] if (root/'indexes'/MACHINE/'sessions.jsonl').exists() else []
    oldprompts=[json.loads(x) for x in (root/'prompts'/MACHINE/'prompts.jsonl').open()] if (root/'prompts'/MACHINE/'prompts.jsonl').exists() else []
    codex_sessions=[x for x in oldidx if x.get('archive_id','').startswith('codex-toyc-')]
    codex_prompts=[x for x in oldprompts if x.get('archive_id','').startswith('codex-toyc-')]
    write_jsonl(root/'indexes'/MACHINE/'sessions.jsonl',sorted(codex_sessions+sessions,key=lambda x:(x.get('start_time') or '',x['archive_id'])))
    write_jsonl(root/'prompts'/MACHINE/'prompts.jsonl',sorted(codex_prompts+full_prompts,key=lambda x:(x.get('timestamp') or '',x['archive_id'],x['turn_index'])))
    write_jsonl(root/'indexes'/MACHINE/'git-links.jsonl',[x for x in (json.loads(y) for y in (root/'indexes'/MACHINE/'git-links.jsonl').open()) if x.get('archive_id','').startswith('codex-toyc-')] + links if (root/'indexes'/MACHINE/'git-links.jsonl').exists() else links)
    histories=parse_history(Path(a.wsl_history),'wsl')+parse_history(Path(a.windows_history),'windows')
    write_jsonl(root/'prompts'/MACHINE/'global-history'/'prompts.jsonl',histories)
    full_by_text=defaultdict(list)
    for x in full_prompts+codex_prompts: full_by_text[norm(x.get('prompt'))].append(x)
    a_prompts=[json.loads(x) for x in (root/'prompts'/'machine-a'/'prompts.jsonl').open()]
    a_by_text=defaultdict(list)
    for x in a_prompts:a_by_text[norm(x.get('prompt'))].append(x)
    matches=[]; a_matches=[]; generic=[]
    for h in histories:
        key=norm(h['prompt']); candidates=full_by_text.get(key,[]); ac=a_by_text.get(key,[])
        if key in GENERIC or len(key)<=8:
            if candidates or ac: generic.append({'prompt':h['prompt'],'global':h['source_environment'],'full_count':len(candidates),'machine_a_count':len(ac)})
            continue
        for x in candidates:
            sameproj=bool(h.get('project') and x.get('project') and (h['project'].split('/')[-1].lower() in x['project'].lower() or x['project'].split('/')[-1].lower() in h['project'].lower()))
            matches.append({'kind':'prompt_only_to_full_candidate','strength':'STRONG' if h.get('source_record_index') and h.get('source_environment')=='wsl' and x.get('session_id')==h.get('session_id') else ('POSSIBLE' if sameproj else 'AMBIGUOUS'),'global_source_environment':h['source_environment'],'global_record':h['source_record_index'],'full_archive_id':x['archive_id'],'full_session_id':x['session_id'],'timestamp_global':h['timestamp'],'timestamp_full':x['timestamp']})
        for x in ac:a_matches.append({'kind':'prompt_only_to_machine_a_candidate','strength':'POSSIBLE','global_source_environment':h['source_environment'],'global_record':h['source_record_index'],'machine_a_archive_id':x['archive_id'],'machine_a_session_id':x['session_id'],'timestamp_global':h['timestamp'],'timestamp_machine_a':x['timestamp']})
    for x in histories:
        if x['prompt'] in GENERIC or len(norm(x['prompt']))<=8: stats['generic']+=1
    report={'generated_at':dt.datetime.now(dt.timezone.utc).isoformat(),'boundary_repair':{'claude_project_jsonl_files':source_count,'multi_session_files':len(multi),'logical_sessions':len(sessions),'cross_file_sessions':sum(1 for sid,items in groups.items() if len({i[0] for i in items})>1),'unbound_events':len(unbound),'exact_duplicate_events_removed':None,'unbound_by_source':Counter(source_ref(x[0]) for x in unbound)},'prompt_only':{'wsl_records':sum(1 for x in histories if x['source_environment']=='wsl'),'windows_records':sum(1 for x in histories if x['source_environment']=='windows'),'full_session_candidates':len(matches),'machine_a_candidates':len(a_matches),'generic_collisions':len(generic)},'candidates':{'prompt_only_to_full':matches,'prompt_only_to_machine_a':a_matches,'generic_text_collisions':generic},'unavailable':['semantic duplicates not compared','raw snapshots not materialized']}
    write_jsonl(root/'reports'/MACHINE/'unbound-events.jsonl',[{'source_file':source_ref(src),'source_record_index':n,'event':sanitize(x)} for src,n,x in unbound])
    (root/'reports'/MACHINE/'boundary-repair.json').write_text(json.dumps(report,ensure_ascii=False,indent=2,default=list)+'\n',encoding='utf8')
    print(json.dumps({'claude_source_files':source_count,'multi_session_files':len(multi),'claude_sessions':len(sessions),'cross_file_sessions':report['boundary_repair']['cross_file_sessions'],'unbound_events':len(unbound),'wsl_history':sum(1 for x in histories if x['source_environment']=='wsl'),'windows_history':sum(1 for x in histories if x['source_environment']=='windows'),'full_prompts':len(full_prompts),'codex_prompts':len(codex_prompts),'prompt_only_full_candidates':len(matches),'prompt_only_a_candidates':len(a_matches),'generic_collisions':len(generic)},ensure_ascii=False))
if __name__=='__main__':main()
