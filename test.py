import subprocess
import sys
import os

def parseMyLddOutputAndCompare(file : str, lddVals : dict, lddKeys : set) :
    keys = set()

    os.environ['LD_LIBRARY_PATH'] = ""    
    proc = subprocess.run(['./main', file], stdout=subprocess.PIPE)
    for line in proc.stdout.decode('ascii').split('\n') :
        parts = line.split()
        
        # Second part of the output starts on line "expected import scheme"
        if parts[0] == "expected" :
            break

        if len(parts) == 0 :
            continue
        
        if parts[0] == 'ld-linux-x86-64.so.2' :
            continue
        
        if len(parts) != 3 :
            sys.stderr.write("Wrong my ldd output: " + line + "\n")
            exit(1)

        if lddVals.get(parts[0]) is not None \
                and lddVals[parts[0]] != parts[2] :
            sys.stderr.write("Not consistent output : " + parts[0] + ' ' + lddVals[parts[0]] + ' ' + parts[2] + "\n")
            exit(1)

        keys.add(parts[0])

    if not lddKeys.issubset(keys) :
        sys.stderr.write("Keys are different: " + str(keys) + " \n\n  " + str(lddKeys) + "\n")
        exit(1)

def isAllowedPath(file : str) -> bool :
    # Only LD_LIBRARY_PATH (empty) + /lib + /usr/lib
    path_components = file.split('/')
    path_components[-1] = ''
    prefix = '/'.join(path_components)

    return prefix == "/lib/" or prefix == "/usr/lib"

def parseLddOutput(file : str) :
    ldd = dict()
    keys = set()
    proc = subprocess.run(['ldd', file], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0 :
        return ldd, keys, False

    for line in proc.stdout.decode('ascii').split('\n') :
        parts = line.split()
        
        if len(parts) != 4 :
            continue
        
        if parts[0] == 'linux-vdso.so.1' :
            continue

        if parts[0] == '/lib64/ld-linux-x86-64.so.2' :
            continue

        if isAllowedPath(parts[2]) :
            ldd[parts[0]] = parts[2]
            keys.add(parts[0])

    return ldd, keys, True

def main() :
    _ = subprocess.run(['g++', '-o', 'main', 'main.cpp', 'worker/ldd_worker.cpp', '-I', 'worker',], stdout=subprocess.PIPE)

    for root, _, files in os.walk("test", topdown = False):
        iter = 0
        for name in files:
            file = os.path.join(root, name)
            d, k, ok = parseLddOutput(file)
            if ok :
                parseMyLddOutputAndCompare(file, d, k)

            iter += 1
            print(str(iter) + "/" + str(len(files)) + " " + str(file))


main()