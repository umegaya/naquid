import os
import sys
import subprocess

def main():
	argv = sys.argv
	for a in argv:
		print(a)
	with open(argv[2], 'w') as fp:
		text = ('set(ssl_src ' + os.linesep)
		for f in subprocess.check_output(['find', argv[1], '-name', '*.c']).split(os.linesep):
			text += ('\t' + f + os.linesep)
		text += ')'
		fp.write(text)

if __name__ == "__main__":
    main()
