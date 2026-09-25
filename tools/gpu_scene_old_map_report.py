"""Real-clock Campaign diagnostics. Logged runs are not formal FPS acceptance."""
import json
import re
import statistics
import sys
from pathlib import Path
from gpu_scene_cost_report import summarize


def rows(text, prefix):
    return [dict((k, float(v)) for k, v in re.findall(r"(\w+)=(-?[\d.]+)", line))
            for line in text.splitlines() if line.startswith(prefix + " ")]


def analyze(root, run):
    name, case = run['name'], run['case']
    text = (root / (name+'.out')).read_text(encoding='utf-8')
    errors = (root / (name+'.err')).read_text(encoding='utf-8')
    assert run['exit_code'] == 0 and not re.search(r'Validation Error|SYNC-HAZARD|VUID-', errors), name
    assert 'Loading world source: rasterfall/assets/maps/rasterfall.map' in text, name
    groups = {p: rows(text, p) for p in (
        'SCENE-RUNTIME', 'SCENE-FRAME-COST', 'SCENE-CPU-COST', 'SCENE-EXTRACT',
        'SCENE-ENEMY-COST', 'SCENE-SUBMIT-COST', 'SCENE-WORLD-COST',
        'SCENE-NATIVE', 'SCENE-SOURCE', 'SCENE-LOCAL', 'SCENE-WORLD-GPU')}
    ids = list(range(1, case['frames']+1))
    for prefix, values in groups.items():
        assert [x['frame'] for x in values] == ids, (name, prefix, 'missing frames')
    # Older evidence predates the optional Game diagnostic sink.
    logic_profiles = {}
    for prefix in ('SCENE-LOGIC-COST', 'SCENE-LOGIC-ENEMIES', 'SCENE-LOGIC-NAV'):
        values = rows(text, prefix)
        if not values:
            continue
        assert [x['frame'] for x in values] == ids, (name, prefix, 'missing frames')
        logic_profiles[prefix] = {
            key.replace('_us', '_ms'): summarize([
                x[key] / (1000 if key.endswith('_us') else 1) for x in values[60:]])
            for key in values[0] if key != 'frame'}
    update_rows = rows(text, 'SCENE-TRIANGLE-UPDATE')
    triangle_updates = {}
    if update_rows:
        by_frame = {int(x['frame']): x for x in update_rows}
        assert len(by_frame) == len(update_rows) and set(by_frame) <= set(ids), name
        triangle_updates = {
            key.replace('_us', '_ms'): summarize([
                by_frame.get(frame, {}).get(key, 0) / (1000 if key.endswith('_us') else 1)
                for frame in ids[60:]])
            for key in update_rows[0] if key != 'frame'}
    assert all(x['bridges'] == x['readback'] == x['mixed_execute'] == x['world_only'] == 0
               for x in groups['SCENE-NATIVE']), name
    assert all(x['independent'] == 1 and x['legacy_producer'] == x['raster_commands'] == x['mixed_draws'] == 0
               for x in groups['SCENE-SOURCE']), name
    runtime = groups['SCENE-RUNTIME']
    assert all(x['fixed_tick'] == (case['clock'] == 'fixed') and x['auto'] == case['auto']
               and x['world'] == 1 and x['width'] == 1280 and x['height'] == 720 for x in runtime), name
    assert all(x['prop_payload'] == 134 for x in groups['SCENE-LOCAL']), (name, 'map content')
    if case['auto']:
        assert 'auto teleport' in text and len({(x['camera_x'], x['camera_z']) for x in runtime}) > 8, name
        assert max(x['alive'] for x in runtime) > 0, (name, 'no live combat')
    else:
        assert 'GPU-NORMAL scene='+case['view']+' enemies='+str(case['enemies']) in text, name
    if case['view'].startswith('west-button'):
        button = rows(text,'SCENE-CORRIDOR-BUTTON')
        expected = case['enemies'] or 16
        assert len(button)==1 and button[0]['spawned']==expected and button[0]['frame']==61, (name,'button failed')
        if expected>16:
            assert button[0]['presses']==expected//16 and button[0]['requested']==expected, (name,'button count')
            assert all('tank' in x for x in runtime), (name,'missing enemy type audit')
        assert all(x['alive']==0 for x in runtime[:60]), (name,'nonempty warmup')
        assert max(x['alive'] for x in runtime[60:])>=expected, (name,'no simultaneous horde')
    warm = 60
    metrics = {}
    for prefix, fields in {
        'SCENE-RUNTIME': ['interval_us','ticks','dropped_us','ai','alive','dying'],
        'SCENE-FRAME-COST': ['whole_loop_us','actors_us','enemies_us','layers_us'],
        'SCENE-CPU-COST': ['logic_us','freeze_us','pose_us','dynamic_source_us','map_prepare_us'],
        'SCENE-EXTRACT': ['geometry_us'],
        'SCENE-ENEMY-COST': ['upload_us','draw_prepare_us','triangles'],
        'SCENE-SUBMIT-COST': ['retire_us','record_us','present_us','acquire_us'],
        'SCENE-WORLD-COST': ['gpu_draw_ms','upload_bytes'],
        'SCENE-WORLD-GPU': ['prop_draws','actor_draws'],
    }.items():
        for key in fields:
            metrics[key.replace('_us','_ms')] = summarize([
                x[key]/(1000 if key.endswith('_us') else 1) for x in groups[prefix][warm:]])
    hot = runtime[warm:]
    # Sum-of-times / sum-of-ticks avoids overweighting zero/one-tick frames.
    ticks = sum(x['ticks'] for x in hot)
    logic_per_tick_ms = (sum(x['logic_us'] for x in groups['SCENE-CPU-COST'][warm:])
                         / ticks / 1000) if ticks else None
    windows = []
    for start in range(warm,len(runtime),100):
        end = min(start+100,len(runtime))
        window_ticks = sum(x['ticks'] for x in runtime[start:end])
        interval = summarize([x['interval_us']/1000 for x in runtime[start:end]])
        windows.append(dict(frame_from=start+1,frame_to=end,interval_ms=interval,
                            fps=1000/interval['mean'],
                            alive=summarize([x['alive'] for x in runtime[start:end]]),
                            ticks=window_ticks/(end-start),
                            logic_per_tick_ms=(sum(x['logic_us'] for x in groups['SCENE-CPU-COST'][start:end])
                                               /window_ticks/1000) if window_ticks else None,
                            dropped_ms=sum(x['dropped_us'] for x in runtime[start:end])/1000))
    type_keys = ('common','heavy','fast','smoker','charger','tank')
    type_ranges = {}
    if all(k in x for x in runtime for k in type_keys):
        assert all(sum(x[k] for k in type_keys)==x['alive'] for x in runtime), (name,'enemy types')
        type_ranges = {k:[min(x[k] for x in hot),max(x[k] for x in hot)] for k in type_keys}
        if case['view']=='west-button-no-tank':
            assert type_ranges['tank']==[0,0], (name,'unexpected tank')
    buckets = {}
    for label, low, high in [('0',0,0),('1-9',1,9),('10-19',10,19),('20-29',20,29),
                             ('30-31',30,31),('32',32,32),('33-63',33,63),('64+',64,9999)]:
        samples = [i for i in range(warm,len(runtime)) if low <= runtime[i]['alive'] <= high
                   and not runtime[i]['paused'] and runtime[i]['state']==0]
        if not samples:
            continue
        buckets[label] = {'frames':len(samples)}
        for prefix, key in [('SCENE-RUNTIME','interval_us'),('SCENE-RUNTIME','ticks'),
                            ('SCENE-CPU-COST','logic_us'),('SCENE-EXTRACT','geometry_us'),
                            ('SCENE-ENEMY-COST','upload_us'),('SCENE-SUBMIT-COST','retire_us')]:
            buckets[label][key.replace('_us','_ms')] = summarize([
                groups[prefix][i][key]/(1000 if key.endswith('_us') else 1) for i in samples])
    samples = json.loads((root/(name+'.cpu.json')).read_text(encoding='utf-8-sig')) or []
    if isinstance(samples,dict):
        samples = [samples]
    advancing = []
    for sample in samples:
        # Repeated last log frame during GPU teardown is not gameplay wall time.
        if (warm < sample['frame'] < case['frames']-2 and sample['process_ms'] is not None
                and any(t['cpu_ms'] is not None for t in sample['threads'])
                and (not advancing or sample['frame'] > advancing[-1]['frame'])):
            advancing.append(sample)
    samples = advancing
    cpu = None
    if len(samples) >= 2:
        a,b = samples[0],samples[-1]
        wall = b['elapsed_ms']-a['elapsed_ms']
        old = {t['id']:t['cpu_ms'] for t in a['threads'] if t['cpu_ms'] is not None}
        thread_deltas = sorted([(t['cpu_ms']-old[t['id']],t['id']) for t in b['threads']
                                if t['id'] in old and t['cpu_ms'] is not None], reverse=True)
        if thread_deltas and wall > 0:
            cpu = dict(window_ms=wall,frame_from=a['frame'],frame_to=b['frame'],
                       process_cores=(b['process_ms']-a['process_ms'])/wall,
                       busiest_thread_cores=thread_deltas[0][0]/wall,
                       busiest_thread_id=thread_deltas[0][1])
    return dict(name=name,case=case,round=run['round'],warmup=warm,metrics=metrics,
                logic_profiles=logic_profiles,
                triangle_updates=triangle_updates,
                fps=1000/metrics['interval_ms']['mean'],
                logic_per_tick_ms=logic_per_tick_ms,windows=windows,
                multistep_fraction=sum(x['ticks']>1 for x in hot)/len(hot),
                playing_frames=sum(x['state']==0 and not x['paused'] for x in hot),
                workload_ranges={key:[min(x[key] for x in hot),max(x[key] for x in hot)]
                                 for key in ('alive','dying','ai','camera_x','camera_z','direction_sy','direction_cy')},
                enemy_type_ranges=type_ranges,
                dropped_ms=sum(x['dropped_us'] for x in hot)/1000,
                buckets=buckets,cpu=cpu)


