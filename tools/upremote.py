import subprocess
import re
import os
import sys

scp = os.getenv('SCP_CMD', 'scp')
remote = os.getenv('SCP_REMOTE', '127.0.0.1:/')

if len(sys.argv) > 1:
    remote = sys.argv[1]


p = subprocess.Popen(['git', 'status'],stdout=subprocess.PIPE,stderr=subprocess.PIPE);
out,err = p.communicate()

print 'OUT'
print out
print 'ERR'
print err

files_c = re.findall(r"#\s*modified:\s*(.*)", out)
flist = []
for st in files_c:
    flist.append(st)

untracked = out.partition('Untracked files:')[2]
files_u = re.findall(r"#[ \t]*(.*)", untracked)
for st in files_u[2:len(files_u):1]:
    flist.append(st)

print flist

for st in flist:
    cmdpara = st + ' ' + remote + st
    print cmdpara
    p = subprocess.Popen([scp, st, remote + st],stdout=subprocess.PIPE,stderr=subprocess.PIPE);
    out,err = p.communicate()
    print out, err
