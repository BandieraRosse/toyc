#!/usr/bin/env python3
"""Recover Claude and Codex Toyc history into the machine-independent archive."""
from __future__ import annotations

import argparse, datetime as dt, hashlib, json, re
from pathlib import Path
from collections import Counter

from archive_agent_history import sanitize, sanitize_string, classify, git_hashes, git_head, message_text, VERSION

def sha256(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024), b''): h.update(b)
    return h.hexdigest()

def iso(v):
    if isinstance(v,str): return v
    if isinstance(v,(int,float)):
        sec=v/1000 if v>100000000000 else v
        return dt.datetime.fromtimestamp(sec,dt.timezone.utc).isoformat(timespec='milliseconds').replace('+00:00','Z')
    return None

def records(p):
    out=[]
    for n,line in enumerate(p.open(encoding='utf-8',errors='replace'),1):
        try:
            x=json.loads(line)
            if isinstance(x,dict): out.append((n,x))
        except json.JSONDecodeError: pass
    return out

def payload(x): return x.get('payload',{}) if isinstance(x.get('payload'),dict) else {}

def sid(x):
    p=payload(x)
    return str(x.get('sessionId') or x.get('session_id') or p.get('session_id') or p.get('id') or '')

def stamp(x):
    p=payload(x)
    return iso(x.get('timestamp') or x.get('ts') or p.get('timestamp') or p.get('ts') or p.get('started_at') or p.get('completed_at'))

def role(x):
    p=payload(x)
    if x.get('type')=='user': return 'user'
    if x.get('type')=='assistant': return 'assistant'
    if x.get('type')=='event_msg' and p.get('type')=='user_message': return 'user'
    if x.get('type')=='event_msg' and p.get('type')=='agent_message': return 'assistant'
    # Codex mirrors each human turn as response_item.message(role=user) and
    # event_msg.user_message.  The latter is the canonical human prompt;
    # accepting both would duplicate the ledger and include context packets.
    if x.get('type')=='response_item' and p.get('type')=='message' and p.get('role')=='assistant': return 'assistant'
    return None

def text(x):
    p=payload(x)
    if x.get('type')=='event_msg' and p.get('type') in ('user_message','agent_message'): return str(p.get('message') or '')
    if x.get('type')=='response_item' and p.get('type')=='message':
        c=p.get('content',[]); c=c if isinstance(c,list) else [c]
        return '\n'.join(str(i.get('text','')) for i in c if isinstance(i,dict) and i.get('text'))
    return message_text(x.get('message',x.get('content',x.get('input',''))))