def compare_nav_ground(root, runs):
    pairs = {}
    for run in runs:
        case = run['case']
        if 'legacy_nav_ground' not in case:
            continue
        key = (run['round'], case['view'], case['enemies'], case['clock'])
        pairs.setdefault(key, {})[bool(case['legacy_nav_ground'])] = run
    comparisons = []
    for key, pair in pairs.items():
        assert set(pair) == {False, True}, ('incomplete nav pair', key)
        optimized, reference = pair[False], pair[True]
        query_savings = None
        if key[-1] == 'fixed':
            texts = [(root / (r['name'] + '.out')).read_text(encoding='utf-8')
                     for r in (optimized, reference)]
            for prefix, fields in {
                'SCENE-RUNTIME': ['frame','ticks','state','paused','camera_x','camera_z',
                    'direction_sy','direction_cy','ai','alive','dying','common','heavy',
                    'fast','smoker','charger','tank'],
                'SCENE-LOGIC-ENEMIES': ['frame','nav_queries'],
                'SCENE-LOGIC-NAV': ['frame','nav_searches','nav_nodes','nav_candidates',
                    'nav_segments','nav_samples','body_queries','body_scans',
                    'segment_queries','segment_scans'],
            }.items():
                a, b = [rows(t, prefix) for t in texts]
                assert len(a) == len(b) == optimized['case']['frames'], (key, prefix)
                assert all(x[f] == y[f] for x, y in zip(a,b) for f in fields), (key, prefix, 'workload drift')
            a, b = [rows(t, 'SCENE-LOGIC-ENEMIES') for t in texts]
            assert all(x['ground_queries'] <= y['ground_queries'] for x,y in zip(a,b)), key
            query_savings = {'ground_queries': sum(y['ground_queries']-x['ground_queries']
                                                   for x,y in zip(a,b))}
            a, b = [rows(t, 'SCENE-LOGIC-NAV') for t in texts]
            if all('ramp_transition_scans' in x for x in a+b):
                assert all(x['ramp_transition_scans'] <= y['ramp_transition_scans']
                           for x,y in zip(a,b)), key
                query_savings['ramp_scans'] = sum(y['ramp_transition_scans']-x['ramp_transition_scans']
                                                for x,y in zip(a,b))
        comparisons.append(dict(round=key[0],view=key[1],enemies=key[2],clock=key[3],
            fixed_workload_checked=key[-1]=='fixed',
            query_savings=query_savings,
            reference=reference['name'],optimized=optimized['name'],
            mean_delta_ms={field: optimized['metrics'][field]['mean']-reference['metrics'][field]['mean']
                           for field in ('interval_ms','whole_loop_ms','logic_ms')},
            logic_mean_ratio=optimized['metrics']['logic_ms']['mean']/reference['metrics']['logic_ms']['mean']))
    return comparisons


