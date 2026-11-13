#!/usr/bin/env python3
"""Native Calendar Todo CLI for local AI agents. Windows 11 only; stdlib only."""
from __future__ import annotations
import argparse, datetime as dt, json, os, platform, re, sys
try:
    import winreg
except ImportError:
    winreg = None

ROOT=r"SOFTWARE\Windhawk\Engine\ModsWritable"
MODS=("local@win11-native-todo-calendar","win11-native-todo-calendar")
TASKS="todo-tasks-v2"; OLD="todo-tasks-v1"; CATS="todo-categories-v2"
DEFAULT_CATS=[{"id":"work","name":"工作","hex":"#2563EB"},{"id":"important","name":"重要","hex":"#EA580C"},{"id":"personal","name":"个人","hex":"#059669"}]

class E(RuntimeError): pass

def acc(x): return x | (getattr(winreg,"KEY_WOW64_64KEY",0) if winreg else 0)
def path(mid): return ROOT+"\\"+mid+"\\LocalStorage"

def candidates():
    out=[]; env=os.getenv("CALCTL_MOD_ID","").strip()
    if env: out.append(env)
    out += [x for x in MODS if x not in out]
    if not winreg: return out
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,ROOT,0,acc(winreg.KEY_READ)) as k:
            i=0
            while True:
                try: n=winreg.EnumKey(k,i); i+=1
                except OSError: break
                if n.endswith("win11-native-todo-calendar") and n not in out: out.append(n)
    except OSError: pass
    return out

def mod(write=False):
    if platform.system()!="Windows" or not winreg: raise E("仅支持 Windows 11")
    mode=winreg.KEY_READ | (winreg.KEY_SET_VALUE if write else 0)
    for mid in candidates():
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,path(mid),0,acc(mode)): return mid
        except PermissionError:
            if write: raise E("已找到 Windhawk 存储，但没有写权限；请允许宿主提升权限后重试")
        except OSError: pass
    raise E("未找到 Native Calendar Todo 的 Windhawk 本地存储；请先安装并启用该 mod，或设置 CALCTL_MOD_ID")

def get(mid,name):
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,path(mid),0,acc(winreg.KEY_READ)) as k:
            v,t=winreg.QueryValueEx(k,name)
            if t not in (winreg.REG_SZ,winreg.REG_EXPAND_SZ): raise E(f"{name} 不是字符串值")
            return str(v)
    except FileNotFoundError: return None

def put(mid,name,value):
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,path(mid),0,acc(winreg.KEY_READ|winreg.KEY_SET_VALUE)) as k:
            winreg.SetValueEx(k,name,0,winreg.REG_SZ,value)
    except PermissionError as ex: raise E("没有写入 Windhawk 本地存储的权限") from ex

def esc(s): return str(s).replace("%","%25").replace("\t","%09").replace("\n","%0A").replace("\r","%0D")
def unesc(s):
    d={"25":"%","09":"\t","0A":"\n","0D":"\r"}; out=[]; i=0
    while i<len(s):
        h=d.get(s[i+1:i+3].upper()) if s[i:i+1]=="%" and i+2<len(s) else None
        if h is not None: out.append(h); i+=3
        else: out.append(s[i]); i+=1
    return "".join(out)

def parse2(raw):
    if not raw.startswith("#v2\n"): raise E("todo-tasks-v2 格式异常，为避免损坏数据已停止操作")
    out=[]
    for line in raw[4:].splitlines():
        c=[unesc(x) for x in line.split("\t")]
        if len(c)==8 and re.fullmatch(r"\d{8}",c[0]) and c[3]:
            out.append(dict(day=c[0],done=c[1]=="1",category=c[2] or "work",title=c[3],time=c[4],location=c[5],url=c[6],notes=c[7]))
    return out