def cwd(x, fallback):
    p=payload(x)
    return x.get('cwd') or x.get('project') or p.get('cwd') or fallback

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--output',default='local/agent-history')
    ap.add_argument('--claude-root',required=True)
    ap.add_argument('--codex-root',required=True)
    ap.add_argument('--claude-history',required=True)
    ap.add_argument('--plans-root',required=True)
    a=ap.parse_args(); root=Path(a.output); machine='machine-b'
    sessdir=root/'sanitized'/machine/'sessions'; sessdir.mkdir(parents=True,exist_ok=True)
    for p in sessdir.glob('*.jsonl'): p.unlink()
    files=[]
    for p in sorted(Path(a.claude_root).glob('*.jsonl')): files.append(('claude-toyc',p,'claude-code-session-jsonl'))
    for p in sorted(Path(a.codex_root).rglob('*.jsonl')):
        rr=records(p)
        if rr and str(cwd(rr[0][1],'')).lower().rstrip('/')=='/mnt/c/users/legion/desktop/toyc': files.append(('codex-toyc',p,'codex-rollout-jsonl'))
    sessions=[]; prompts=[]; links=[]; manifest=[]; totals=Counter(); secret=[]
    for label,p,stype in files:
        rr=records(p)
        if not rr: continue
        session_id=next((sid(x) for _,x in rr if sid(x)),p.stem)
        rel=p.name if label.startswith('claude') else str(p.relative_to(Path(a.codex_root)))
        suffix=hashlib.sha256(rel.encode()).hexdigest()[:10]
        archive=f'{label}-{session_id}-{suffix}'
        out=sessdir/(archive+'.jsonl'); raw_hash=sha256(p)
        with out.open('w',encoding='utf-8',newline='\n') as f:
            for n,x in rr:
                raw=json.dumps(x,ensure_ascii=False)
                if re.search(r'(?i)(bearer\s+|api[_-]?key|access[_-]?token|password|cookie|private key)',raw): secret.append(f'{label}/{rel}:L{n}')
                f.write(json.dumps(sanitize(x),ensure_ascii=False,sort_keys=True)+'\n')
        sh=sha256(out); us=[(n,stamp(x),text(x)) for n,x in rr if role(x)=='user' and text(x)]
        times=[stamp(x) for _,x in rr if stamp(x)]; alltext='\n'.join(text(x) for _,x in rr)
        project=sanitize_string(next((cwd(x,'') for _,x in rr if cwd(x,'')),label))
        assistant=sum(1 for _,x in rr if role(x)=='assistant'); tools=sum(1 for _,x in rr if x.get('type')=='response_item' and payload(x).get('type') in ('custom_tool_call','function_call'))
        for i,(n,t,pr) in enumerate(us):
            prompts.append({'source_machine':machine,'archive_id':archive,'session_id':session_id,'turn_index':i,'timestamp':t,'project':project,'cwd':project,'prompt':sanitize_string(pr),'source_pointer':f'sanitized/{machine}/sessions/{archive}.jsonl#L{n}','source_sha256':sh,'tags':classify(pr)})
        hashes=git_hashes(alltext)
        sessions.append({'source_machine':machine,'session_id':session_id,'archive_id':archive,'start_time':min(times) if times else None,'end_time':max(times) if times else None,'project':project,'cwd':project,'user_prompt_count':len(us),'assistant_turn_count':assistant,'tool_call_count':tools,'first_prompt':sanitize_string(us[0][2]) if us else None,'last_prompt':sanitize_string(us[-1][2]) if us else None,'keywords':sorted(set(re.findall(r'(?i)toyc|toy.?c|tinylibc|toycc|sc7|rasterfall|compiler',alltext))),'tags':classify(alltext),'starting_git_head':git_head(alltext),'ending_git_head':git_head(alltext),'commits_explicitly_created':[],'commits_observed':hashes,'historical_value':'candidate','privacy_status':'sanitized; review required','original_sha256':raw_hash,'sanitized_sha256':sh,'sanitized_at':dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace('+00:00','Z'),'sanitization_version':VERSION,'sanitized_session_path':f'sanitized/{machine}/sessions/{archive}.jsonl'})
        for h in hashes: links.append({'source_machine':machine,'session_id':session_id,'archive_id':archive,'commit':h,'evidence':'POSSIBLE','basis':'hash appears in git-related session context; no direct commit proof','source_pointer':f'sanitized/{machine}/sessions/{archive}.jsonl'})
        manifest.append({'source_machine':machine,'source_label':label,'source_path':f'<MACHINE_B_SOURCE>/{rel}','source_type':stype,'session_id':session_id,'archive_id':archive,'original_size':p.stat().st_size,'original_sha256':raw_hash,'sanitized_size':out.stat().st_size,'sanitized_sha256':sh,'sanitization_version':VERSION})
        totals['raw']+=p.stat().st_size; totals['sanitized']+=out.stat().st_size
    def dump(path,rows):
        path.parent.mkdir(parents=True,exist_ok=True)
        with path.open('w',encoding='utf-8',newline='\n') as f:
            for x in rows: f.write(json.dumps(x,ensure_ascii=False,sort_keys=True)+'\n')
    dump(root/'indexes'/machine/'sessions.jsonl',sorted(sessions,key=lambda x:(x['start_time'] or '',x['archive_id'])))
    dump(root/'prompts'/machine/'prompts.jsonl',sorted(prompts,key=lambda x:(x['timestamp'] or '',x['archive_id'],x['turn_index'])))
    dump(root/'indexes'/machine/'git-links.jsonl',links)
    dump(root/'manifests'/machine/'source-files.jsonl',manifest)
    plan_dir=root/'sanitized'/machine/'plans'; plan_manifest=[]
    for p in sorted(Path(a.plans_root).glob('*.md')):
        raw=p.read_text(encoding='utf-8',errors='replace'); out=plan_dir/(p.stem+'.md'); out.parent.mkdir(parents=True,exist_ok=True); out.write_text(sanitize_string(raw),encoding='utf-8')
        plan_manifest.append({'source_machine':machine,'source_label':'claude-plans','source_path':f'<MACHINE_B_SOURCE>/plans/{p.name}','source_type':'claude-plan-markdown','session_id':None,'archive_id':f'plan-{p.stem}','original_size':p.stat().st_size,'original_sha256':sha256(p),'sanitized_size':out.stat().st_size,'sanitized_sha256':sha256(out),'sanitization_version':VERSION})
    dump(root/'manifests'/machine/'plans.jsonl',plan_manifest)
    hist=[]
    for n,line in enumerate(Path(a.claude_history).open(encoding='utf-8',errors='replace'),1):
        try: x=json.loads(line)
        except: continue
        pr=x.get('display',''); proj=x.get('project','')
        if re.search(r'(?i)tinylibc|toyc|toy.?ccompiler|/compiler|bootstrap|standalone|self-host|/tcc|/tas|/tld',pr+' '+proj): hist.append({'line':n,'session_id':x.get('sessionId'),'project':sanitize_string(proj),'timestamp':iso(x.get('timestamp')),'prompt':sanitize_string(pr)})
    dump(root/'reports'/machine/'history-only-candidates.jsonl',hist)
    dump(root/'reports'/machine/'archive-run.json', [{'source_machine':machine,'generated_at':dt.datetime.now(dt.timezone.utc).isoformat(),'source_files':len(files),'sessions':len(sessions),'prompts':len(prompts),'raw_bytes':totals['raw'],'sanitized_bytes':totals['sanitized'],'secret_pattern_hits':len(secret),'secret_hit_pointers':secret,'prompt_only_candidates':len(hist)}])
    print(json.dumps({'source_files':len(files),'sessions':len(sessions),'prompts':len(prompts),'raw_bytes':totals['raw'],'sanitized_bytes':totals['sanitized'],'secret_pattern_hits':len(secret),'history_only_candidates':len(hist)},ensure_ascii=False))
if __name__=='__main__': main()
