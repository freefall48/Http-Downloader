import time
import os
import subprocess

start = time.time_ns()
subprocess.call("./downloader large.txt 6 files", shell=True)
end = time.time_ns()

print(end - start)