def parse1(raw):
    out=[]
    for line in raw.splitlines():
        c=[unesc(x) for x in line.split("\t")]
        if len(c)==4 and re.fullmatch(r"\d{8}",c[0]) and c[3]:
            cat="important" if c[2]=="1" else "personal" if c[2]=="2" else "work"
            out.append(dict(day=c[0],done=c[1]=="1",category=cat,title=c[3],time="",location="",url="",notes=""))
    return out

def dump(tasks):
    rows=["#v2"]
    for t in tasks:
        rows.append("\t".join([esc(t["day"]),"1" if t["done"] else "0",esc(t["category"]),esc(t["title"]),esc(t.get("time","")),esc(t.get("location","")),esc(t.get("url","")),esc(t.get("notes",""))]))
    raw="\n".join(rows)+"\n"
    if len(raw.encode("utf-16-le"))//2>=65535: raise E("待办数据达到 Windhawk 当前存储上限")
    return raw

def load(mid=None):
    mid=mid or mod(); raw=get(mid,TASKS)
    if raw is not None: return mid,parse2(raw),raw
    old=get(mid,OLD); return mid,(parse1(old) if old else []),None

def save(mid,tasks,expected):
    if get(mid,TASKS)!=expected: raise E("待办列表在操作期间发生变化；请重新读取后再试")
    payload=dump(tasks); put(mid,TASKS,payload)
    if get(mid,TASKS)!=payload: raise E("写入后校验失败")

def day(s):
    if not re.fullmatch(r"\d{8}",s or ""): raise E("日期必须是 YYYYMMDD")
    try: dt.datetime.strptime(s,"%Y%m%d")
    except ValueError: raise E("日期无效")
    return s

def tim(s):
    s=(s or "").replace("：",":").strip()
    if not s: return ""
    if not re.fullmatch(r"\d{1,2}:\d{2}(-\d{1,2}:\d{2})?",s): raise E("时间必须类似 09:30 或 09:30-10:15")
    for p in s.split("-"):
        h,m=map(int,p.split(":"))
        if h>23 or m>59: raise E("时间无效")
    return s

def cats(mid):
    raw=get(mid,CATS)
    if not raw: return DEFAULT_CATS
    out=[]
    for line in raw.splitlines():
        c=[unesc(x) for x in line.split("\t")]
        if len(c)==3 and c[0] and c[1] and re.fullmatch(r"#[0-9A-Fa-f]{6}",c[2]): out.append(dict(id=c[0],name=c[1],hex=c[2].upper()))
    return out or DEFAULT_CATS

def cat(mid,x):
    ids={c["id"] for c in cats(mid)}
    if x not in ids: raise E("未知分类 %s；可用：%s"%(x,", ".join(sorted(ids))))
    return x

def pick(tasks,match,on=None):
    q=match.casefold().strip(); hits=[(i,t) for i,t in enumerate(tasks) if (not on or t["day"]==on) and q in t["title"].casefold()]
    if len(hits)!=1: raise E("匹配到 %d 条待办；请使用更具体的 --match%s"%(len(hits)," 或 --on YYYYMMDD" if not on else ""))
    return hits[0]

def show(t,j=False):
    if j: print(json.dumps(t,ensure_ascii=False,indent=2)); return
    print("[%s] %s%s  %s  (%s)"%("✓" if t["done"] else " ",t["day"],(" "+t["time"]) if t.get("time") else "",t["title"],t["category"]))

def show_json(x,j):
    if j: print(json.dumps(x,ensure_ascii=False,indent=2))
    else:
        for k,v in x.items(): print(f"{k}: {v}")

def doctor(a):
    mid=mod(); _,ts,_=load(mid)
    try: mod(True); writable=True
    except E: writable=False
    show_json(dict(ok=True,mod_id=mid,storage="HKLM\\"+path(mid),task_count=len(ts),writable=writable),a.json); return 0

def ls(a):
    _,ts,_=load()
    if a.day: ts=[t for t in ts if t["day"]==day(a.day)]
    if a.pending: ts=[t for t in ts if not t["done"]]
    ts.sort(key=lambda t:(t["day"],t["time"],t["title"].casefold()))
    if a.json: print(json.dumps(ts,ensure_ascii=False,indent=2))
    elif not ts: print("(没有待办)")
    else:
        for t in ts: show(t)
    return 0