def main():
    root = Path(sys.argv[1])
    runs = json.loads((root/'runs.json').read_text(encoding='utf-8-sig'))
    if isinstance(runs,dict):
        runs = [runs]
    config_path = root/'config.json'
    if config_path.exists():
        config = json.loads(config_path.read_text(encoding='utf-8-sig'))
        expected = {(n,r) for n in config['cases'] for r in range(1,config['rounds']+1)}
        actual = {(r['case']['name'],r['round']) for r in runs}
        assert len(runs)==len(expected) and actual==expected, 'Incomplete performance matrix'
    report = [analyze(root,r) for r in runs]
    result = dict(scope='Native Scene, logged diagnostic, real clock and fixed-tick controls; 60 warmup frames. CPU cores are sampled OS thread CPU/wall, not whole-machine utilization.',runs=report,
                  nav_ground_comparisons=compare_nav_ground(root, report))
    (root/'report.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    for name in dict.fromkeys(r['case']['name'] for r in report):
        selected = [r for r in report if r['case']['name']==name]
        aggregate = {k:round(statistics.median(r['metrics'][k]['mean'] for r in selected),3)
                     for k in ('interval_ms','logic_ms','geometry_ms','upload_ms','freeze_ms','retire_ms','ticks','alive')}
        aggregate['fps']=round(statistics.median(r['fps'] for r in selected),2)
        cpu = [r['cpu'] for r in selected if r['cpu']]
        aggregate['busy_thread_cores']=round(statistics.median(c['busiest_thread_cores'] for c in cpu),3) if cpu else None
        print(name,json.dumps(aggregate))


if __name__ == '__main__':
    main()
