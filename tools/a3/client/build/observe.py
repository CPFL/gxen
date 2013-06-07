# -*- coding: utf-8 -*-
import sys
import time
import subprocess

def main():
    try:
	prev = -1
	while True:
	    now = subprocess.check_output(['./a3-client', 'register', '0x400700'], universal_newlines=True).strip()
	    if prev != now:
		print now
	    prev = now
	    time.sleep(0.3)
    except KeyboardInterrupt:
	sys.exit(0)

if __name__ == '__main__':
    main()