def categories(a):
    cs=cats(mod())
    if a.json: print(json.dumps(cs,ensure_ascii=False,indent=2))
    else:
        for c in cs: print(f"{c['id']}\t{c['name']}\t{c['hex']}")
    return 0

def add(a):
    mid=mod(True); _,ts,raw=load(mid); title=a.title.strip()
    if not title: raise E("标题不能为空")
    t=dict(day=day(a.day),done=False,category=cat(mid,a.category),title=title,time=tim(a.time),location=a.location or "",url=a.url or "",notes=a.notes or "")
    ts.append(t); save(mid,ts,raw); show(t,a.json); return 0

def edit(a):
    mid=mod(True); _,ts,raw=load(mid); i,t=pick(ts,a.match,day(a.on) if a.on else None); u=dict(t)
    for k in ("title","location","url","notes"):
        v=getattr(a,k)
        if v is not None: u[k]=v.strip() if k=="title" else v
    if not u["title"]: raise E("标题不能为空")
    if a.day is not None: u["day"]=day(a.day)
    if a.time is not None: u["time"]=tim(a.time)
    if a.category is not None: u["category"]=cat(mid,a.category)
    ts[i]=u; save(mid,ts,raw); show(u,a.json); return 0

def done(a):
    mid=mod(True); _,ts,raw=load(mid); i,t=pick(ts,a.match,day(a.on) if a.on else None); t=dict(t); t["done"]=not a.undo; ts[i]=t; save(mid,ts,raw); show(t,a.json); return 0

def rm(a):
    mid=mod(True); _,ts,raw=load(mid); i,t=pick(ts,a.match,day(a.on) if a.on else None); del ts[i]; save(mid,ts,raw); show_json({"deleted":t},a.json); return 0

def parser():
    p=argparse.ArgumentParser(description="管理 Native Calendar Todo 本地待办"); s=p.add_subparsers(required=True)
    q=s.add_parser("doctor"); q.add_argument("--json",action="store_true"); q.set_defaults(f=doctor)
    q=s.add_parser("list"); q.add_argument("--day"); q.add_argument("--pending",action="store_true"); q.add_argument("--json",action="store_true"); q.set_defaults(f=ls)
    q=s.add_parser("categories"); q.add_argument("--json",action="store_true"); q.set_defaults(f=categories)
    q=s.add_parser("add"); q.add_argument("--title",required=True); q.add_argument("--day",required=True); q.add_argument("--time",default=""); q.add_argument("--category",default="work"); q.add_argument("--location",default=""); q.add_argument("--url",default=""); q.add_argument("--notes",default=""); q.add_argument("--json",action="store_true"); q.set_defaults(f=add)
    q=s.add_parser("edit"); q.add_argument("--match",required=True); q.add_argument("--on"); q.add_argument("--title"); q.add_argument("--day"); q.add_argument("--time"); q.add_argument("--category"); q.add_argument("--location"); q.add_argument("--url"); q.add_argument("--notes"); q.add_argument("--json",action="store_true"); q.set_defaults(f=edit)
    q=s.add_parser("done"); q.add_argument("--match",required=True); q.add_argument("--on"); q.add_argument("--undo",action="store_true"); q.add_argument("--json",action="store_true"); q.set_defaults(f=done)
    q=s.add_parser("rm"); q.add_argument("--match",required=True); q.add_argument("--on"); q.add_argument("--json",action="store_true"); q.set_defaults(f=rm)
    return p

def main():
    try:
        a=parser().parse_args(); return a.f(a)
    except E as e: print("ERROR:",e,file=sys.stderr); return 2
    except OSError as e: print("ERROR: Windows/注册表操作失败：",e,file=sys.stderr); return 2
if __name__=="__main__": raise SystemExit(main())
